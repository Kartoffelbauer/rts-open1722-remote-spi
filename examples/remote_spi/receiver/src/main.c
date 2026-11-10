/*
 * Receiver (board B): runs the air_quality example in main() as before, and a
 * second thread, the remote SPI server. The server receives GBB Requests over
 * Ethernet, runs each one as a raw SPI transfer on SPI1 and sends back a GBB
 * Response. It does not know what the bytes mean: the sender decides that.
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <stdio.h>

#include "remote_spi.h"

/* Log messages from this file appear as "receiver: ..."; the level is set in common/debug.conf */
LOG_MODULE_REGISTER(receiver, CONFIG_REMOTE_SPI_LOG_LEVEL);

/* Log the server stats after this many requests */
#define STATS_EVERY 20

/* ---------- Remote SPI server ---------- */

/* What the server needs to know about one SPI device (one Byte Bus) */
struct byte_bus {
	const char *name;	/* Only for the log */
	struct spi_dt_spec spi;	/* SPI bus, chip select and settings of the device */
	bool can_write;		/* Write Requests allowed? */
	bool can_read;		/* Read Requests allowed? */
};

/*
 * The Byte Bus table, indexed by Byte Bus ID. The chip select and speed come
 * from the same devicetree nodes the local drivers use, and the bit order and
 * SPI mode match those drivers, so remote transfers look the same on the wire.
 * ID 0 stays empty (spi.bus == NULL) and is answered with an error.
 */
static const struct byte_bus byte_buses[] = {
	[BYTE_BUS_SENSOR] = {
		.name = "BME68x",
		/* BME68x: MSB first, SPI mode 3, like Zephyr's bme680 driver */
		.spi = SPI_DT_SPEC_GET(DT_NODELABEL(bme68x), SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
							     SPI_MODE_CPOL | SPI_MODE_CPHA),
		/*
		 * No writes: the local bme680 driver remembers which register page
		 * the sensor is on, and a remote write could switch it behind its back
		 */
		.can_write = false,
		.can_read = true,
	},
	[BYTE_BUS_DISPLAY] = {
		.name = "SHARP display",
		/* SHARP display: LSB first, CS active-high, like Zephyr's ls0xx driver */
		.spi = SPI_DT_SPEC_GET(DT_NODELABEL(sharp_lcd), SPI_WORD_SET(8) | SPI_TRANSFER_LSB |
								SPI_CS_ACTIVE_HIGH),
		.can_write = true,
		/* No reads: the display has no MISO wire */
		.can_read = false,
	},
};

/* Datagram buffers and the bytes read from a device, static to keep the thread stack small */
static uint8_t rx_frame[REMOTE_SPI_FRAME_SIZE];
static uint8_t tx_frame[REMOTE_SPI_FRAME_SIZE];
static uint8_t read_buf[REMOTE_SPI_MAX_PAYLOAD];

/* Logs the Byte Bus table once at start-up, to check the SPI settings against the wiring */
static void log_byte_buses(void)
{
	for (size_t id = 0; id < ARRAY_SIZE(byte_buses); id++) {
		const struct byte_bus *bus = &byte_buses[id];
		const struct spi_config *cfg = &bus->spi.config;

		/* Empty table slot (ID 0) */
		if (bus->spi.bus == NULL) {
			continue;
		}

		/* The SPI mode number (0 to 3) is made of the CPOL and CPHA bits */
		LOG_INF("bus %zu = %s: %s, CS %s pin %u (active %s), %u Hz, mode %d, %s first, "
			"write %s, read %s",
			id, bus->name, bus->spi.bus->name, cfg->cs.gpio.port->name,
			cfg->cs.gpio.pin, (cfg->cs.gpio.dt_flags & GPIO_ACTIVE_LOW) ? "low" : "high",
			cfg->frequency,
			((cfg->operation & SPI_MODE_CPOL) ? 2 : 0) +
				((cfg->operation & SPI_MODE_CPHA) ? 1 : 0),
			(cfg->operation & SPI_TRANSFER_LSB) ? "LSB" : "MSB",
			bus->can_write ? "yes" : "no", bus->can_read ? "yes" : "no");

		if (!spi_is_ready_dt(&bus->spi)) {
			LOG_ERR("bus %zu: SPI controller or CS GPIO not ready", id);
		}
	}
}

