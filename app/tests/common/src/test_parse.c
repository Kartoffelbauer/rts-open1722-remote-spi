#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "gbb.h"
#include "test_data.h"

static uint8_t big_buf[GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD)];
static uint8_t big_payload[GBB_MAX_PAYLOAD];

/* Copies a reference datagram into buf, so a test can change single bytes */
#define COPY(buf, golden) memcpy(buf, golden, sizeof(golden))

/* Sets ntscf_data_length (11 bits) */
static void set_ntscf_data_length(uint8_t *buf, uint16_t len)
{
	buf[OFF_NTSCF + 1] = (buf[OFF_NTSCF + 1] & ~0x07) | (len >> 8);
	buf[OFF_NTSCF + 2] = len & 0xFF;
}

/* Sets acf_msg_length (9 bits, in quadlets) */
static void set_acf_msg_length(uint8_t *buf, uint16_t quadlets)
{
	buf[OFF_GBB] = (buf[OFF_GBB] & ~0x01) | (quadlets >> 8);
	buf[OFF_GBB + 1] = quadlets & 0xFF;
}

ZTEST_SUITE(gbb_parse, NULL, NULL, NULL, NULL, NULL);

ZTEST(gbb_parse, test_parses_write_request)
{
	struct gbb_msg msg;
	static const uint8_t payload[] = {0xAA, 0xBB, 0xCC};

	zassert_ok(gbb_parse(golden_write_request, sizeof(golden_write_request), &msg));

	zassert_equal(msg.byte_bus_id, 2);
	zassert_equal(msg.op, GBB_OP_WRITE);
	zassert_false(msg.rsp);
	zassert_false(msg.err);
	zassert_false(msg.ms);
	zassert_equal(msg.transaction_num, 0x10);
	zassert_equal(msg.read_size_segment_num, 0);
	zassert_equal(msg.payload_len, sizeof(payload));
	zassert_equal_ptr(msg.payload, &golden_write_request[OFF_PAYLOAD], "payload is not copied");
	zassert_mem_equal(msg.payload, payload, sizeof(payload));
}

ZTEST(gbb_parse, test_parses_read_request)
{
	struct gbb_msg msg;

	zassert_ok(gbb_parse(golden_read_request, sizeof(golden_read_request), &msg));

	zassert_equal(msg.byte_bus_id, 1);
	zassert_equal(msg.op, GBB_OP_READ);
	zassert_false(msg.rsp);
	zassert_equal(msg.transaction_num, 0x11);
	zassert_equal(msg.read_size_segment_num, 1);
	zassert_equal(msg.payload_len, 1);
	zassert_equal(msg.payload[0], 0xD0);
}

ZTEST(gbb_parse, test_parses_read_response)
{
	struct gbb_msg msg;

	zassert_ok(gbb_parse(golden_read_response, sizeof(golden_read_response), &msg));

	zassert_equal(msg.byte_bus_id, 1);
	zassert_equal(msg.op, GBB_OP_READ);
	zassert_true(msg.rsp);
	zassert_false(msg.err);
	zassert_equal(msg.transaction_num, 0x11);
	zassert_equal(msg.read_size_segment_num, 0);
	zassert_equal(msg.payload_len, 1);
	zassert_equal(msg.payload[0], 0x61);
}

ZTEST(gbb_parse, test_parses_error_response)
{
	struct gbb_msg msg;

	zassert_ok(gbb_parse(golden_error_response, sizeof(golden_error_response), &msg));

	zassert_equal(msg.byte_bus_id, 2);
	zassert_equal(msg.op, GBB_OP_WRITE);
	zassert_true(msg.rsp);
	zassert_true(msg.err);
	zassert_equal(msg.transaction_num, 0x10);
	zassert_equal(msg.payload_len, 0);
}

ZTEST(gbb_parse, test_reads_ms_flag)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	COPY(buf, golden_write_request);
	buf[OFF_GBB + 14] |= 0x10;

	zassert_ok(gbb_parse(buf, sizeof(buf), &msg), "ms is refused by gbb_check_request()");
	zassert_true(msg.ms);
}

ZTEST(gbb_parse, test_round_trip_small_payloads)
{
	uint8_t data[64];
	uint8_t buf[GBB_DATAGRAM_LEN(sizeof(data))];

	fill_pattern(data, sizeof(data));

	for (size_t n = 1; n <= sizeof(data); n++) {
		struct gbb_stream stream = REQUESTER_STREAM(0, 0, (uint8_t)n);
		struct gbb_msg msg;
		int len = gbb_build_write_request(&stream, 3, data, n, buf, sizeof(buf), NULL);

		zassert_true(len > 0, "payload %zu", n);
		zassert_ok(gbb_parse(buf, len, &msg), "payload %zu", n);
		zassert_equal(msg.byte_bus_id, 3, "payload %zu", n);
		zassert_equal(msg.transaction_num, (uint8_t)n, "payload %zu", n);
		zassert_equal(msg.payload_len, n, "payload %zu", n);
		zassert_mem_equal(msg.payload, data, n, "payload %zu", n);
	}
}

