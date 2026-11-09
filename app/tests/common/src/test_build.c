#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "gbb.h"
#include "test_data.h"

static uint8_t big_buf[GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD) + 4];
static uint8_t big_payload[GBB_MAX_PAYLOAD + 1];

/* Layout of the datagrams, checked against the reference bytes from the spec */
ZTEST_SUITE(gbb_build, NULL, NULL, NULL, NULL, NULL);

ZTEST(gbb_build, test_write_request_matches_spec_layout)
{
	struct gbb_stream stream = REQUESTER_STREAM(0x01020304, 0x05, 0x10);
	static const uint8_t data[] = {0xAA, 0xBB, 0xCC};
	uint8_t buf[64];

	int len = gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf), NULL);

	zassert_equal(len, (int)sizeof(golden_write_request));
	zassert_mem_equal(buf, golden_write_request, sizeof(golden_write_request));
}

ZTEST(gbb_build, test_read_request_matches_spec_layout)
{
	struct gbb_stream stream = REQUESTER_STREAM(0x01020305, 0x06, 0x11);
	static const uint8_t reg[] = {0xD0};
	uint8_t buf[64];

	int len = gbb_build_read_request(&stream, 1, reg, sizeof(reg), 1, buf, sizeof(buf), NULL);

	zassert_equal(len, (int)sizeof(golden_read_request));
	zassert_mem_equal(buf, golden_read_request, sizeof(golden_read_request));
}

ZTEST(gbb_build, test_read_response_matches_spec_layout)
{
	struct gbb_stream stream = RESPONDER_STREAM(0x00000007, 0x09);
	const struct gbb_msg req = {
		.byte_bus_id = 1,
		.op = GBB_OP_READ,
		.transaction_num = 0x11,
		.read_size_segment_num = 1,
	};
	static const uint8_t data[] = {0x61};
	uint8_t buf[64];

	int len = gbb_build_response(&stream, &req, false, data, sizeof(data), buf, sizeof(buf));

	zassert_equal(len, (int)sizeof(golden_read_response));
	zassert_mem_equal(buf, golden_read_response, sizeof(golden_read_response));
}

ZTEST(gbb_build, test_error_response_has_no_payload)
{
	struct gbb_stream stream = RESPONDER_STREAM(0x00000008, 0x0A);
	const struct gbb_msg req = {
		.byte_bus_id = 2,
		.op = GBB_OP_WRITE,
		.transaction_num = 0x10,
	};
	/* Must be ignored because err is set */
	static const uint8_t data[] = {0x11, 0x22};
	uint8_t buf[64];

	int len = gbb_build_response(&stream, &req, true, data, sizeof(data), buf, sizeof(buf));

	zassert_equal(len, (int)sizeof(golden_error_response));
	zassert_mem_equal(buf, golden_error_response, sizeof(golden_error_response));
}

ZTEST(gbb_build, test_request_reports_what_was_sent)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0x42);
	static const uint8_t reg[] = {0xD0, 0x00};
	uint8_t buf[64];
	struct gbb_msg req;

	int len = gbb_build_read_request(&stream, 1, reg, sizeof(reg), 6, buf, sizeof(buf), &req);

	zassert_true(len > 0);
	zassert_equal(req.byte_bus_id, 1);
	zassert_equal(req.op, GBB_OP_READ);
	zassert_false(req.rsp);
	zassert_false(req.err);
	zassert_false(req.ms);
	zassert_equal(req.transaction_num, 0x42);
	zassert_equal(req.read_size_segment_num, 6);
	zassert_equal(req.payload_len, sizeof(reg));
	zassert_mem_equal(req.payload, reg, sizeof(reg));
}

ZTEST(gbb_build, test_payload_is_padded_to_a_quadlet)
{
	uint8_t data[8];
	uint8_t buf[64];

	fill_pattern(data, sizeof(data));

	for (size_t n = 1; n <= sizeof(data); n++) {
		struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);
		size_t pad = (4 - n % 4) % 4;

		memset(buf, 0xFF, sizeof(buf));
		int len = gbb_build_write_request(&stream, 2, data, n, buf, sizeof(buf), NULL);

		zassert_equal(len, (int)GBB_DATAGRAM_LEN(n), "payload %zu", n);
		/* pad field: top two bits of GBB byte 2 */
		zassert_equal(buf[OFF_GBB + 2] >> 6, pad, "payload %zu", n);
		/* acf_msg_length in quadlets: low bit of byte 0 and byte 1 */
		zassert_equal(((buf[OFF_GBB] & 0x01) << 8 | buf[OFF_GBB + 1]) * 4,
			      len - OFF_GBB, "payload %zu", n);
		/* ntscf_data_length covers the whole GBB message */
		zassert_equal((buf[OFF_NTSCF + 1] & 0x07) << 8 | buf[OFF_NTSCF + 2], len - OFF_GBB,
			      "payload %zu", n);
		zassert_mem_equal(&buf[OFF_PAYLOAD], data, n, "payload %zu", n);
		for (size_t i = 0; i < pad; i++) {
			zassert_equal(buf[OFF_PAYLOAD + n + i], 0, "pad byte %zu of payload %zu", i, n);
		}
	}
}