/*
 * Runs one GBB Request as one SPI transfer (one chip select assertion).
 * Returns the number of bytes read (0 for a write) or a negative errno.
 */
static int run_request(const Avtp_Gbb_t *req)
{
	uint16_t bus_id = Avtp_Gbb_GetByteBusId(req);
	bool write = Avtp_Gbb_IsOp(req);			/* op: 1 = write, 0 = read */
	void *payload = (void *)Avtp_Gbb_GetPayload(req);	/* Bytes to clock out */
	uint16_t len = Avtp_Gbb_GetPayloadLength(req);
	uint16_t read_size = Avtp_Gbb_GetReadSize(req);		/* Bytes to read afterwards */
	const struct byte_bus *bus;
	uint32_t start;
	int ret;

	/* Unknown Byte Bus ID */
	if (bus_id >= ARRAY_SIZE(byte_buses) || byte_buses[bus_id].spi.bus == NULL) {
		LOG_WRN("rejected: no Byte Bus %u", bus_id);
		return -ENODEV;
	}
	bus = &byte_buses[bus_id];

	if (write) {
		/* Write Request: clock the payload out, nothing to read */
		struct spi_buf tx_buf = {.buf = payload, .len = len};
		struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};

		if (!bus->can_write) {
			LOG_WRN("rejected: writes are not allowed on bus %u (%s)", bus_id, bus->name);
			return -EINVAL;
		}
		if (len == 0) {
			LOG_WRN("rejected: Write Request without payload");
			return -EINVAL;
		}

		/*
		 * The SPI driver lets only one transfer use SPI1 at a time, so this
		 * waits if the local display or sensor driver is busy right now
		 */
		start = k_cycle_get_32();
		ret = spi_write_dt(&bus->spi, &tx);
		LOG_DBG("bus %u: SPI write of %u B took %u us (incl. waiting for SPI1)", bus_id, len,
			k_cyc_to_us_floor32(k_cycle_get_32() - start));
		LOG_HEXDUMP_DBG(payload, MIN(len, REMOTE_SPI_LOG_DUMP_MAX), "SPI out (first bytes)");
		if (ret < 0) {
			LOG_ERR("bus %u: SPI write failed (%d)", bus_id, ret);
		}
		return ret < 0 ? ret : 0;
	}

	/*
	 * Read Request. A read_size with bit 11 set (KiB units) is larger than
	 * read_buf, so it is rejected here as well
	 */
	if (!bus->can_read) {
		LOG_WRN("rejected: reads are not possible on bus %u (%s)", bus_id, bus->name);
		return -EINVAL;
	}
	if (read_size == 0 || read_size > sizeof(read_buf)) {
		LOG_WRN("rejected: read_size %u (allowed 1 to %zu)", read_size, sizeof(read_buf));
		return -EINVAL;
	}

	/* Send: first the payload (e.g. register address), then zeros while reading */
	struct spi_buf tx_bufs[] = {
		{.buf = payload, .len = len},
		{.buf = NULL, .len = read_size},
	};
	/* Receive: ignore what comes in during the payload, keep the read_size bytes after it */
	struct spi_buf rx_bufs[] = {
		{.buf = NULL, .len = len},
		{.buf = read_buf, .len = read_size},
	};
	struct spi_buf_set tx = {.buffers = tx_bufs, .count = ARRAY_SIZE(tx_bufs)};
	struct spi_buf_set rx = {.buffers = rx_bufs, .count = ARRAY_SIZE(rx_bufs)};

	start = k_cycle_get_32();
	ret = spi_transceive_dt(&bus->spi, &tx, &rx);
	LOG_DBG("bus %u: SPI %u B out + %u B in took %u us (incl. waiting for SPI1)", bus_id, len,
		read_size, k_cyc_to_us_floor32(k_cycle_get_32() - start));
	if (ret < 0) {
		LOG_ERR("bus %u: SPI read failed (%d)", bus_id, ret);
		return ret;
	}
	LOG_HEXDUMP_DBG(payload, MIN(len, REMOTE_SPI_LOG_DUMP_MAX), "SPI out (first bytes)");
	LOG_HEXDUMP_DBG(read_buf, MIN(read_size, REMOTE_SPI_LOG_DUMP_MAX), "SPI in (first bytes)");
	return read_size;
}

