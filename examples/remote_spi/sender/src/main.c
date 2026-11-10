/*
 * Sender (board A): has no SPI devices. It formulates SPI transfers as GBB
 * Requests and sends them over Ethernet to the receiver, which runs them on its
 * SPI1 bus. Twice per second it:
 *   1. paints a checkered bar on the receiver's display (Write Request)
 *   2. reads a register of the receiver's BME68x sensor (Read Request)
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "remote_spi.h"

/* Log messages from this file appear as "sender: ..."; the level is set in common/debug.conf */
LOG_MODULE_REGISTER(sender, CONFIG_REMOTE_SPI_LOG_LEVEL);

/* SHARP display: 144 pixels per line, one bit per pixel = 18 bytes */
#define DISPLAY_BYTES_PER_LINE 18
/* Paint lines 1 to 12: empty space above the "Air Quality" title */
#define BAR_LINES 12
/* Display mode byte "write lines" (the receiver sends display bytes LSB first) */
#define DISPLAY_CMD_WRITE 0x01

/* BME68x status register: reachable from both register pages, so always safe to read */
#define BME68X_REG_STATUS 0x73
/* Over SPI, bit 7 of the register address means "read" */
#define BME68X_SPI_READ 0x80
/* Bit 4 of the status register: which register page is selected */
#define BME68X_MEM_PAGE_BIT 0x10

/* Log the link stats after this many requests */
#define STATS_EVERY 20

/* Datagram buffers, static so they don't use up the main stack */
static uint8_t tx_frame[REMOTE_SPI_FRAME_SIZE];
static uint8_t rx_frame[REMOTE_SPI_FRAME_SIZE];

static int sock;				/* UDP socket to the receiver */
static struct net_sockaddr_in receiver;	/* Receiver's IP address and port */
static uint32_t seq_num;			/* Counts sent datagrams (Annex J / NTSCF) */
static uint8_t transaction_num;			/* Counts GBB Requests, echoed in each response */

/* Link stats: what happened to the requests so far */
static struct {
	uint32_t sent;		/* Requests sent */
	uint32_t ok;		/* Answered without error */
	uint32_t timeouts;	/* No answer within REMOTE_SPI_TIMEOUT_MS */
	uint32_t errors;	/* Answered with err = 1, or a local socket error */
	uint32_t dropped;	/* Received datagrams that were not the expected answer */
	uint32_t rtt_min_us;	/* Shortest round trip */
	uint32_t rtt_max_us;	/* Longest round trip */
	uint64_t rtt_sum_us;	/* Sum of round trips, for the average */
} stats = {.rtt_min_us = UINT32_MAX};

/*
 * Sends one GBB Request to Byte Bus bus_id and waits for its GBB Response.
 * write = true:  payload is clocked out on the bus.
 * write = false: payload is clocked out, then read_size bytes are read into read_buf.
 * Returns the number of bytes read (0 for a write), -ETIMEDOUT if no answer
 * came, -EIO if the receiver reported an error, or another negative errno.
 */