/*
 * Fails until the Open1722 fix for Avtp_Gbb_GetPayloadLength() is merged and the
 * submodule is updated: the getter truncates payload lengths to 8 bits, see
 * docs/open1722-notes.md.
 */
ZTEST(gbb_parse, test_round_trip_payloads_over_255_bytes)
{
	static const size_t sizes[] = {256, 300, GBB_MAX_PAYLOAD};

	fill_pattern(big_payload, sizeof(big_payload));

	for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
		struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);
		struct gbb_msg msg;
		int len = gbb_build_write_request(&stream, 3, big_payload, sizes[i], big_buf,
						  sizeof(big_buf), NULL);

		zassert_true(len > 0, "payload %zu", sizes[i]);
		zassert_ok(gbb_parse(big_buf, len, &msg), "payload %zu", sizes[i]);
		zassert_equal(msg.payload_len, sizes[i], "payload %zu", sizes[i]);
		zassert_mem_equal(msg.payload, big_payload, sizes[i], "payload %zu", sizes[i]);
	}
}

ZTEST(gbb_parse, test_ignores_stream_header_details)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	COPY(buf, golden_write_request);
	/* Other encapsulation_seq_num, sv=1, other sequence_num_lsb and stream_id */
	memset(buf, 0xEE, 4);
	buf[OFF_NTSCF + 1] |= 0x80;
	buf[OFF_NTSCF + 3] = 0x99;
	memset(&buf[OFF_NTSCF + 4], 0x77, 8);

	zassert_ok(gbb_parse(buf, sizeof(buf), &msg));
	zassert_equal(msg.byte_bus_id, 2);
	zassert_equal(msg.transaction_num, 0x10);
	zassert_equal(msg.payload_len, 3);
}

ZTEST(gbb_parse, test_rejects_truncated_datagrams)
{
	struct gbb_msg msg;

	for (size_t n = 0; n < sizeof(golden_write_request); n++) {
		zassert_equal(gbb_parse(golden_write_request, n, &msg), -EBADMSG, "length %zu", n);
	}
}

ZTEST(gbb_parse, test_rejects_trailing_bytes)
{
	uint8_t buf[sizeof(golden_write_request) + 4] = {0};
	struct gbb_msg msg;

	COPY(buf, golden_write_request);

	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_other_avtp_subtypes)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	COPY(buf, golden_write_request);
	/* TSCF */
	buf[OFF_NTSCF] = 0x05;

	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_ntscf_version_1)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	COPY(buf, golden_write_request);
	buf[OFF_NTSCF + 1] |= 0x10;

	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_ntscf_data_length_mismatch)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	/* Claims more data than the datagram holds */
	COPY(buf, golden_write_request);
	set_ntscf_data_length(buf, 24);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);

	/* Leaves bytes of the datagram outside the NTSCF payload */
	COPY(buf, golden_write_request);
	set_ntscf_data_length(buf, 16);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_other_acf_message_types)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	/* ABB (ACF_BYTE_BUS_BRIEF, 0x0E) */
	COPY(buf, golden_write_request);
	buf[OFF_GBB] = (0x0E << 1) | (buf[OFF_GBB] & 0x01);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);

	/* CAN (0x01) */
	COPY(buf, golden_write_request);
	buf[OFF_GBB] = (0x01 << 1) | (buf[OFF_GBB] & 0x01);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_second_acf_message)
{
	uint8_t buf[sizeof(golden_write_request) + 4];
	struct gbb_msg msg;

	COPY(buf, golden_write_request);
	/* A one-quadlet CAN message behind the GBB message, inside the NTSCF payload */
	buf[sizeof(golden_write_request) + 0] = 0x01 << 1;
	buf[sizeof(golden_write_request) + 1] = 0x01;
	buf[sizeof(golden_write_request) + 2] = 0x00;
	buf[sizeof(golden_write_request) + 3] = 0x00;
	set_ntscf_data_length(buf, 24);

	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_acf_msg_length_mismatch)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	/* Longer than the NTSCF payload */
	COPY(buf, golden_write_request);
	set_acf_msg_length(buf, 6);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);

	/* Shorter than the NTSCF payload */
	COPY(buf, golden_write_request);
	set_acf_msg_length(buf, 4);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);

	/* Shorter than the GBB header */
	COPY(buf, golden_write_request);
	set_acf_msg_length(buf, 3);
	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}

ZTEST(gbb_parse, test_rejects_pad_longer_than_payload)
{
	uint8_t buf[sizeof(golden_error_response)];
	struct gbb_msg msg;

	/* No payload at all, but pad=3 */
	COPY(buf, golden_error_response);
	buf[OFF_GBB + 2] |= 0xC0;

	zassert_equal(gbb_parse(buf, sizeof(buf), &msg), -EBADMSG);
}
