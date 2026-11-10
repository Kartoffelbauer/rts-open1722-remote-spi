#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <avtp/Udp.h>
#include <avtp/acf/Ntscf.h>

#include "remote_spi.h"

/* Log messages from this file appear as "remote_spi: ..." */
LOG_MODULE_REGISTER(remote_spi, CONFIG_REMOTE_SPI_LOG_LEVEL);

/* Any fixed number works: it names the "stream" both boards talk on */
#define STREAM_ID 0xAABBCCDDEEFF0001ULL

/* Byte offsets inside the frame buffer we send */
#define NTSCF_OFFSET AVTP_UDP_HEADER_LEN			/* NTSCF follows Annex J */
#define GBB_OFFSET   (NTSCF_OFFSET + AVTP_NTSCF_HEADER_LEN_V0)	/* GBB follows NTSCF */

Avtp_Gbb_t *frame_gbb(uint8_t *frame)
{
	/* The caller writes the GBB message here, the headers in front come later */
	return (Avtp_Gbb_t *)(frame + GBB_OFFSET);
}

size_t frame_finish(uint8_t *frame, uint32_t seq_num)
{
	Avtp_Udp_t *udp = (Avtp_Udp_t *)frame;
	Avtp_Ntscf_t *ntscf = (Avtp_Ntscf_t *)(frame + NTSCF_OFFSET);
	/* GBB length in bytes incl. padding, as set by Avtp_Gbb_SetPayloadLength() */
	uint16_t gbb_len = Avtp_AcfCommon_GetAcfMsgLengthInBytes((Avtp_AcfCommon_t *)frame_gbb(frame));

	/* Annex J header: a counter so the other side could spot lost datagrams */
	Avtp_Udp_SetEncapsulationSeqNo(udp, seq_num);

	/* NTSCF header: Init only clears its own 12 bytes, so the GBB behind it stays intact */
	Avtp_Ntscf_InitV0(ntscf);
	Avtp_Ntscf_SetStreamId(ntscf, STREAM_ID);
	Avtp_Ntscf_SetSequenceNum(ntscf, seq_num);
	/* Tell the receiver how many bytes of ACF messages (here: one GBB) follow */
	Avtp_Ntscf_SetNtscfDataLength(ntscf, gbb_len);

	LOG_DBG("frame out: seq %u, NTSCF data %u B, datagram %u B", seq_num, gbb_len,
		GBB_OFFSET + gbb_len);

	return GBB_OFFSET + gbb_len;
}

const Avtp_Gbb_t *frame_parse(const uint8_t *frame, size_t len)
{
	const Avtp_Ntscf_t *ntscf = (const Avtp_Ntscf_t *)(frame + NTSCF_OFFSET);
	const Avtp_Gbb_t *gbb;
	uint16_t ntscf_len;

	/* Too short to hold the Annex J and NTSCF headers */
	if (len < AVTP_UDP_HEADER_LEN + AVTP_NTSCF_HEADER_LEN_V0) {
		LOG_WRN("frame in: only %zu B, too short for the headers", len);
		LOG_HEXDUMP_WRN(frame, len, "datagram");
		return NULL;
	}

	ntscf_len = Avtp_Ntscf_GetNtscfDataLength(ntscf);

	/* Checks subtype (NTSCF), version and that the NTSCF data fits into the datagram */
	if (!Avtp_Ntscf_IsValid(ntscf, len - AVTP_UDP_HEADER_LEN)) {
		/* NTSCF subtype is 0x82; a length larger than the datagram means it was cut off */
		LOG_WRN("frame in: not a valid NTSCF frame (subtype 0x%02x, NTSCF data %u B, "
			"datagram %zu B)",
			Avtp_CommonHeader_GetSubtype((const Avtp_CommonHeader_t *)ntscf), ntscf_len,
			len);
		LOG_HEXDUMP_WRN(frame, MIN(len, REMOTE_SPI_LOG_DUMP_MAX), "datagram");
		return NULL;
	}

	/* The ACF message behind the NTSCF header must be a valid GBB message */
	gbb = (const Avtp_Gbb_t *)Avtp_Ntscf_GetPayload(ntscf);
	if (!Avtp_Gbb_IsValid(gbb, ntscf_len)) {
		/* GBB is ACF type 0x0d; its length must fit into the NTSCF data */
		LOG_WRN("frame in: NTSCF data is not a valid GBB message (ACF type 0x%02x, "
			"ACF length %u B, NTSCF data %u B)",
			ntscf_len >= AVTP_ACF_COMMON_HEADER_LEN ?
				Avtp_AcfCommon_GetAcfMsgType((const Avtp_AcfCommon_t *)gbb) : 0,
			ntscf_len >= AVTP_ACF_COMMON_HEADER_LEN ?
				Avtp_AcfCommon_GetAcfMsgLengthInBytes((const Avtp_AcfCommon_t *)gbb) : 0,
			ntscf_len);
		LOG_HEXDUMP_WRN(frame, MIN(len, REMOTE_SPI_LOG_DUMP_MAX), "datagram");
		return NULL;
	}

	LOG_DBG("frame in: seq %u, NTSCF seq %u, NTSCF data %u B, datagram %zu B",
		Avtp_Udp_GetEncapsulationSeqNo((const Avtp_Udp_t *)frame),
		Avtp_Ntscf_GetSequenceNum(ntscf), ntscf_len, len);

	return gbb;
}

void frame_log_gbb(const char *what, const Avtp_Gbb_t *gbb)
{
	bool rsp = Avtp_Gbb_IsRsp(gbb);
	bool write = Avtp_Gbb_IsOp(gbb);
	uint16_t len = Avtp_Gbb_GetPayloadLength(gbb);

	/*
	 * The 12-bit field after "ms" is read_size in Read Requests and
	 * segment_num in everything else (Write Requests and all responses)
	 */
	LOG_DBG("%s: bus %u, %s %s, transaction %u, err %d, ms %d, %s %u, payload %u B, pad %u",
		what, Avtp_Gbb_GetByteBusId(gbb), write ? "write" : "read",
		rsp ? "response" : "request", Avtp_Gbb_GetTransactionNum(gbb),
		Avtp_Gbb_IsErr(gbb), Avtp_Gbb_IsMs(gbb),
		(!write && !rsp) ? "read_size" : "segment_num", Avtp_Gbb_GetReadSize(gbb), len,
		Avtp_Gbb_GetPad(gbb));

	if (len > 0) {
		LOG_HEXDUMP_DBG(Avtp_Gbb_GetPayload(gbb), MIN(len, REMOTE_SPI_LOG_DUMP_MAX),
				"payload (first bytes)");
	}
}
