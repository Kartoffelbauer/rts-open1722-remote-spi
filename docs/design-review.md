# Design Review: What the Specs Require

This review checks every design decision and every file in `app/`, `examples/`, `docs/` and `.devcontainer/` for whether it is required. It reflects branch `Task-4` with the decisions of 2026-11-24: [ADR 0004](adr/0004-requester-runs-zephyr-drivers-on-a-gbb-spi-controller.md), port 17221 and the 1440-byte link limit. Each item is checked against:

- **IEEE 1722-2025** (`docs/1722-2025.pdf`): §4.6, §9.2, §9.4.1, §9.4.14, Annex J (normative) and Annexes L, M, O (informative)
- **The project goal**: the Requester uses the remote Peripherals as if they were wired to it, and the Responder only forwards SPI transfers ([README](../README.md), [ADR 0001](adr/0001-responder-is-a-dumb-byte-bus.md), ADR 0004), plus the tasks in the [Project Plan](project-plan.md)
- **The hardware**: the Zephyr v4.4.0 board file for `nucleo_f439zi`, Zephyr's `bme680` and `ls0xx` drivers at v4.4.0, and the peripherals as described in [Architecture](architecture.md)

## Verdicts

| Verdict | Meaning |
|---|---|
| **Spec** | A "shall" in a normative clause of IEEE 1722-2025 |
| **Spec (rec.)** | Recommended by an informative annex (L, M or O) |
| **HW** | Forced by the boards, the peripherals or their Zephyr drivers |
| **Project** | Needed for the project goal or the demo |
| **Optional** | Not needed by any of the above: convenience, robustness or polish |
| **Open** | Conflicts with the spec or the design, and isn't fixed yet |

## Open items

