/*
 * Shared by the sender and the receiver: addresses, Byte Bus numbers and two
 * helpers that wrap a GBB message into a UDP datagram and unwrap it again.
 *
 * One datagram looks like this (all headers are built by Open1722):
 *
 *   | UDP header | Annex J (4 B) | NTSCF (12 B) | GBB header (16 B) | payload | pad |
 *     ^ Zephyr     ^ seq. number   ^ stream frame ^ one GBB message
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <avtp/acf/Gbb.h>

/* UDP port for IEEE 1722 over UDP (the IANA "AVTP" port, Annex J) */
#define REMOTE_SPI_PORT 17220

/* Static IP of the receiver (the board with the SPI devices) */
#define RECEIVER_IP "192.168.1.11"

/* How long the sender waits for an answer before it gives up */
#define REMOTE_SPI_TIMEOUT_MS 200

/*
 * Open1722 returns received payload lengths as 8 bits (see docs/open1722-notes.md),
 * so both sides keep every GBB payload below 256 bytes
 */
#define REMOTE_SPI_MAX_PAYLOAD 255

/* Big enough for all headers plus the largest payload and its padding */
#define REMOTE_SPI_FRAME_SIZE 320

/* Byte Bus IDs: which SPI device on the receiver a GBB message is for */
enum byte_bus_id {
	BYTE_BUS_SENSOR = 1,	/* BME68x, chip select D9 */
	BYTE_BUS_DISPLAY = 2,	/* SHARP memory display, chip select D10 */
};

/* Where the GBB message starts inside a frame buffer (behind Annex J and NTSCF) */
Avtp_Gbb_t *frame_gbb(uint8_t *frame);

/*
 * Call after the GBB message in frame is complete: fills in the Annex J and
 * NTSCF headers around it. Returns the number of bytes to send.
 */
size_t frame_finish(uint8_t *frame, uint32_t seq_num);

/*
 * Checks a received datagram of len bytes. Returns the GBB message inside it,
 * or NULL if the datagram is not a valid NTSCF frame with one GBB message.
 */
const Avtp_Gbb_t *frame_parse(const uint8_t *frame, size_t len);

/* Hex dumps in the log show at most this many bytes, so the serial console keeps up */
#define REMOTE_SPI_LOG_DUMP_MAX 64

/* Logs every GBB header field and the first payload bytes (debug level); what = "request out" etc. */
void frame_log_gbb(const char *what, const Avtp_Gbb_t *gbb);
