#pragma once

#include <zephyr/toolchain.h>

#include "gbb.h"

/*
 * UDP port of the Responder: the AVTP port for discrete encapsulation (IEEE
 * 1722-2025 J.5). The NTSCF stream has no reservation (sv=0), so it is a
 * best-effort transport, which uses discrete encapsulation (Table 7).
 */
#define PROTOCOL_UDP_PORT 17221

/* How long the Requester waits for a response before the transaction fails */
#define PROTOCOL_TIMEOUT_MS 100

/* unique_id in the low 16 bits of each board's stream_id (IEEE 1722-2025 4.6) */
#define PROTOCOL_STREAM_UNIQUE_ID 1

/*
 * Largest datagram on the link. CONFIG_NET_IPV4_FRAGMENT isn't enabled, so
 * Zephyr doesn't fragment IPv4 packets and a datagram has to fit in one
 * Ethernet frame: MTU 1500 - IPv4 header 20 - UDP header 8 = 1472 bytes.
 */
#define PROTOCOL_MAX_DATAGRAM_LEN 1472

/* Largest GBB payload on the link: 1472 - GBB_HEADERS_LEN (32) = 1440 bytes */
#define PROTOCOL_MAX_PAYLOAD (PROTOCOL_MAX_DATAGRAM_LEN - GBB_HEADERS_LEN)

BUILD_ASSERT(GBB_DATAGRAM_LEN(PROTOCOL_MAX_PAYLOAD) == PROTOCOL_MAX_DATAGRAM_LEN,
	     "the largest payload on the link must fill a datagram without padding");

/* Byte Buses on the Responder, see docs/architecture.md */
enum byte_bus_id {
	BYTE_BUS_SENSOR = 1,	/* BME68x */
	BYTE_BUS_DISPLAY = 2,	/* SHARP Memory Display */
};