static int gbb_request(uint16_t bus_id, bool write, uint8_t *payload, uint16_t len,
		       uint8_t *read_buf, uint16_t read_size)
{
	Avtp_Gbb_t *req = frame_gbb(tx_frame);
	size_t frame_len;
	uint32_t start;

	/* A new number per request, so an old late answer can't be mistaken for this one */
	transaction_num++;

	/* GBB header + payload: which bus, write or read, transaction number, bytes to clock out */
	Avtp_Gbb_CreateAcfMessage(req, bus_id, write, transaction_num, payload, len);
	if (!write) {
		/* Only Read Requests say how many bytes to read after the payload */
		Avtp_Gbb_SetReadSize(req, read_size);
	}
	frame_log_gbb("request out", req);

	/* Put the Annex J and NTSCF headers in front of the GBB message */
	frame_len = frame_finish(tx_frame, seq_num++);

	/* Start the round-trip clock right before sending */
	start = k_cycle_get_32();
	stats.sent++;

	/* One GBB message = one UDP datagram */
	if (zsock_sendto(sock, tx_frame, frame_len, 0, (struct net_sockaddr *)&receiver,
			 sizeof(receiver)) < 0) {
		int err = errno;

		/* E.g. ENETUNREACH: no link or no IP yet ("net iface" in the shell shows which) */
		LOG_ERR("request %u: sendto failed (%d)", transaction_num, err);
		stats.errors++;
		return -err;
	}

	/* Wait for the answer; anything that is not our answer is skipped */
	for (;;) {
		/* Blocks for at most REMOTE_SPI_TIMEOUT_MS (set with SO_RCVTIMEO in main) */
		ssize_t n = zsock_recv(sock, rx_frame, sizeof(rx_frame), 0);
		const Avtp_Gbb_t *rsp;
		uint32_t rtt_us;
		uint16_t got;

		if (n < 0) {
			int err = errno;

			if (err == EAGAIN) {
				/* Receiver not running, cable unplugged, or the answer was lost */
				LOG_WRN("request %u: no answer within %d ms", transaction_num,
					REMOTE_SPI_TIMEOUT_MS);
				stats.timeouts++;
				return -ETIMEDOUT;
			}
			LOG_ERR("request %u: recv failed (%d)", transaction_num, err);
			stats.errors++;
			return -err;
		}

		/* Unwrap Annex J + NTSCF; NULL means the datagram was not a valid GBB frame */
		rsp = frame_parse(rx_frame, n);
		if (rsp == NULL) {
			/* frame_parse() already logged why */
			stats.dropped++;
			continue;
		}
		if (!Avtp_Gbb_IsRsp(rsp) || Avtp_Gbb_GetTransactionNum(rsp) != transaction_num) {
			/* Usually a late answer to an earlier request that timed out */
			LOG_WRN("request %u: dropped %s %u (not our answer)", transaction_num,
				Avtp_Gbb_IsRsp(rsp) ? "response" : "request",
				Avtp_Gbb_GetTransactionNum(rsp));
			stats.dropped++;
			continue;
		}

		/* Our answer: stop the round-trip clock */
		rtt_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
		stats.rtt_min_us = MIN(stats.rtt_min_us, rtt_us);
		stats.rtt_max_us = MAX(stats.rtt_max_us, rtt_us);
		stats.rtt_sum_us += rtt_us;
		frame_log_gbb("response in", rsp);
		LOG_DBG("request %u: round trip %u us", transaction_num, rtt_us);

		/* The receiver could not run the transfer (its log says why) */
		if (Avtp_Gbb_IsErr(rsp)) {
			LOG_WRN("request %u: receiver answered with err = 1", transaction_num);
			stats.errors++;
			return -EIO;
		}
		stats.ok++;

		/* A read answer carries exactly the bytes read; a write answer carries none */
		got = MIN(Avtp_Gbb_GetPayloadLength(rsp), read_size);
		if (Avtp_Gbb_GetPayloadLength(rsp) != read_size) {
			LOG_WRN("request %u: expected %u bytes back, got %u", transaction_num,
				read_size, Avtp_Gbb_GetPayloadLength(rsp));
		}
		if (got > 0) {
			memcpy(read_buf, Avtp_Gbb_GetPayload(rsp), got);
		}
		return got;
	}
}

/* Logs the link stats (info level) */
static void log_stats(void)
{
	LOG_INF("stats: %u sent, %u ok, %u timeouts, %u errors, %u dropped; "
		"round trip min/avg/max %u/%u/%u us",
		stats.sent, stats.ok, stats.timeouts, stats.errors, stats.dropped,
		stats.ok > 0 ? stats.rtt_min_us : 0,
		stats.ok > 0 ? (uint32_t)(stats.rtt_sum_us / stats.ok) : 0, stats.rtt_max_us);
}

