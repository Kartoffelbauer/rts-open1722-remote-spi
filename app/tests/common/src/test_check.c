#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "gbb.h"
#include "test_data.h"

/* Responder buffer size used by most tests */
#define MAX_READ 2048

static const uint8_t some_bytes[] = {0x01, 0x02, 0x03};

static struct gbb_msg write_request(size_t len)
{
	return (struct gbb_msg){
		.byte_bus_id = 2,
		.op = GBB_OP_WRITE,
		.payload = some_bytes,
		.payload_len = len,
	};
}

static struct gbb_msg read_request(uint16_t read_size)
{
	return (struct gbb_msg){
		.byte_bus_id = 1,
		.op = GBB_OP_READ,
		.read_size_segment_num = read_size,
		.payload = some_bytes,
		.payload_len = 1,
	};
}

/* Decoding of the raw read_size field (9.4.14.14) */
ZTEST_SUITE(gbb_read_size, NULL, NULL, NULL, NULL, NULL);

ZTEST(gbb_read_size, test_byte_units)
{
	zassert_equal(gbb_read_size_bytes(0x000), 0);
	zassert_equal(gbb_read_size_bytes(0x001), 1);
	zassert_equal(gbb_read_size_bytes(0x7FF), 2047);
}

ZTEST(gbb_read_size, test_kib_units)
{
	/* 0 KiB also means 1 KiB, only without segmentation */
	zassert_equal(gbb_read_size_bytes(0x800), 1024);
	zassert_equal(gbb_read_size_bytes(0x801), 1024);
	zassert_equal(gbb_read_size_bytes(0x802), 2048);
	zassert_equal(gbb_read_size_bytes(0xFFF), 2047 * 1024);
}

/* Which parsed requests the Responder can execute, and why not */
ZTEST_SUITE(gbb_check_request, NULL, NULL, NULL, NULL, NULL);

ZTEST(gbb_check_request, test_accepts_valid_requests)
{
	struct gbb_msg write = write_request(sizeof(some_bytes));
	struct gbb_msg read = read_request(4);
	struct gbb_msg read_without_payload = read_request(4);

	read_without_payload.payload_len = 0;

	zassert_ok(gbb_check_request(&write, MAX_READ));
	zassert_ok(gbb_check_request(&read, MAX_READ));
	zassert_ok(gbb_check_request(&read_without_payload, MAX_READ));
}

ZTEST(gbb_check_request, test_refuses_empty_write_request)
{
	struct gbb_msg write = write_request(0);

	zassert_equal(gbb_check_request(&write, MAX_READ), -ENODATA);
}

ZTEST(gbb_check_request, test_refuses_segmented_requests)
{
	struct gbb_msg write_ms = write_request(sizeof(some_bytes));
	struct gbb_msg read_ms = read_request(4);
	struct gbb_msg write_segment = write_request(sizeof(some_bytes));

	write_ms.ms = true;
	read_ms.ms = true;
	write_segment.read_size_segment_num = 1;

	zassert_equal(gbb_check_request(&write_ms, MAX_READ), -ENOTSUP);
	zassert_equal(gbb_check_request(&read_ms, MAX_READ), -ENOTSUP);
	zassert_equal(gbb_check_request(&write_segment, MAX_READ), -ENOTSUP);
}

ZTEST(gbb_check_request, test_refuses_zero_read_size)
{
	struct gbb_msg read = read_request(0);

	zassert_equal(gbb_check_request(&read, MAX_READ), -EINVAL);
}

ZTEST(gbb_check_request, test_refuses_reads_larger_than_buffer)
{
	struct gbb_msg fits = read_request(16);
	struct gbb_msg too_big = read_request(17);

	zassert_ok(gbb_check_request(&fits, 16));
	zassert_equal(gbb_check_request(&too_big, 16), -EMSGSIZE);
}

ZTEST(gbb_check_request, test_decodes_kib_read_sizes)
{
	struct gbb_msg one_kib = read_request(0x800);
	struct gbb_msg two_kib = read_request(0x802);
	struct gbb_msg three_kib = read_request(0x803);

	zassert_ok(gbb_check_request(&one_kib, MAX_READ));
	zassert_ok(gbb_check_request(&two_kib, MAX_READ));
	zassert_equal(gbb_check_request(&three_kib, MAX_READ), -EMSGSIZE);
}

ZTEST(gbb_check_request, test_ignores_fields_meant_for_the_target_bus)
{
	uint8_t buf[sizeof(golden_write_request)];
	struct gbb_msg msg;

	memcpy(buf, golden_write_request, sizeof(buf));
	/* mtv=1 with a timestamp, evt=0xF, hs=1, cs=1 */
	buf[OFF_GBB + 2] |= 0x20;
	memset(&buf[OFF_GBB + 4], 0x5A, 8);
	buf[OFF_GBB + 12] = 0xF3;

	zassert_ok(gbb_parse(buf, sizeof(buf), &msg));
	zassert_ok(gbb_check_request(&msg, MAX_READ));
}