ZTEST(gbb_build, test_read_request_may_have_empty_payload)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);
	uint8_t buf[64];

	int len = gbb_build_read_request(&stream, 1, NULL, 0, 4, buf, sizeof(buf), NULL);

	zassert_equal(len, GBB_HEADERS_LEN);
}

ZTEST(gbb_build, test_largest_payload_fits)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);

	fill_pattern(big_payload, GBB_MAX_PAYLOAD);
	int len = gbb_build_write_request(&stream, 2, big_payload, GBB_MAX_PAYLOAD, big_buf,
					  sizeof(big_buf), NULL);

	zassert_equal(len, (int)GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD));
	zassert_mem_equal(&big_buf[OFF_PAYLOAD], big_payload, GBB_MAX_PAYLOAD);
}

ZTEST(gbb_build, test_rejects_empty_write_request)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);
	uint8_t buf[64];

	zassert_equal(gbb_build_write_request(&stream, 2, NULL, 0, buf, sizeof(buf), NULL),
		      -EINVAL);
}

ZTEST(gbb_build, test_rejects_payload_over_maximum)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);
	const struct gbb_msg req = {.byte_bus_id = 1, .op = GBB_OP_READ};

	zassert_equal(gbb_build_write_request(&stream, 2, big_payload, GBB_MAX_PAYLOAD + 1,
					      big_buf, sizeof(big_buf), NULL),
		      -EMSGSIZE);
	zassert_equal(gbb_build_read_request(&stream, 1, big_payload, GBB_MAX_PAYLOAD + 1, 1,
					     big_buf, sizeof(big_buf), NULL),
		      -EMSGSIZE);
	zassert_equal(gbb_build_response(&stream, &req, false, big_payload, GBB_MAX_PAYLOAD + 1,
					 big_buf, sizeof(big_buf)),
		      -EMSGSIZE);
}

ZTEST(gbb_build, test_rejects_fields_that_do_not_fit)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0);
	static const uint8_t data[] = {0x01};
	uint8_t buf[64];

	/* byte_bus_id has 11 bits */
	zassert_equal(gbb_build_write_request(&stream, 0x800, data, sizeof(data), buf, sizeof(buf),
					      NULL),
		      -EINVAL);
	zassert_equal(gbb_build_read_request(&stream, 0x800, data, sizeof(data), 1, buf,
					     sizeof(buf), NULL),
		      -EINVAL);
	/* read_size has 12 bits */
	zassert_equal(gbb_build_read_request(&stream, 1, data, sizeof(data), 0x1000, buf,
					     sizeof(buf), NULL),
		      -EINVAL);
	/* The highest values that fit are fine */
	zassert_true(gbb_build_write_request(&stream, 0x7FF, data, sizeof(data), buf, sizeof(buf),
					     NULL) > 0);
	zassert_true(gbb_build_read_request(&stream, 1, data, sizeof(data), 0xFFF, buf,
					    sizeof(buf), NULL) > 0);
}

ZTEST(gbb_build, test_rejects_too_small_buffer_without_side_effects)
{
	struct gbb_stream stream = REQUESTER_STREAM(0x01020304, 0x05, 0x10);
	const struct gbb_stream before = stream;
	const struct gbb_msg req = {.byte_bus_id = 1, .op = GBB_OP_READ};
	static const uint8_t data[] = {0xAA, 0xBB, 0xCC};
	uint8_t buf[64];
	size_t needed = GBB_DATAGRAM_LEN(sizeof(data));

	zassert_equal(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, needed - 1,
					      NULL),
		      -ENOSPC);
	zassert_equal(gbb_build_read_request(&stream, 1, data, sizeof(data), 1, buf, needed - 1,
					     NULL),
		      -ENOSPC);
	zassert_equal(gbb_build_response(&stream, &req, false, data, sizeof(data), buf,
					 needed - 1),
		      -ENOSPC);
	zassert_mem_equal(&stream, &before, sizeof(stream), "stream changed on failure");

	/* Exactly the needed size is enough */
	zassert_equal(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, needed, NULL),
		      (int)needed);
}

