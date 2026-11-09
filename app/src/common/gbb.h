#pragma once

/*
 * GBB framing and parsing (IEEE 1722-2025 9.4.14 and Annex J).
 *
 * One UDP datagram carries exactly one GBB message:
 *
 *   Annex J header (4) | NTSCF v0 header (12) | GBB header (16) | payload | pad (0-3)
 *
 * The functions are pure (no sockets, no globals), so they run on native_sim.
 * Build functions write into a buffer from the caller, gbb_parse() points into
 * the received buffer instead of copying.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bytes in front of the GBB payload: Annex J (4) + NTSCF v0 (12) + GBB header (16) */
#define GBB_HEADERS_LEN 32

/* Largest GBB payload: acf_msg_length is 9 bits of quadlets (2044 bytes) minus the GBB header */
#define GBB_MAX_PAYLOAD 2028

/* Datagram length for a payload of len bytes, padded to a whole quadlet */
#define GBB_DATAGRAM_LEN(len) (GBB_HEADERS_LEN + (((size_t)(len) + 3) & ~(size_t)3))

enum gbb_op {
	GBB_OP_READ = 0,
	GBB_OP_WRITE = 1,
};

/*
 * Sender state for one direction of the link. The build functions put these
 * values into each datagram and then increment them.
 */
struct gbb_stream {
	/* NTSCF stream_id: the sender's MAC address followed by a 16-bit ID (4.6) */
	uint64_t stream_id;
	/* Annex J encapsulation_sequence_num, +1 per datagram (J.3.2) */
	uint32_t encapsulation_seq_num;
	/* NTSCF sequence_num_lsb, +1 per datagram (9.2.4) */
	uint8_t sequence_num;
	/* GBB transaction_num of the next request, +1 per request only (9.4.14.9) */
	uint8_t transaction_num;
};

/* One GBB message, as sent or as received */
struct gbb_msg {
	uint16_t byte_bus_id;
	enum gbb_op op;
	bool rsp;
	bool err;
	/* More segments (9.4.14.13): never sent by us, requests with it set are refused */
	bool ms;
	uint8_t transaction_num;
	/* read_size in Read Requests (see gbb_read_size_bytes()), segment_num otherwise */
	uint16_t read_size_segment_num;
	/* Points into the datagram buffer, not copied */
	const uint8_t *payload;
	size_t payload_len;
};

/* Result of gbb_match_response() */
enum gbb_match {
	/* Not the response to this request (stale or unrelated): keep waiting */
	GBB_MATCH_NONE,
	/* The response to this request, and the transfer succeeded */
	GBB_MATCH_OK,
	/* The response to this request, but a Failed Transaction (err=1 or wrong read length) */
	GBB_MATCH_FAILED,
};

/*
 * Builds a Write Request with the stream's next transaction_num. If req isn't
 * NULL, it receives the message as sent, for gbb_match_response().
 *
 * Returns the datagram length, or
 *   -EINVAL   len is 0 or byte_bus_id doesn't fit in 11 bits
 *   -EMSGSIZE len is over GBB_MAX_PAYLOAD
 *   -ENOSPC   buf_size is too small
 * The stream is only changed on success.
 */
int gbb_build_write_request(struct gbb_stream *stream, uint16_t byte_bus_id, const uint8_t *data,
			    size_t len, uint8_t *buf, size_t buf_size, struct gbb_msg *req);

/*
 * Builds a Read Request: data (e.g. a register address, may be empty) is clocked
 * out, then read_size bytes are read. read_size is the raw 12-bit field, so plain
 * byte counts go up to 2047 (9.4.14.14).
 *
 * Returns like gbb_build_write_request(), with -EINVAL also for a read_size that
 * doesn't fit in 12 bits. An empty data is allowed.
 */
int gbb_build_read_request(struct gbb_stream *stream, uint16_t byte_bus_id, const uint8_t *data,
			   size_t len, uint16_t read_size, uint8_t *buf, size_t buf_size,
			   struct gbb_msg *req);

/*
 * Builds the GBB Response to req: transaction_num, byte_bus_id and op are copied
 * from req, rsp=1, ms=0, segment_num=0. With err set, data is ignored and the
 * response has no payload. stream->transaction_num isn't changed.
 *
 * Returns the datagram length, -EMSGSIZE or -ENOSPC (see gbb_build_write_request()).
 */
int gbb_build_response(struct gbb_stream *stream, const struct gbb_msg *req, bool err,
		       const uint8_t *data, size_t len, uint8_t *buf, size_t buf_size);

/*
 * Parses one datagram. Returns 0, or -EBADMSG unless it is exactly one GBB
 * message in an NTSCF version 0 AVTPDU behind an Annex J header, with no bytes
 * left over. Doesn't check rsp, the sequence numbers, sv or the stream_id.
 */
int gbb_parse(const uint8_t *buf, size_t len, struct gbb_msg *msg);

/*
 * Decodes a raw read_size field into bytes (9.4.14.14): bit 11 set means units
 * of KiB, where 0x800 also means 1 KiB. A raw 0 decodes to 0 bytes.
 */
size_t gbb_read_size_bytes(uint16_t read_size);

/*
 * Checks whether the Responder can execute a parsed request (rsp=0). The Byte
 * Bus ID isn't checked here. Returns 0, or the reason for an err=1 response:
 *   -ENOTSUP  segmented: ms=1, or segment_num != 0 in a Write Request
 *   -ENODATA  Write Request without payload
 *   -EINVAL   read size of 0 bytes
 *   -EMSGSIZE read size over max_read
 */
int gbb_check_request(const struct gbb_msg *req, size_t max_read);

/*
 * Decides whether rsp answers req: it must have rsp=1 and the same
 * transaction_num, byte_bus_id and op. A matching response fails if err=1 or if
 * a read response doesn't carry exactly the requested number of bytes.
 */
enum gbb_match gbb_match_response(const struct gbb_msg *req, const struct gbb_msg *rsp);
