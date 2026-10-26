# Open1722 Notes

Findings about [Open1722](https://github.com/COVESA/Open1722) that affect this project. Checked against `main` at `b78c387` (2026-10-26).

## GBB support in Open1722

- GBB lives entirely in the header [`include/avtp/acf/Gbb.h`](https://github.com/COVESA/Open1722/blob/main/include/avtp/acf/Gbb.h). It has a unit test (`unit/test-gbb.c`) but **no example app**.
- There is **no `ACF_BYTE_BUS_BRIEF` header** (§9.4.15). The spec (Annex M, NOTE 2) says Listeners have to accept both the full and the brief format. Our Responder only parses the full format, which is fine as long as our Requester is the only Talker.
- Open1722 is **not a Zephyr module**. Its only Zephyr app (`examples/acf-can/zephyr`) is built from inside the Open1722 repo.

## Bug: `Avtp_Gbb_GetPayloadLength()` truncates to 8 bits

```c
OPEN1722_INLINE uint8_t Avtp_Gbb_GetPayloadLength(const Avtp_Gbb_t *const pdu)
{
    ...
    return (uint8_t)(acf_length_bytes - AVTP_GBB_HEADER_LEN - pad_length);
}
```

`acf_msg_length` is 9 bits of quadlets, so a GBB payload can be up to about 2028 bytes, but the getter returns `uint8_t`. A received payload of 256 bytes or more comes back as `length % 256`. The setter (`Avtp_Gbb_SetPayloadLength`) already takes `uint16_t`, so only the receive side is affected.

**Workaround in this project:** every GBB payload stays at or below 255 bytes. OLED updates are sent as one GBB Request per 128-byte SSD1306 RAM page.

**Upstream fix (PR to COVESA wanted):** change the return type to `uint16_t` and add a test with a payload over 255 bytes in `unit/test-gbb.c`.

## Spec facts we rely on (IEEE 1722-2025)

The spec PDF is in [1722-2025.pdf](1722-2025.pdf).

| Field | Meaning (§9.4.14) |
|---|---|
| `op` | 1 = write, 0 = read |
| `rsp` | 1 = response, 0 = request |
| `err` | Set in a response if the request failed; always 0 in a request |
| `transaction_num` | Set by the Requester, incremented per request (wrapping); echoed in the response |
| `read_size` / `segment_num` | `read_size` in read requests (bit 11 = KiB units); `segment_num` in write requests and read responses |
| `ms` | More segments: only for messages that don't fit in one AVTPDU |
| `cs` | **Conditional start** for chains of GBB messages in one AVTPDU (not chip select) |
| `hs` | Hold stop: I2C only, always 0 for SPI (Annex O) |

**SPI read (Annex O → Annex M, Figure M.7):** a GBB read request carries the bytes to clock out first (instruction / register address) in its payload, and `read_size` gives the number of bytes to read afterwards, all in one bus transaction. The read response contains only the bytes read. The "don't care" bytes on either line aren't transferred.