static void remote_spi_server(void *p1, void *p2, void *p3)
{
	/* Listen on the AVTP port on any of this board's addresses */
	struct net_sockaddr_in local = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(REMOTE_SPI_PORT),
	};
	uint32_t seq_num = 0;	/* Counts sent datagrams (Annex J / NTSCF) */
	uint32_t handled = 0;	/* Requests answered */
	uint32_t failed = 0;	/* ...of which with err = 1 */
	uint32_t dropped = 0;	/* Datagrams that were not GBB Requests */
	int sock;

	log_byte_buses();

	sock = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
	if (sock < 0 || zsock_bind(sock, (struct net_sockaddr *)&local, sizeof(local)) < 0) {
		LOG_ERR("could not open UDP port %d (%d)", REMOTE_SPI_PORT, errno);
		return;
	}

	LOG_INF("remote SPI server listening on %s:%d", CONFIG_NET_CONFIG_MY_IPV4_ADDR,
		REMOTE_SPI_PORT);

	while (1) {
		struct net_sockaddr_in from;	/* Who sent the request: the answer goes back there */
		net_socklen_t from_len = sizeof(from);
		char from_ip[NET_INET_ADDRSTRLEN];
		const Avtp_Gbb_t *req;
		Avtp_Gbb_t *rsp;
		size_t frame_len;
		ssize_t n;
		int ret;

		/* Wait (forever) for the next datagram */
		n = zsock_recvfrom(sock, rx_frame, sizeof(rx_frame), 0, (struct net_sockaddr *)&from,
				   &from_len);
		if (n < 0) {
			LOG_ERR("recvfrom failed (%d)", errno);
			continue;
		}

		net_addr_ntop(NET_AF_INET, &from.sin_addr, from_ip, sizeof(from_ip));
		LOG_DBG("datagram from %s:%u, %d B", from_ip, net_ntohs(from.sin_port), (int)n);

		/* Unwrap Annex J + NTSCF; drop anything that is not a GBB Request */
		req = frame_parse(rx_frame, n);
		if (req == NULL) {
			/* frame_parse() already logged why */
			dropped++;
			continue;
		}
		frame_log_gbb("request in", req);
		if (Avtp_Gbb_IsRsp(req)) {
			LOG_WRN("dropped: a response from %s, not a request", from_ip);
			dropped++;
			continue;
		}

		/* Do the SPI transfer */
		ret = run_request(req);
		LOG_INF("request %u from %s: bus %u %s -> %s (%d)", Avtp_Gbb_GetTransactionNum(req),
			from_ip, Avtp_Gbb_GetByteBusId(req), Avtp_Gbb_IsOp(req) ? "write" : "read",
			ret < 0 ? "err" : "ok", ret);

		/*
		 * GBB Response: same bus, op and transaction number as the request,
		 * the bytes read as payload (none for a write or on error)
		 */
		rsp = frame_gbb(tx_frame);
		Avtp_Gbb_CreateAcfMessage(rsp, Avtp_Gbb_GetByteBusId(req), Avtp_Gbb_IsOp(req),
					  Avtp_Gbb_GetTransactionNum(req), read_buf,
					  ret > 0 ? ret : 0);
		Avtp_Gbb_SetRsp(rsp, true);	/* This is a response, not a request */
		Avtp_Gbb_SetErr(rsp, ret < 0);	/* Tell the sender whether it worked */
		frame_log_gbb("response out", rsp);

		/* Put the Annex J and NTSCF headers in front and send it back */
		frame_len = frame_finish(tx_frame, seq_num++);
		if (zsock_sendto(sock, tx_frame, frame_len, 0, (struct net_sockaddr *)&from,
				 from_len) < 0) {
			LOG_ERR("sending the response to %s failed (%d)", from_ip, errno);
		}

		handled++;
		failed += ret < 0 ? 1 : 0;
		if (handled % STATS_EVERY == 0) {
			LOG_INF("stats: %u requests answered, %u with err, %u datagrams dropped",
				handled, failed, dropped);
		}
	}
}