/* Sequence numbers and transaction_num kept in struct gbb_stream */
ZTEST_SUITE(gbb_stream, NULL, NULL, NULL, NULL, NULL);

ZTEST(gbb_stream, test_transaction_num_increments_per_request)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 7);
	static const uint8_t data[] = {0x01};
	uint8_t buf[64];
	struct gbb_msg first;
	struct gbb_msg second;

	zassert_true(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf),
					     &first) > 0);
	zassert_equal(buf[OFF_GBB + 13], 7);
	zassert_true(gbb_build_read_request(&stream, 1, data, sizeof(data), 1, buf, sizeof(buf),
					    &second) > 0);
	zassert_equal(buf[OFF_GBB + 13], 8);

	zassert_equal(first.transaction_num, 7);
	zassert_equal(second.transaction_num, 8);
	zassert_equal(stream.transaction_num, 9);
}

ZTEST(gbb_stream, test_transaction_num_wraps)
{
	struct gbb_stream stream = REQUESTER_STREAM(0, 0, 0xFF);
	static const uint8_t data[] = {0x01};
	uint8_t buf[64];
	struct gbb_msg req;

	zassert_true(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf),
					     &req) > 0);
	zassert_equal(req.transaction_num, 0xFF);
	zassert_true(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf),
					     &req) > 0);
	zassert_equal(req.transaction_num, 0x00);
}

ZTEST(gbb_stream, test_responses_keep_transaction_num)
{
	struct gbb_stream stream = RESPONDER_STREAM(0, 0);
	const struct gbb_msg req = {.byte_bus_id = 2, .op = GBB_OP_WRITE, .transaction_num = 0x33};
	uint8_t buf[64];

	stream.transaction_num = 0x55;
	zassert_true(gbb_build_response(&stream, &req, false, NULL, 0, buf, sizeof(buf)) > 0);

	zassert_equal(buf[OFF_GBB + 13], 0x33, "response must echo the request's number");
	zassert_equal(stream.transaction_num, 0x55);
}

ZTEST(gbb_stream, test_sequence_numbers_increment_per_datagram)
{
	struct gbb_stream stream = REQUESTER_STREAM(0x01020304, 0x05, 0);
	const struct gbb_msg req = {.byte_bus_id = 2, .op = GBB_OP_WRITE};
	static const uint8_t data[] = {0x01};
	static const uint8_t encap_second[] = {0x01, 0x02, 0x03, 0x05};
	static const uint8_t encap_third[] = {0x01, 0x02, 0x03, 0x06};
	uint8_t buf[64];

	zassert_true(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf),
					     NULL) > 0);
	zassert_true(gbb_build_read_request(&stream, 1, data, sizeof(data), 1, buf, sizeof(buf),
					    NULL) > 0);
	zassert_mem_equal(buf, encap_second, sizeof(encap_second));
	zassert_equal(buf[OFF_NTSCF + 3], 0x06);

	/* Responses count as datagrams too */
	zassert_true(gbb_build_response(&stream, &req, false, NULL, 0, buf, sizeof(buf)) > 0);
	zassert_mem_equal(buf, encap_third, sizeof(encap_third));
	zassert_equal(buf[OFF_NTSCF + 3], 0x07);

	zassert_equal(stream.encapsulation_seq_num, 0x01020307);
	zassert_equal(stream.sequence_num, 0x08);
}

ZTEST(gbb_stream, test_sequence_numbers_wrap)
{
	struct gbb_stream stream = REQUESTER_STREAM(0xFFFFFFFF, 0xFF, 0);
	static const uint8_t data[] = {0x01};
	uint8_t buf[64];

	zassert_true(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf),
					     NULL) > 0);
	zassert_true(gbb_build_write_request(&stream, 2, data, sizeof(data), buf, sizeof(buf),
					     NULL) > 0);

	zassert_equal(buf[0] | buf[1] | buf[2] | buf[3], 0, "encapsulation_seq_num must wrap to 0");
	zassert_equal(buf[OFF_NTSCF + 3], 0, "sequence_num_lsb must wrap to 0");
}