1. **Responder source port (Annex J.4), task 5.** J.4 requires the source port of an IP AVTPDU to be an ephemeral port (RFC 6056) or an IANA-registered application-specific port, and its NOTE says it is never 17220 or 17221. The Responder therefore sends its responses from a second socket bound to port 0. The Requester's socket isn't connected, so it accepts a response from any source port. The Annex J sequence number counts per address and port pair (J.3.2), and that pair stays fixed for the response socket.
2. **Test payload on the Display Bus, task 5.** The Requester sends a 9-byte test Write Request to the Display Bus every second (task 4). Once the Responder runs SPI transfers, these bytes reach the display as a real transfer.
3. **GBB SPI Controller, task 6.** ADR 0004 rests on the Zephyr v4.4.0 driver sources, not on a running build. The parts to prove first are the held-CS collection for `ls0xx`, deferred init with `device_init()`, and the blocks of at most 71 lines between the character framebuffer and `ls0xx`. If the approach doesn't work out, the fallback is hand-written BME68x and SHARP drivers on the Requester that build GBB Requests directly.
4. **Examples wire MOSI to PA7.** The examples are only tests and stay as they are. The wiring note in [Architecture](architecture.md#wiring-responder) covers the switch to PB5.

## Required, though it looks optional

- The PB5 MOSI override on the Responder, and the disabled SPI1 on the Requester: PA7 is also the Ethernet RMII CRS_DV pin.
- `sv`=0, the MAC-based `stream_id` and both sequence numbers.
- The Open1722 `GetPayloadLength()` fix: display blocks are longer than 255 bytes.
- Collecting a held CS window into one Write Request: `ls0xx` sends one display update as many SPI calls in one CS window.

## 1. Transport and framing

| Decision | Where | Verdict | Note |
|---|---|---|---|
| IEEE 1722 over UDP/IPv4 (Annex J), not raw Ethernet (EtherType 0x22F0) | architecture.md, `prj.conf` | Project | The spec allows both. UDP gives plain sockets on both boards and lets a PC take part (see [Debugging](debugging.md)). |
| Static IPs, IPv4 only, no TCP, no IPv6 | `prj.conf`, `requester.conf`, `responder.conf` | Project | A direct cable has no DHCP server. |
| Annex J header, `encapsulation_sequence_num` +1 per datagram | `gbb.c` | Spec (J.3.2) | |
| NTSCF, not TSCF | `gbb.c` | Project | The demo has no presentation-time requirement, so the non-time-synchronous format fits. |
| NTSCF version 0 | `gbb.c` | Optional | Version 1 adds a 32-bit `sequence_num` and the gPTP grandmaster ID. Both are allowed, and version 0 is the smaller one. |
| `sv`=0 | `gbb.c` | Spec (§9.2.2) | There is no stream reservation. Open1722's `Avtp_Ntscf_InitV0()` sets `sv`=1, so the override is needed. |
| `stream_id` = own MAC + `unique_id` 1 | `transport_udp.c`, `protocol.h` | Spec (§9.2.5, §4.6) | The StreamID format is mandatory, and using the Talker's MAC is the usual choice (§4.6 NOTE). Each direction is its own stream (Annex L). The MACs differ, so both boards can use `unique_id` 1. |
| `sequence_num_lsb` +1 per datagram | `gbb.c` | Spec (§9.2.4) | |
| Receiver ignores both sequence numbers and the `stream_id` | `gbb_parse()` | Spec allows it | Listeners may use them and must tolerate any start value (J.3.2, §9.2.4). |
| `mtv`=0, no `message_timestamp` | `gbb.c` | Optional | Allowed. No time base is needed. |
| Exactly one GBB message per datagram; a second ACF message, ABB or trailing bytes are dropped | `gbb_parse()` | Project, deviates from Spec (rec.) | Annex M NOTE 2 says Listeners need to accept both GBB and ABB, and the spec's own read response example (Figure M.8) is ABB. Both boards only parse GBB, which is fine because each one only receives messages from the other ([Open1722 Notes](open1722-notes.md)). |
| Requester sends from an ephemeral port | `requester/main.c` | Spec (J.4) | |
| Responder listens on UDP 17221 | `protocol.h` | Spec (J.5) | Table 7 allows continuous or discrete encapsulation for NTSCF, and its NOTE makes discrete NTSCF the best-effort transport. Without a reservation (`sv`=0) the stream is best effort, and J.5 gives discrete encapsulation port 17221. |
| Responder sends responses from a second socket on an ephemeral port | architecture.md (task 5) | Spec (J.4), Open | See [open item 1](#open-items). |
| A datagram is at most 1472 bytes, a GBB payload at most 1440 (`PROTOCOL_MAX_PAYLOAD`) | `protocol.h` | Project | `CONFIG_NET_IPV4_FRAGMENT` isn't enabled, so a datagram has to fit in one Ethernet frame: 1500 − 20 (IPv4) − 8 (UDP) = 1472, minus 32 header bytes. |
| `GBB_MAX_PAYLOAD` = 2028 | `gbb.h` | Spec (field limit) | The limit of the ACF length field, kept as the codec limit. The link carries less. |

## 2. GBB fields and request rules

| Decision | Where | Verdict | Note |
|---|---|---|---|
| `transaction_num` +1 per request, wrapping | `gbb.c` | Spec (§9.4.14.9) | |
| Response copies `transaction_num` and `byte_bus_id` | `gbb_build_response()` | Spec (§9.4.14.9) | |
| Response copies `op` | `gbb_build_response()` | Spec (§9.4.14.10) | `op` marks a read or a write response. |
| `err`=0 in requests | `gbb.c` | Spec (§9.4.14.12) | |
| `hs`=0 | `Avtp_Gbb_Init()` | Spec (§9.4.14.7, Annex O.1.2) | Never set. |
| `cs` and `evt` unused | | Allowed | Chains exist only inside one AVTPDU (§9.4.14.8). With one message per datagram there are no chains. |
| Responder ignores `evt`, `hs`, `cs` and `mtv` in requests | `gbb_check_request()` | Optional | A tolerant Listener. |
| No segmentation: `ms`=0 and `segment_num`=0 are sent, segmented requests get `err`=1 | `gbb.c`, `gbb_check_request()` | Spec allows it | §9.4.14.13 uses segmentation only when a payload doesn't fit in one AVTPDU, and every CS window of the demo fits. Answering `err`=1 gives the Requester a result instead of a timeout. |
| Empty Write Request refused (`-EINVAL` when building, `err`=1 on the Responder) | `gbb.c` | Spec (§9.4.14.10) | A write request expects one or more octets. |
| Read Request with an empty payload allowed | `gbb.c` | Spec (§9.4.14.10) | Zero or more octets. |
| `read_size` of 0 bytes gets `err`=1 | `gbb_check_request()` | Matches the spec | A read response expects one or more octets (§9.4.14.10). |
| KiB decoding of `read_size` | `gbb_read_size_bytes()` | Spec (§9.4.14.14), unused by the demo | The Requester never sends KiB sizes. Within 1440 bytes, only 1 KiB (`0x800`/`0x801`) could be served at all. It is 8 lines with tests, so keeping it costs nothing. |
| `byte_bus_id` 0 unused, buses are 1 and 2 | `protocol.h` | Optional | IDs are application specific (§9.4.14.4, Annex L). 0 is what a Talker sends when no ID is specified. Bus 0 is just an unknown bus with `err`=1 and needs no code of its own. |
| `byte_bus_id` selects SPI bus and CS | architecture.md | Spec (rec.) (Annex O.1.2) | On the Requester, the `byte_bus_id` is the devicetree `reg` of the device on the GBB SPI Controller. |
| A read response must carry exactly `read_size` bytes, otherwise the transaction failed | `gbb_match_response()` | Spec (rec.) | §9.4.14.14 lets a response carry fewer bytes, but Annex M.2.3 NOTE (applied to SPI by Annex O) treats a short read as an error. |
| Error responses carry no payload | `gbb_build_response()` | Optional, deviates from Spec (rec.) | Annex M.2.3 NOTE returns the partial data with `err`=1. A failed `spi_transceive` gives no usable partial data, so nothing is lost. |

## 3. Transactions on the Requester

| Decision | Where | Verdict | Note |
|---|---|---|---|
| One outstanding request, 100 ms timeout, no retry | architecture.md, `protocol.h` | Project | The spec defines no timeouts. This is the simplest scheme that works on a direct cable. A display block lost to a Failed Transaction is rewritten with the next frame a second later. |
| A response must match `transaction_num`, `byte_bus_id` and `op`; others are ignored | `gbb_match_response()` | Project | Without matching, a response that arrives after its timeout would be taken as the answer to the next request. With one outstanding request, `transaction_num` alone would do. The bus and `op` comparison is a cheap extra check. |
| Startup retries every 1 s, then the drivers are initialized with `device_init()` | architecture.md | Project | The boards boot independently, and both drivers talk to their Peripheral during init (`zephyr,deferred-init`). |
| Failed Transaction = timeout or failed match; the SPI transfer returns an error to its driver | architecture.md | Project | |
| Link Stats (sent, timeouts, errors, min/avg/max RTT) | glossary, task 7 | Project | The RTT is the real-time figure the demo can show. |
| Status LEDs | overlays, architecture.md | Project | Part of task 7. Cheap. |

## 4. Responder

| Decision | Where | Verdict | Note |
|---|---|---|---|
| Dumb byte bus, no `bme680`/`ls0xx` driver on the Responder | ADR 0001 | Project | The core of the project. Otherwise GBB would only carry a custom RPC protocol. |
| Byte Bus table with one `spi_config` per bus (CS line and polarity, bit order, CS delays) | architecture.md | HW | Display: LSB first, CS active-high. BME68x: MSB first, CS active-low. The Requester drivers' own `spi_config` doesn't travel in GBB, so this table holds the settings used on the wire. |
| SPI1, mode 0, 1 MHz | architecture.md | HW | Zephyr's `bme680` driver asks for mode 3, but that request stays on the Requester. Bosch specifies the BME68x's SPI for modes 0 and 3. Confirm on hardware in task 5. |
| SPI1 MOSI on PB5 instead of PA7 | `nucleo_f439zi_responder.overlay` | HW | In the Zephyr v4.4.0 board file, SPI1 uses `spi1_mosi_pa7`, and the Ethernet MAC uses `eth_crs_dv_pa7`. Both nodes are enabled. |
| Pull-down/pull-up flags on the CS lines | `nucleo_f439zi_responder.overlay` | Optional | Keeps both peripherals deselected while the pins float during reset. |
| `err`=1 for an unknown Byte Bus | architecture.md | Project | |
| `err`=1 for a `read_size` over 1440 bytes | architecture.md | Project | The response has to fit in one datagram. |
| `err`=1 for an SPI failure | architecture.md | Spec (§9.4.14.12) | |
| Unparseable datagrams and responses (`rsp`=1) are dropped without an answer | `gbb_parse()`, `responder/main.c` | Project | There is no request to answer. "A read response is always generated for each read request" (Annex M.2.3) applies to parsed requests. |
| Every request logged with its payload bytes | `responder/main.c` | Project (task 4) | Task 4's deliverable. Once the Link Stats page measures RTT, serial output in the request path can add to the time it measures. |
| Red LED after an SPI failure | `nucleo_f439zi_responder.overlay` | Project | Part of task 7. Cheap. |
| Receive buffer of `PROTOCOL_MAX_DATAGRAM_LEN` = 1472 bytes | `responder/main.c` | Project | The largest datagram the link delivers. |

## 5. Requester

| Decision | Where | Verdict | Note |
|---|---|---|---|
| GBB SPI Controller with Zephyr's `bme680` and `ls0xx` drivers and the character framebuffer on top | ADR 0004 | Project | The Requester's device code doesn't know its Peripherals are remote. No own device drivers. |
| A held CS window (`SPI_HOLD_ON_CS`) is collected into one Write Request | architecture.md | HW | `ls0xx` sends one display update as a mode byte, one call per line and a trailer, all in one CS window. |
| Display frames passed to `ls0xx` in blocks of at most 71 lines | architecture.md | Project | One update of N lines is 1 + 20·N + 1 bytes. A full screen (3362 bytes) doesn't fit in 1440, but 71 lines (1422 bytes) do. The character framebuffer only writes whole screens. |
| BME68x and display nodes marked `zephyr,deferred-init` | architecture.md | Project | `bme680` reads the chip ID and calibration data, and `ls0xx` clears the screen, both during init. |
| VCOM inverted by `ls0xx`'s VCOM thread (`serial-vcom-inversion`, every 1000 ms) | ADR 0003 | HW | The panel needs regular inversion. A 2-byte Write Request on the Display Bus every second, with no Requester code. |
| Requester's own SPI1 disabled | architecture.md | HW | Its default MOSI pin PA7 is the Ethernet CRS_DV pin. |
| Gas resistance | glossary | Project | Kept. Zephyr's `bme680` driver always runs the heater and measures gas. Its fetch polls the status register every millisecond until the measurement is done, so each fetch is a burst of 1-byte Read Requests. |
| `bme680` transfers | Zephyr `bme680` | HW | One CS window per register access. The largest read is the 23-byte calibration block, far below 1440. |
| Full redraw of the Display Page every second | architecture.md | Project | Replaces the earlier changed-lines-only updates and their VCOM-only request. |
| Button cycles the Display Pages | `requester/main.c`, task 7 | Project | The handler is a placeholder in the task 4 code. |
| Green LED configured but unused | `requester/main.c` | Project (task 5) | Used once responses arrive. |

## 6. Build, Kconfig and devicetree

| Decision | Where | Verdict | Note |
|---|---|---|---|
| One app, role chosen with `-DROLE`, plus `<role>.conf` and `<board>_<role>.overlay` | `app/CMakeLists.txt` | Optional | Two separate apps would work too. A shared `common/` and one build path avoid duplication. |
| Open1722 as a pinned submodule, used header-only | `.gitmodules`, `app/CMakeLists.txt` | Project | |
| Open1722 `GetPayloadLength()` fix (task 9) | `lib/open1722` at `906a0de` | Project | Display blocks are longer than 255 bytes. Before the fix, the Responder would have read their lengths modulo 256. |
| `BUILD_ASSERT`s on the header sizes and the link limit | `gbb.c`, `protocol.h` | Optional | Cheap compile-time guards. |
| `CONFIG_NET_CONTEXT_RCVTIMEO` | `prj.conf` | Project | Needed for the 100 ms receive timeout through `SO_RCVTIMEO`. |
| `CONFIG_NET_CONFIG_INIT_TIMEOUT=5` | `prj.conf` | Optional | Shorter boot with the cable unplugged. |
| `CONFIG_STM32_ENABLE_DEBUG_SLEEP_STOP` | `prj.conf` | Optional | Debugger convenience. |
| `CONFIG_LOG` | `prj.conf` | Optional | The app prints with `printk` only. The option keeps Zephyr's own network and driver messages. |
| `CONFIG_MAIN_STACK_SIZE=2048` | `prj.conf` | Optional | The large buffers are static, so how much stack is needed wasn't checked. |
| `CONFIG_SPI` only in `responder.conf` | `responder.conf` | Project | The Requester gets SPI with the GBB SPI Controller (task 6). |
| Own aliases `link-ok-led`, `link-err-led`, `page-button`, `spi-err-led` | overlays | Optional | The board already has `led0`, `led2` and `sw0`. Own names state each pin's role. |

## 7. Examples, tooling and docs

| Item | Where | Verdict | Note |
|---|---|---|---|
| `hello_world` and `air_quality` | `examples/` | Project (task 2) | The local test of both peripherals. `air_quality` covers everything `hello_world` does. |
| `cookie_clicker` | `examples/cookie_clicker` | Optional | Only a test. |
| The examples use PA7 (D11) for MOSI, the app uses PB5 | example overlays, architecture.md | Optional | See [open item 4](#open-items). |
| Dev container with the `openocd-remote` wrapper | `.devcontainer/` | Project (tooling) | Docker Desktop doesn't pass USB into the container. |
| ADR 0002 (superseded) | `docs/adr/` | Optional | Kept as decision history. |
| "Hardware change" progress note | [Project Plan](project-plan.md) | Optional | Repeats ADR 0003. |
| Serial speed 115400 | [Debugging](debugging.md) | Optional | The board's console runs at 115200. 115400 is 0.17 % off, which a UART tolerates, and it works in practice. |

## 8. Tests

| Item | Where | Verdict | Note |
|---|---|---|---|
| ztest suite for `gbb.h` on `native_sim`, golden datagrams written byte by byte from the spec figures | `app/tests/common` | Project (task 4a) | The spec doesn't require tests. This suite is the only check of the wire format against the spec that needs no boards, and its golden bytes don't depend on Open1722. |
| Round trips with payloads over 255 bytes | `test_parse.c` | Project | Regression test for the Open1722 fix. |
| Tests at the 2028-byte payload limit | `test_build.c`, `test_parse.c` | Optional | They test the codec limit. The link carries at most 1440 bytes. |
| API misuse tests (field ranges, no side effects on `-ENOSPC`) | `test_build.c` | Optional | Cheap. |
| `testcase.yaml` for twister | `app/tests/common` | Optional | |

## Related

- [Architecture](architecture.md)
- [Project Plan](project-plan.md)
- [Open1722 Notes](open1722-notes.md)
- [Decisions](adr/)
