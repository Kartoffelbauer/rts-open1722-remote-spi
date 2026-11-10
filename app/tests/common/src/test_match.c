#include <zephyr/ztest.h>

#include "gbb.h"
#include "test_data.h"

static const uint8_t four_bytes[] = {0x11, 0x22, 0x33, 0x44};

/* The request the Requester sent: read 4 bytes on Byte Bus 1 */
static const struct gbb_msg read_req = {
	.byte_bus_id = 1,
	.op = GBB_OP_READ,
	.transaction_num = 0x20,
	.read_size_segment_num = 4,
};

static const struct gbb_msg write_req = {
	.byte_bus_id = 2,
	.op = GBB_OP_WRITE,
	.transaction_num = 0x21,
	.payload = four_bytes,
	.payload_len = sizeof(four_bytes),
};

/* A successful response to req carrying payload_len bytes */
static struct gbb_msg response_to(const struct gbb_msg *req, size_t payload_len)
{
	return (struct gbb_msg){
		.byte_bus_id = req->byte_bus_id,
		.op = req->op,
		.rsp = true,
		.transaction_num = req->transaction_num,
		.payload = four_bytes,
		.payload_len = payload_len,
	};
}

ZTEST_SUITE(gbb_match, NULL, NULL, NULL, NULL, NULL);

ZTEST(gbb_match, test_read_response_with_requested_length_is_ok)
{
	struct gbb_msg rsp = response_to(&read_req, 4);

	zassert_equal(gbb_match_response(&read_req, &rsp), GBB_MATCH_OK);
}

ZTEST(gbb_match, test_write_response_is_ok)
{
	struct gbb_msg rsp = response_to(&write_req, 0);

	zassert_equal(gbb_match_response(&write_req, &rsp), GBB_MATCH_OK);
}

ZTEST(gbb_match, test_stale_transaction_num_is_ignored)
{
	struct gbb_msg rsp = response_to(&read_req, 4);

	rsp.transaction_num = read_req.transaction_num - 1;

	zassert_equal(gbb_match_response(&read_req, &rsp), GBB_MATCH_NONE);
}

ZTEST(gbb_match, test_other_byte_bus_is_ignored)
{
	struct gbb_msg rsp = response_to(&read_req, 4);

	rsp.byte_bus_id = 2;

	zassert_equal(gbb_match_response(&read_req, &rsp), GBB_MATCH_NONE);
}

ZTEST(gbb_match, test_other_op_is_ignored)
{
	struct gbb_msg rsp = response_to(&read_req, 4);

	rsp.op = GBB_OP_WRITE;

	zassert_equal(gbb_match_response(&read_req, &rsp), GBB_MATCH_NONE);
}

ZTEST(gbb_match, test_request_is_not_a_response)
{
	struct gbb_msg rsp = response_to(&read_req, 4);

	rsp.rsp = false;

	zassert_equal(gbb_match_response(&read_req, &rsp), GBB_MATCH_NONE);
}

ZTEST(gbb_match, test_error_response_fails)
{
	struct gbb_msg read_rsp = response_to(&read_req, 0);
	struct gbb_msg write_rsp = response_to(&write_req, 0);

	read_rsp.err = true;
	write_rsp.err = true;

	zassert_equal(gbb_match_response(&read_req, &read_rsp), GBB_MATCH_FAILED);
	zassert_equal(gbb_match_response(&write_req, &write_rsp), GBB_MATCH_FAILED);
}

ZTEST(gbb_match, test_wrong_read_length_fails)
{
	struct gbb_msg too_short = response_to(&read_req, 3);
	struct gbb_msg empty = response_to(&read_req, 0);
	struct gbb_msg too_long = response_to(&read_req, 4);

	/* four_bytes has only 4 bytes, but the length alone decides */
	too_long.payload_len = 5;

	zassert_equal(gbb_match_response(&read_req, &too_short), GBB_MATCH_FAILED);
	zassert_equal(gbb_match_response(&read_req, &empty), GBB_MATCH_FAILED);
	zassert_equal(gbb_match_response(&read_req, &too_long), GBB_MATCH_FAILED);
}

/* One full transaction through all functions, as Requester and Responder will run it */
ZTEST(gbb_match, test_read_transaction_end_to_end)
{
	struct gbb_stream requester = REQUESTER_STREAM(0, 0, 0x30);
	struct gbb_stream responder = RESPONDER_STREAM(0, 0);
	static const uint8_t reg[] = {0xD0};
	static const uint8_t chip_id[] = {0x61};
	uint8_t req_buf[64];
	uint8_t rsp_buf[64];
	struct gbb_msg sent;
	struct gbb_msg received;
	struct gbb_msg answer;

	/* Requester */
	int len = gbb_build_read_request(&requester, 1, reg, sizeof(reg), sizeof(chip_id), req_buf,
					 sizeof(req_buf), &sent);
	zassert_true(len > 0);

	/* Responder */
	zassert_ok(gbb_parse(req_buf, len, &received));
	zassert_false(received.rsp);
	zassert_ok(gbb_check_request(&received, 64));
	len = gbb_build_response(&responder, &received, false, chip_id, sizeof(chip_id), rsp_buf,
				 sizeof(rsp_buf));
	zassert_true(len > 0);

	/* Requester */
	zassert_ok(gbb_parse(rsp_buf, len, &answer));
	zassert_equal(gbb_match_response(&sent, &answer), GBB_MATCH_OK);
	zassert_equal(answer.payload_len, sizeof(chip_id));
	zassert_equal(answer.payload[0], 0x61);
}
