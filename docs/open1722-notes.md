# Open1722 Notes

Findings about [Open1722](https://github.com/COVESA/Open1722) that affect this project. Checked against `main` at `906a0de` (2026-11-05).

## GBB support in Open1722

- GBB lives entirely in the header [`include/avtp/acf/Gbb.h`](https://github.com/COVESA/Open1722/blob/main/include/avtp/acf/Gbb.h). It has a unit test (`unit/test-gbb.c`) but **no example app**.
- The abbreviated version of GBB is called **ABB** (Abbreviated Byte Bus) (§9.4.15). It can be found in [`include/avtp/acf/Abb.h`](https://github.com/COVESA/Open1722/blob/main/include/avtp/acf/Abb.h). Both boards only parse the full format, which is fine because each one only receives messages from the other.
- Open1722 is **not a Zephyr module**. Its only Zephyr app (`examples/acf-can/zephyr`) is built from inside the Open1722 repo.

## Fixed: `Avtp_Gbb_GetPayloadLength()` truncated to 8 bits

Up to `v0.9.7`, the getter returned `uint8_t`, so a received payload of 256 bytes or more came back as `length % 256`. Our upstream PR changed the return type to `uint16_t` and added a test with a payload over 255 bytes. It is merged as [`906a0de`](https://github.com/COVESA/Open1722/commit/906a0de94490c3f45d5c6e897996ecbc477a0d91), and payloads up to the GBB maximum of 2028 bytes are read correctly.

## Spec facts we rely on (IEEE 1722-2025)

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

**SPI read (Annex O → Annex M, Figures M.7 and M.8):** a GBB read request carries the bytes to clock out first (instruction / register address) in its payload, and `read_size` gives the number of bytes to read afterwards, all in one bus transaction. The read response contains only the bytes read. The "don't care" bytes on either line aren't transferred.
