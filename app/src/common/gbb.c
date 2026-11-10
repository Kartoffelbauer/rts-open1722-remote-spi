#include <errno.h>
#include <zephyr/toolchain.h>
#include <avtp/Udp.h>
#include <avtp/acf/Ntscf.h>
#include <avtp/acf/Gbb.h>

#include "gbb.h"

BUILD_ASSERT(GBB_HEADERS_LEN == AVTP_UDP_HEADER_LEN + AVTP_NTSCF_HEADER_LEN_V0 + AVTP_GBB_HEADER_LEN,
	     "GBB_HEADERS_LEN must match the Open1722 header sizes");

/*
 * Stubs: the tests in app/tests/common are written first and fail against these.
 * The real implementation follows in task 4a.
 */

int gbb_build_write_request(struct gbb_stream *stream, uint16_t byte_bus_id, const uint8_t *data,
			    size_t len, uint8_t *buf, size_t buf_size, struct gbb_msg *req)
{
	return -ENOSYS;
}

int gbb_build_read_request(struct gbb_stream *stream, uint16_t byte_bus_id, const uint8_t *data,
			   size_t len, uint16_t read_size, uint8_t *buf, size_t buf_size,
			   struct gbb_msg *req)
{
	return -ENOSYS;
}

int gbb_build_response(struct gbb_stream *stream, const struct gbb_msg *req, bool err,
		       const uint8_t *data, size_t len, uint8_t *buf, size_t buf_size)
{
	return -ENOSYS;
}

int gbb_parse(const uint8_t *buf, size_t len, struct gbb_msg *msg)
{
	return -ENOSYS;
}

size_t gbb_read_size_bytes(uint16_t read_size)
{
	return 0;
}

int gbb_check_request(const struct gbb_msg *req, size_t max_read)
{
	return -ENOSYS;
}

enum gbb_match gbb_match_response(const struct gbb_msg *req, const struct gbb_msg *rsp)
{
	return GBB_MATCH_NONE;
}
