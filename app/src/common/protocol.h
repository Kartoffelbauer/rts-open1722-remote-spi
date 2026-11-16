#pragma once

#include <zephyr/toolchain.h>
#include <avtp/acf/Gbb.h>

/* IANA port for AVTP over UDP (IEEE 1722-2025 Annex J) */
#define PROTOCOL_UDP_PORT 17220

/* How long the Requester waits for a response before the transaction fails */
#define PROTOCOL_TIMEOUT_MS 100

/* Byte Buses on the Responder, see docs/architecture.md */
enum byte_bus_id {
	BYTE_BUS_SENSOR = 1,	/* BME68x */
	BYTE_BUS_DISPLAY = 2,	/* SHARP Memory Display */
};

BUILD_ASSERT(AVTP_GBB_HEADER_LEN == 16, "GBB header must be 16 bytes (IEEE 1722-2025 9.4.14)");