/* Start the server as its own thread at boot (3 KiB stack for logging, priority 7, below main) */
K_THREAD_DEFINE(remote_spi_tid, 3072, remote_spi_server, NULL, NULL, NULL, 7, 0, 0);

/* ---------- air_quality example, plus log messages ---------- */

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(bme68x));
	struct sensor_value temp, humidity, pressure, gas;
	char line[20];

	if (!device_is_ready(display)) {
		LOG_ERR("the display is not ready");
		return 0;
	}

	if (!device_is_ready(sensor)) {
		/* Usually wiring: the bme680 driver could not read the chip ID at boot */
		LOG_ERR("the sensor is not ready");
		return 0;
	}

	if (cfb_framebuffer_init(display) != 0) {
		LOG_ERR("could not prepare the drawing area");
		return 0;
	}

	cfb_framebuffer_set_font(display, 0);
	LOG_INF("air quality loop running, redraw every 2 s");

	while (1) {
		/* Take a new measurement, then read each value from it */
		if (sensor_sample_fetch(sensor) != 0) {
			LOG_ERR("could not read the sensor");
			k_sleep(K_SECONDS(2));
			continue;
		}

		sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(sensor, SENSOR_CHAN_HUMIDITY, &humidity);
		sensor_channel_get(sensor, SENSOR_CHAN_PRESS, &pressure);
		sensor_channel_get(sensor, SENSOR_CHAN_GAS_RES, &gas);

		cfb_framebuffer_clear(display, false);
		cfb_draw_text(display, "Air Quality", 17, 20);

		/* Each line goes to the screen and, as text, to the log */

		/* Temperature in degrees Celsius */
		snprintf(line, sizeof(line), "T: %.2f C", sensor_value_to_double(&temp));
		cfb_draw_text(display, line, 2, 52);
		LOG_INF("%s", line);

		/* Relative humidity in percent */
		snprintf(line, sizeof(line), "H: %.2f %%", sensor_value_to_double(&humidity));
		cfb_draw_text(display, line, 2, 76);
		LOG_INF("%s", line);

		/* Air pressure in kilopascals: about 101 kPa at sea level */
		snprintf(line, sizeof(line), "P: %.2f kPa", sensor_value_to_double(&pressure));
		cfb_draw_text(display, line, 2, 100);
		LOG_INF("%s", line);

		/* Gas resistance in ohms: higher means cleaner air */
		snprintf(line, sizeof(line), "G: %d Ohm", gas.val1);
		cfb_draw_text(display, line, 2, 124);
		LOG_INF("%s", line);

		/* Redraws the whole screen, so it also wipes the sender's bar until it paints it again */
		if (cfb_framebuffer_finalize(display) != 0) {
			LOG_ERR("could not update the screen");
		}
		LOG_DBG("screen redrawn (sender's bar wiped)");

		/* Update the screen every 2 seconds */
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
