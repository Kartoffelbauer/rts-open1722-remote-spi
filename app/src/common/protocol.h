#pragma once

#include <zephyr/toolchain.h>
#include <avtp/acf/Gbb.h>

/* IANA port for AVTP over UDP (IEEE 1722-2025 Annex J) */
#define PROTOCOL_UDP_PORT 17220

/* Open1722 truncates received GBB payload lengths to 8 bits, see docs/open1722-notes.md */
#define PROTOCOL_MAX_PAYLOAD 255

/* Byte Buses on the Responder, see docs/architecture.md */
enum byte_bus_id {
	BYTE_BUS_BME280 = 1,
	BYTE_BUS_OLED_COMMAND = 2,
	BYTE_BUS_OLED_DATA = 3,
};

BUILD_ASSERT(AVTP_GBB_HEADER_LEN == 16, "GBB header must be 16 bytes (IEEE 1722-2025 9.4.14)");
