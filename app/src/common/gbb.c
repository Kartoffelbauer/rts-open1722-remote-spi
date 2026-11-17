#include <errno.h>
#include <string.h>
#include <zephyr/toolchain.h>
#include <avtp/CommonHeader.h>
#include <avtp/Udp.h>
#include <avtp/acf/AcfCommon.h>
#include <avtp/acf/Ntscf.h>
#include <avtp/acf/Gbb.h>

#include "gbb.h"

BUILD_ASSERT(GBB_HEADERS_LEN == AVTP_UDP_HEADER_LEN + AVTP_NTSCF_HEADER_LEN_V0 + AVTP_GBB_HEADER_LEN,
	     "GBB_HEADERS_LEN must match the Open1722 header sizes");

/* Byte offsets of the headers inside a datagram */
#define NTSCF_OFFSET AVTP_UDP_HEADER_LEN
#define GBB_OFFSET   (AVTP_UDP_HEADER_LEN + AVTP_NTSCF_HEADER_LEN_V0)

/* Largest values of the 11-bit byte_bus_id and the 12-bit read_size/segment_num */
#define BYTE_BUS_ID_MAX 0x7FF
#define READ_SIZE_MAX   0xFFF

/* read_size bit 11: the low 11 bits count KiB instead of bytes (9.4.14.14) */
#define READ_SIZE_KIB 0x800

/* Datagram length for a payload of len bytes, or -EMSGSIZE / -ENOSPC */
static int datagram_len(size_t len, size_t buf_size)
{
	if (len > GBB_MAX_PAYLOAD) {
		return -EMSGSIZE;
	}

	if (buf_size < GBB_DATAGRAM_LEN(len)) {
		return -ENOSPC;
	}

	return (int)GBB_DATAGRAM_LEN(len);
}

/*
 * Writes msg as one datagram into buf, which must hold GBB_DATAGRAM_LEN(msg->payload_len)
 * bytes, and increments the stream's sequence numbers.
 */
static void write_datagram(struct gbb_stream *stream, const struct gbb_msg *msg, uint8_t *buf)
{
	Avtp_Udp_t *udp = (Avtp_Udp_t *)buf;
	Avtp_NtscfV0_t *ntscf = (Avtp_NtscfV0_t *)(buf + NTSCF_OFFSET);
	Avtp_Gbb_t *gbb = (Avtp_Gbb_t *)(buf + GBB_OFFSET);
	size_t gbb_len = GBB_DATAGRAM_LEN(msg->payload_len) - GBB_OFFSET;

	Avtp_Udp_Init(udp);
	Avtp_Udp_SetEncapsulationSeqNo(udp, stream->encapsulation_seq_num);

	/* Init sets sv=1, but the stream has no reservation (9.2.2) */
	Avtp_Ntscf_InitV0(ntscf);
	Avtp_Ntscf_SetSv_V0(ntscf, false);
	Avtp_Ntscf_SetNtscfDataLength_V0(ntscf, (uint16_t)gbb_len);
	Avtp_Ntscf_SetSequenceNum_V0(ntscf, stream->sequence_num);
	Avtp_Ntscf_SetStreamId_V0(ntscf, stream->stream_id);

	Avtp_Gbb_Init(gbb);
	Avtp_Gbb_SetByteBusId(gbb, msg->byte_bus_id);
	Avtp_Gbb_SetTransactionNum(gbb, msg->transaction_num);
	Avtp_Gbb_SetOp(gbb, msg->op == GBB_OP_WRITE);
	Avtp_Gbb_SetRsp(gbb, msg->rsp);
	Avtp_Gbb_SetErr(gbb, msg->err);
	Avtp_Gbb_SetMs(gbb, msg->ms);
	Avtp_Gbb_SetReadSize(gbb, msg->read_size_segment_num);

	if (msg->payload_len > 0) {
		memcpy(buf + GBB_HEADERS_LEN, msg->payload, msg->payload_len);
	}
	/* Sets acf_msg_length and pad, and zeroes the pad bytes */
	Avtp_Gbb_SetPayloadLength(gbb, (uint16_t)msg->payload_len);

	stream->encapsulation_seq_num++;
	stream->sequence_num++;
}

/* Shared part of the two request builders */
static int build_request(struct gbb_stream *stream, uint16_t byte_bus_id, enum gbb_op op,
			 uint16_t read_size, const uint8_t *data, size_t len, uint8_t *buf,
			 size_t buf_size, struct gbb_msg *req)
{
	if (byte_bus_id > BYTE_BUS_ID_MAX || read_size > READ_SIZE_MAX) {
		return -EINVAL;
	}

	int ret = datagram_len(len, buf_size);

	if (ret < 0) {
		return ret;
	}

	struct gbb_msg msg = {
		.byte_bus_id = byte_bus_id,
		.op = op,
		.transaction_num = stream->transaction_num,
		.read_size_segment_num = read_size,
		.payload = data,
		.payload_len = len,
	};

	write_datagram(stream, &msg, buf);
	stream->transaction_num++;

	if (req != NULL) {
		*req = msg;
		/* Like gbb_parse(): the payload as it is in the datagram */
		req->payload = buf + GBB_HEADERS_LEN;
	}

	return ret;
}

