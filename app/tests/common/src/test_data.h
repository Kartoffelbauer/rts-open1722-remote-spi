#pragma once

/*
 * Reference datagrams, written byte by byte from IEEE 1722-2025 (Figure 58,
 * Figure 84, Annex J) instead of with Open1722, so the tests also catch layout
 * mistakes the library would make on both sides.
 */

#include <stdint.h>

#include "gbb.h"

/* Byte offsets inside a datagram */
#define OFF_NTSCF   4
#define OFF_GBB     16
#define OFF_PAYLOAD 32

#define REQUESTER_STREAM_ID 0x0011223344550001ULL
#define RESPONDER_STREAM_ID 0x00AABBCCDDEE0002ULL

#define REQUESTER_STREAM(encap_seq, seq, tn)                                                       \
	((struct gbb_stream){.stream_id = REQUESTER_STREAM_ID,                                     \
			     .encapsulation_seq_num = (encap_seq),                                 \
			     .sequence_num = (seq),                                                \
			     .transaction_num = (tn)})

#define RESPONDER_STREAM(encap_seq, seq)                                                           \
	((struct gbb_stream){.stream_id = RESPONDER_STREAM_ID,                                     \
			     .encapsulation_seq_num = (encap_seq),                                 \
			     .sequence_num = (seq)})

/*
 * Write Request: REQUESTER_STREAM(0x01020304, 0x05, 0x10), Byte Bus 2,
 * payload AA BB CC (pad 1)
 */
extern const uint8_t golden_write_request[36];

/*
 * Read Request: REQUESTER_STREAM(0x01020305, 0x06, 0x11), Byte Bus 1,
 * payload D0 (pad 3), read_size 1
 */
extern const uint8_t golden_read_request[36];

/* Read Response to golden_read_request: RESPONDER_STREAM(0x00000007, 0x09), payload 61 */
extern const uint8_t golden_read_response[36];

/* Error response to golden_write_request: RESPONDER_STREAM(0x00000008, 0x0A), no payload */
extern const uint8_t golden_error_response[32];

/* Fills buf with a recognisable pattern */
void fill_pattern(uint8_t *buf, size_t len);