/*
 * Fills buf with the SPI bytes that make the display draw BAR_LINES lines of
 * 4x4-pixel checkers. This is the SHARP display's own protocol, which only the
 * sender knows: the receiver just forwards the bytes. Returns the length.
 */
static uint16_t build_bar(uint8_t *buf)
{
	uint16_t i = 0;

	/* Mode byte: "write lines" (the VCOM bit stays 0, the receiver's driver toggles it) */
	buf[i++] = DISPLAY_CMD_WRITE;

	for (uint8_t line = 1; line <= BAR_LINES; line++) {
		/* Line address: the display counts lines from 1 */
		buf[i++] = line;
		/* 18 bytes of pixels; the pattern flips every 4 lines to make checkers */
		memset(&buf[i], ((line - 1) / 4) % 2 ? 0x0F : 0xF0, DISPLAY_BYTES_PER_LINE);
		i += DISPLAY_BYTES_PER_LINE;
		/* 8 dummy bits end each line */
		buf[i++] = 0x00;
	}

	/* 8 more dummy bits end the whole command */
	buf[i++] = 0x00;

	return i;
}

int main(void)
{
	/* 1 mode byte + 20 bytes per line + 1 trailer = 242 bytes, fits into one GBB message */
	static uint8_t bar[1 + BAR_LINES * (1 + DISPLAY_BYTES_PER_LINE + 1) + 1];
	uint16_t bar_len = build_bar(bar);
	/* How long recv() waits for an answer before giving up */
	struct zsock_timeval timeout = {
		.tv_sec = 0,
		.tv_usec = REMOTE_SPI_TIMEOUT_MS * 1000,
	};

	BUILD_ASSERT(sizeof(bar) <= REMOTE_SPI_MAX_PAYLOAD, "bar does not fit into one GBB message");

	/* Where to send: receiver IP + AVTP port */
	receiver.sin_family = NET_AF_INET;
	receiver.sin_port = net_htons(REMOTE_SPI_PORT);
	net_addr_pton(NET_AF_INET, RECEIVER_IP, &receiver.sin_addr);

	/* UDP socket; it gets a free local port automatically on the first send */
	sock = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("could not open a socket (%d)", errno);
		return 0;
	}

	if (zsock_setsockopt(sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &timeout,
			     sizeof(timeout)) < 0) {
		LOG_ERR("could not set the receive timeout (%d)", errno);
		return 0;
	}

	LOG_INF("sender up: %s -> %s:%d, timeout %d ms, display bar %u B",
		CONFIG_NET_CONFIG_MY_IPV4_ADDR, RECEIVER_IP, REMOTE_SPI_PORT,
		REMOTE_SPI_TIMEOUT_MS, bar_len);

	while (1) {
		/* SPI bytes for the sensor: just the register address with the read bit */
		uint8_t read_cmd = BME68X_REG_STATUS | BME68X_SPI_READ;
		uint8_t status;
		int ret;

		/* 1. Write Request: the receiver clocks the bar bytes out to the display */
		ret = gbb_request(BYTE_BUS_DISPLAY, true, bar, bar_len, NULL, 0);
		if (ret == 0) {
			LOG_INF("request %u: display write ok", transaction_num);
		} else {
			LOG_WRN("request %u: display write failed (%d)", transaction_num, ret);
		}

		/* 2. Read Request: clock out 1 address byte, then read 1 byte back */
		ret = gbb_request(BYTE_BUS_SENSOR, false, &read_cmd, 1, &status, 1);
		if (ret == 1) {
			LOG_INF("request %u: sensor status 0x%02x (register page %d)",
				transaction_num, status, (status & BME68X_MEM_PAGE_BIT) ? 1 : 0);
		} else {
			LOG_WRN("request %u: sensor read failed (%d)", transaction_num, ret);
		}

		/* Two requests per round, so this logs every STATS_EVERY requests */
		if (stats.sent % STATS_EVERY == 0) {
			log_stats();
		}

		k_msleep(500);
	}

	return 0;
}