int gbb_build_write_request(struct gbb_stream *stream, uint16_t byte_bus_id, const uint8_t *data,
			    size_t len, uint8_t *buf, size_t buf_size, struct gbb_msg *req)
{
	if (len == 0) {
		return -EINVAL;
	}

	return build_request(stream, byte_bus_id, GBB_OP_WRITE, 0, data, len, buf, buf_size, req);
}

int gbb_build_read_request(struct gbb_stream *stream, uint16_t byte_bus_id, const uint8_t *data,
			   size_t len, uint16_t read_size, uint8_t *buf, size_t buf_size,
			   struct gbb_msg *req)
{
	return build_request(stream, byte_bus_id, GBB_OP_READ, read_size, data, len, buf, buf_size,
			     req);
}

int gbb_build_response(struct gbb_stream *stream, const struct gbb_msg *req, bool err,
		       const uint8_t *data, size_t len, uint8_t *buf, size_t buf_size)
{
	if (err) {
		data = NULL;
		len = 0;
	}

	int ret = datagram_len(len, buf_size);

	if (ret < 0) {
		return ret;
	}

	const struct gbb_msg msg = {
		.byte_bus_id = req->byte_bus_id,
		.op = req->op,
		.rsp = true,
		.err = err,
		.transaction_num = req->transaction_num,
		.payload = data,
		.payload_len = len,
	};

	write_datagram(stream, &msg, buf);

	return ret;
}

int gbb_parse(const uint8_t *buf, size_t len, struct gbb_msg *msg)
{
	if (len < GBB_HEADERS_LEN) {
		return -EBADMSG;
	}

	const Avtp_NtscfV0_t *ntscf = (const Avtp_NtscfV0_t *)(buf + NTSCF_OFFSET);
	const Avtp_Gbb_t *gbb = (const Avtp_Gbb_t *)(buf + GBB_OFFSET);
	size_t gbb_len = len - GBB_OFFSET;

	if (Avtp_CommonHeader_GetSubtype((const Avtp_CommonHeader_t *)ntscf) != AVTP_SUBTYPE_NTSCF ||
	    Avtp_CommonHeader_GetVersion((const Avtp_CommonHeader_t *)ntscf) != AVTPDU_VERSION_0) {
		return -EBADMSG;
	}

	/* The NTSCF payload is the rest of the datagram, and exactly one GBB message */
	if (Avtp_Ntscf_GetNtscfDataLength_V0(ntscf) != gbb_len ||
	    !Avtp_Gbb_IsValid(gbb, gbb_len) ||
	    Avtp_AcfCommon_GetAcfMsgLengthInBytes((const Avtp_AcfCommon_t *)gbb) != gbb_len) {
		return -EBADMSG;
	}

	*msg = (struct gbb_msg){
		.byte_bus_id = Avtp_Gbb_GetByteBusId(gbb),
		.op = Avtp_Gbb_IsOp(gbb) ? GBB_OP_WRITE : GBB_OP_READ,
		.rsp = Avtp_Gbb_IsRsp(gbb),
		.err = Avtp_Gbb_IsErr(gbb),
		.ms = Avtp_Gbb_IsMs(gbb),
		.transaction_num = Avtp_Gbb_GetTransactionNum(gbb),
		.read_size_segment_num = Avtp_Gbb_GetReadSize(gbb),
		.payload = Avtp_Gbb_GetPayload(gbb),
		.payload_len = Avtp_Gbb_GetPayloadLength(gbb),
	};

	return 0;
}

size_t gbb_read_size_bytes(uint16_t read_size)
{
	if (read_size & READ_SIZE_KIB) {
		size_t kib = read_size & (READ_SIZE_KIB - 1);

		return (kib == 0 ? 1 : kib) * 1024;
	}

	return read_size;
}

int gbb_check_request(const struct gbb_msg *req, size_t max_read)
{
	if (req->ms || (req->op == GBB_OP_WRITE && req->read_size_segment_num != 0)) {
		return -ENOTSUP;
	}

	if (req->op == GBB_OP_WRITE) {
		return req->payload_len == 0 ? -ENODATA : 0;
	}

	size_t read_len = gbb_read_size_bytes(req->read_size_segment_num);

	if (read_len == 0) {
		return -EINVAL;
	}

	if (read_len > max_read) {
		return -EMSGSIZE;
	}

	return 0;
}

enum gbb_match gbb_match_response(const struct gbb_msg *req, const struct gbb_msg *rsp)
{
	if (!rsp->rsp || rsp->transaction_num != req->transaction_num ||
	    rsp->byte_bus_id != req->byte_bus_id || rsp->op != req->op) {
		return GBB_MATCH_NONE;
	}

	if (rsp->err) {
		return GBB_MATCH_FAILED;
	}

	if (req->op == GBB_OP_READ &&
	    rsp->payload_len != gbb_read_size_bytes(req->read_size_segment_num)) {
		return GBB_MATCH_FAILED;
	}

	return GBB_MATCH_OK;
}
