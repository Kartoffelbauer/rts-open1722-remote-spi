# Design Review: What the Specs Require

This review checks every design decision and every file in `app/`, `examples/`, `docs/` and `.devcontainer/` for whether it is required. It reflects branch `Task-4` at commit `73058e9` (2026-11-17). Each item is checked against:

- **IEEE 1722-2025** (`docs/1722-2025.pdf`): §4.6, §9.2, §9.4.1, §9.4.14, Annex J (normative) and Annexes L, M, O (informative)
- **The project goal**: the [README](../README.md) and the tasks in the [Project Plan](project-plan.md)
- **The hardware**: the Zephyr v4.4.0 board file for `nucleo_f439zi`, and the peripherals as described in [Architecture](architecture.md)

The course assignment isn't in the repo. "Project" below therefore means the README's goal and demo, not the assignment.

## Verdicts

| Verdict | Meaning |
|---|---|
| **Spec** | A "shall" in a normative clause of IEEE 1722-2025 |
| **Spec (rec.)** | Recommended by an informative annex (L, M or O) |
| **HW** | Forced by the boards or the peripherals |
| **Project** | Needed for the project goal or the demo |
| **Optional** | Not needed by any of the above: convenience, robustness or polish |
| **Gap** | Conflicts with the spec or with another part of the design |

## Summary

**Gaps**

1. **Responder source port.** Annex J.4 forbids 17220 as the source port, so the Responder can't send its responses from the socket it listens on ([G1](#g1-responder-source-port-annex-j4)).
2. **Payload limit on the wire.** Without IP fragmentation, the largest GBB payload in one datagram is 1440 bytes, not the 2028 bytes the ACF length field allows ([G2](#g2-payload-limit-on-the-wire)).
3. **Destination port.** With `sv`=0 the stream is best effort, which fits the discrete style and port 17221 better than 17220 ([G3](#g3-destination-port-17220-or-17221)).
4. **Changed-lines-only display updates without retries.** A failed chunk leaves its lines stale ([G4](#g4-changed-lines-only-display-updates-without-retries)).

**Not required: candidates to drop or simplify**

- Sending only changed display lines, and the 2-byte VCOM-only request that follows from it
- Gas resistance (heater setup and gas compensation in the Requester's BME68x driver)
- The "busy" error case: the Responder has no busy state
- The `cookie_clicker` example
- Two hand-written device drivers on the Requester: a GBB SPI controller driver could let Zephyr's own drivers run there ([alternative](#own-drivers-or-a-gbb-spi-controller-on-the-requester))

**Required, though it looks optional:** the PB5 MOSI override, `sv`=0, the MAC-based `stream_id`, both sequence numbers and the Open1722 length fix.

**Incidental:** wrong baud rate in [Debugging](debugging.md), a wrong figure citation in [Architecture](architecture.md), and different MOSI wiring for the examples and the app (see [Examples, tooling and docs](#7-examples-tooling-and-docs)).

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
| Exactly one GBB message per datagram; a second ACF message, ABB or trailing bytes are dropped | `gbb_parse()` | Project, deviates from Spec (rec.) | Annex M NOTE 2 says Listeners need to accept both GBB and ABB, and the spec's own read response example (Figure M.8) is ABB. This is fine while both ends run this code. [Open1722 Notes](open1722-notes.md) states it for the Responder. It also applies to the Requester's response parsing. |
| Requester sends from an ephemeral port | `requester/main.c` | Spec (J.4) | |
| Responder listens on UDP 17220 | `protocol.h` | Gap (judgment) | See [G3](#g3-destination-port-17220-or-17221). |
| Responder replies to the request's source address and port | architecture.md (task 5) | Gap if it replies from the listening socket | See [G1](#g1-responder-source-port-annex-j4). |
| `GBB_MAX_PAYLOAD` = 2028 | `gbb.h` | Spec (field limit), Gap on the wire | See [G2](#g2-payload-limit-on-the-wire). |

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
| No segmentation: `ms`=0 and `segment_num`=0 are sent, segmented requests get `err`=1 | `gbb.c`, `gbb_check_request()` | Spec allows it | §9.4.14.13 uses segmentation only when a payload doesn't fit in one AVTPDU, and display updates are split into separate Write Requests instead. Answering `err`=1 gives the Requester a result instead of a timeout. |
| Empty Write Request refused (`-EINVAL` when building, `err`=1 on the Responder) | `gbb.c` | Spec (§9.4.14.10) | A write request expects one or more octets. |
| Read Request with an empty payload allowed | `gbb.c` | Spec (§9.4.14.10) | Zero or more octets. |
| `read_size` of 0 bytes gets `err`=1 | `gbb_check_request()` | Matches the spec | A read response expects one or more octets (§9.4.14.10). |
| KiB decoding of `read_size` | `gbb_read_size_bytes()` | Spec (§9.4.14.14), unused by the demo | The Requester never sends KiB sizes. Within one datagram ([G2](#g2-payload-limit-on-the-wire)), only 1 KiB (`0x800`/`0x801`) could be served at all. It is 8 lines with tests, so keeping it costs nothing. |
| `byte_bus_id` 0 unused, buses are 1 and 2 | `protocol.h` | Optional | IDs are application specific (§9.4.14.4, Annex L). 0 is what a Talker sends when no ID is specified. Bus 0 is just an unknown bus with `err`=1 and needs no code of its own. |
| `byte_bus_id` selects SPI bus and CS | architecture.md | Spec (rec.) (Annex O.1.2) | |
| A read response must carry exactly `read_size` bytes, otherwise the transaction failed | `gbb_match_response()` | Spec (rec.) | §9.4.14.14 lets a response carry fewer bytes, but Annex M.2.3 NOTE (applied to SPI by Annex O) treats a short read as an error. |
| Error responses carry no payload | `gbb_build_response()` | Optional, deviates from Spec (rec.) | Annex M.2.3 NOTE returns the partial data with `err`=1. A failed `spi_transceive` gives no usable partial data, so nothing is lost. |

## 3. Transactions on the Requester

| Decision | Where | Verdict | Note |
|---|---|---|---|
| One outstanding request, 100 ms timeout, no retry | architecture.md, `protocol.h` | Project | The spec defines no timeouts. This is the simplest scheme that works on a direct cable. See [G4](#g4-changed-lines-only-display-updates-without-retries) for the effect of "no retry". |
| A response must match `transaction_num`, `byte_bus_id` and `op`; others are ignored | `gbb_match_response()` | Project | Without matching, a response that arrives after its timeout would be taken as the answer to the next request. With one outstanding request, `transaction_num` alone would do. The bus and `op` comparison is a cheap extra check. |
| Startup retries every 1 s until the Responder answers | architecture.md | Project | The boards boot independently. |
| Failed Transaction = timeout or failed match | architecture.md | Project | |
| Link Stats (sent, timeouts, errors, min/avg/max RTT) | glossary, task 7 | Project | The RTT is the real-time figure the demo can show. |
| Status LEDs | overlays, architecture.md | Project | Part of task 7. Cheap. |

## 4. Responder

| Decision | Where | Verdict | Note |
|---|---|---|---|
| Dumb byte bus, no `bme680`/`ls0xx` driver on the Responder | [ADR 0001](adr/0001-responder-is-a-dumb-byte-bus.md) | Project | The core of the project. Otherwise GBB would only carry a custom RPC protocol. |
| Byte Bus table with one `spi_config` per bus (CS line and polarity, bit order, CS delays) | architecture.md | HW | Display: LSB first, CS active-high. BME68x: MSB first, CS active-low. |
| SPI1, mode 0, 1 MHz | architecture.md | HW | |
| SPI1 MOSI on PB5 instead of PA7 | `nucleo_f439zi_responder.overlay` | HW | In the Zephyr v4.4.0 board file, SPI1 uses `spi1_mosi_pa7`, and the Ethernet MAC uses `eth_crs_dv_pa7`. Both nodes are enabled. |
| Pull-down/pull-up flags on the CS lines | `nucleo_f439zi_responder.overlay` | Optional | Keeps both peripherals deselected while the pins float during reset. |
| `err`=1 for an unknown Byte Bus | architecture.md | Project | |
| `err`=1 for a `read_size` larger than the buffer | architecture.md | Project | Memory safety. |
| `err`=1 for an SPI failure | architecture.md | Spec (§9.4.14.12) | |
| `err`=1 for a Read Request on the Display Bus | architecture.md | Optional | The display has no MISO, so a read would return the idle level of the MISO line. Refusing it is a per-bus write-only flag. |
| `err`=1 when busy | architecture.md | Not needed | The Responder handles one datagram at a time in a single loop, so it has no busy state. Further requests wait in the socket's receive queue. |
| Unparseable datagrams and responses (`rsp`=1) are dropped without an answer | `gbb_parse()`, `responder/main.c` | Project | There is no request to answer. "A read response is always generated for each read request" (Annex M.2.3) applies to parsed requests. |
| Every request logged with its payload bytes | `responder/main.c` | Project (task 4) | Task 4's deliverable. Once the Link Stats page measures RTT, serial output in the request path can add to the time it measures. |
| Red LED after an SPI failure | `nucleo_f439zi_responder.overlay` | Project | Part of task 7. Cheap. |
| Receive buffer of `GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD)` = 2060 bytes | `responder/main.c` | Optional | Larger than the largest datagram the network delivers (1472 bytes, see [G2](#g2-payload-limit-on-the-wire)). |

## 5. Requester: device logic and demo

| Decision | Where | Verdict | Note |
|---|---|---|---|
| Own BME68x driver (page select, calibration data, compensation) | architecture.md, ADR 0001 | Project | Needed by the design, but see the [alternative](#own-drivers-or-a-gbb-spi-controller-on-the-requester). |
| Gas heater setup and a gas resistance Display Page | architecture.md ("if gas readings are used"), glossary | Optional | Temperature, pressure and humidity need no heater. Gas readings add a heater profile, their own compensation and a longer measurement. |
| Own SHARP driver (framebuffer, font, line writes, VCOM) | architecture.md, ADR 0001 | Project | Needed by the design, but see the [alternative](#own-drivers-or-a-gbb-spi-controller-on-the-requester). |
| VCOM inverted by the Requester over SPI | [ADR 0003](adr/0003-sharp-display-is-one-byte-bus.md) | HW | The panel needs regular inversion. Doing it on the Requester keeps ADR 0001. |
| Display updates split into several Write Requests | architecture.md | HW | A full frame has 168 × 18 = 3024 bytes of pixel data, more than the 1440-byte limit of one message ([G2](#g2-payload-limit-on-the-wire)), so a full redraw takes at least 3 Write Requests. |
| Only changed lines are sent | architecture.md | Optional | A full redraw is at least 3 Write Requests and about 24 ms of SPI clock at 1 MHz (3024 × 8 bits) once per second. Changed-lines-only updates add a shadow framebuffer and the problem in [G4](#g4-changed-lines-only-display-updates-without-retries). |
| 2-byte VCOM-only Write Request when no line changed | ADR 0003, architecture.md | Optional | Only needed because of changed-lines-only updates. With a full redraw every second, there is already a Display Bus write that flips VCOM every second. |
| Button cycles the Display Pages | `requester/main.c`, task 7 | Project | The handler is a placeholder in the task 4 code. |
| Green LED configured but unused | `requester/main.c` | Project (task 5) | Used once responses arrive. |
| 9-byte test payload sent to the Display Bus every second | `requester/main.c` | Project (task 4) | Once the Responder runs SPI transfers (task 5), these bytes reach the display as a real transfer. |

### Own drivers or a GBB SPI controller on the Requester

ADR 0001 says the Requester "can't reuse Zephyr's drivers because those talk to a local bus". In fact, Zephyr drivers talk to whatever SPI controller driver is their devicetree parent. Suppose the Requester had an SPI controller driver whose `transceive` sends a GBB Request, with the child node's `reg` as the `byte_bus_id`. Then Zephyr's `bme680` and `ls0xx` drivers, and the character framebuffer used in the examples, would run on the Requester unchanged, and the Responder would stay a dumb byte bus. The two hand-written drivers are therefore a choice, not a consequence of ADR 0001.

Two limits apply. Neither was checked against the driver sources:

- GBB carries writes and write-then-read transfers, not full-duplex transfers. The controller can only serve `transceive` calls of those two shapes.
- A single transfer larger than 1440 bytes needs GBB segmentation with CS held across the segments, or writes of fewer lines per call. A full-screen `ls0xx` write in one call is such a transfer.

## 6. Build, Kconfig and devicetree

| Decision | Where | Verdict | Note |
|---|---|---|---|
| One app, role chosen with `-DROLE`, plus `<role>.conf` and `<board>_<role>.overlay` | `app/CMakeLists.txt` | Optional | Two separate apps would work too. A shared `common/` and one build path avoid duplication. |
| Open1722 as a pinned submodule, used header-only | `.gitmodules`, `app/CMakeLists.txt` | Project | |
| Open1722 `GetPayloadLength()` fix (task 9) | `lib/open1722` at `906a0de` | Project | Display chunks are longer than 255 bytes. Before the fix, the Responder would have read their lengths modulo 256. |
| `BUILD_ASSERT`s on the header sizes | `protocol.h`, `gbb.c` | Optional | A cheap compile-time guard. The one in `protocol.h` is covered by the combined one in `gbb.c`. |
| `CONFIG_NET_CONTEXT_RCVTIMEO` | `prj.conf` | Project | Needed for the 100 ms receive timeout through `SO_RCVTIMEO`. |
| `CONFIG_NET_CONFIG_INIT_TIMEOUT=5` | `prj.conf` | Optional | Shorter boot with the cable unplugged. |
| `CONFIG_STM32_ENABLE_DEBUG_SLEEP_STOP` | `prj.conf` | Optional | Debugger convenience. |
| `CONFIG_LOG` | `prj.conf` | Optional | The app prints with `printk` only. The option keeps Zephyr's own network and driver messages. |
| `CONFIG_MAIN_STACK_SIZE=2048` | `prj.conf` | Optional | The large buffers are static, so how much stack is needed wasn't checked. |
| `CONFIG_SPI` only in `responder.conf` | `responder.conf` | Project | |
| Own aliases `link-ok-led`, `link-err-led`, `page-button`, `spi-err-led` | overlays | Optional | The board already has `led0`, `led2` and `sw0`. Own names state each pin's role. |

## 7. Examples, tooling and docs

| Item | Where | Verdict | Note |
|---|---|---|---|
| `hello_world` and `air_quality` | `examples/` | Project (task 2) | The local test of both peripherals. `air_quality` covers everything `hello_world` does. |
| `cookie_clicker` (933 lines, AI-generated) | `examples/cookie_clicker` | Optional | No task needs it. |
| The examples use PA7 (D11) for MOSI, the app uses PB5 | example overlays, architecture.md | Optional | The examples don't enable networking, so PA7 works there. With PB5 in the examples as well, one wiring would fit everything, and the "move the MOSI wire" step in [Architecture](architecture.md) would go away. Not tried on hardware. |
| Dev container with the `openocd-remote` wrapper | `.devcontainer/` | Project (tooling) | Docker Desktop doesn't pass USB into the container. The wrapper, `Dockerfile` and `post-create.sh` still have German messages and comments, and the closing hint of `post-create.sh` names the `nrf52840dk` board. |
| ADR 0002 (superseded) | `docs/adr/` | Optional | Kept as decision history. |
| "Hardware change" progress note | [Project Plan](project-plan.md) | Optional | Repeats ADR 0003. |
| Serial speed "115400" | [Debugging](debugging.md) | Gap (typo) | Not a standard baud rate. 115200 is meant. |
| "The response carries only the bytes read (… Figure M.7)" | [Architecture](architecture.md) | Gap (citation) | Figure M.7 is the read request. The read response is Figure M.8. |

## 8. Tests

| Item | Where | Verdict | Note |
|---|---|---|---|
| ztest suite for `gbb.h` on `native_sim`, golden datagrams written byte by byte from the spec figures | `app/tests/common` | Project (task 4a) | The spec doesn't require tests. This suite is the only check of the wire format against the spec that needs no boards, and its golden bytes don't depend on Open1722. |
| Round trips with payloads over 255 bytes | `test_parse.c` | Project | Regression test for the Open1722 fix. |
| Tests at the 2028-byte payload limit | `test_build.c`, `test_parse.c` | Optional | They test the codec limit, which never reaches the wire ([G2](#g2-payload-limit-on-the-wire)). |
| API misuse tests (field ranges, no side effects on `-ENOSPC`) | `test_build.c` | Optional | Cheap. |
| `testcase.yaml` for twister | `app/tests/common` | Optional | |

## Gaps in detail

### G1: Responder source port (Annex J.4)

J.4 requires the source port of an IP AVTPDU to be an ephemeral port (RFC 6056) or an IANA-registered application-specific port. Its NOTE adds that the source port is therefore never 17220 or 17221. A Responder that answers from its listening socket sends from 17220.

A second socket, bound to port 0 and used only for sending responses, meets J.4. The Requester's socket isn't connected, so it accepts the response from any source port. The Annex J sequence number still counts per address and port pair (J.3.2), and that pair stays fixed for the Responder's response socket.

### G2: Payload limit on the wire

IPv4 fragmentation is off by default in Zephyr. The help text of `CONFIG_NET_IPV4_FRAGMENT` says that this "limits incoming and outgoing packets to the MTU (1500 bytes for Ethernet)". The largest datagram is therefore 1500 − 20 (IPv4 header) − 8 (UDP header) = 1472 bytes. That leaves 1472 − 32 (`GBB_HEADERS_LEN`) = 1440 bytes of GBB payload.

- "Every request and response fits in one datagram" ([Architecture](architecture.md)) holds only if Write Requests carry at most 1440 bytes and the Responder's read limit is at most 1440 bytes.
- The Responder's receive buffer can shrink from 2060 bytes to 1472.
- `GBB_MAX_PAYLOAD` = 2028 stays correct as the limit of the ACF length field. The tests at that size test the codec, not the link.

### G3: Destination port 17220 or 17221

In Table 7, NTSCF's encapsulation style is "Continuous or Discrete". The table's NOTE says that NTSCF with continuous encapsulation is a bounded-latency transport, and NTSCF with discrete encapsulation is a best-effort transport. Bounded latency relies on a stream reservation (§4.3.1). J.5 assigns port 17220 to the continuous style and 17221 to the discrete style. An IANA-registered application port or an ephemeral port is allowed for either.

The project sends `sv`=0 (no reservation), so its stream is best effort, which matches the discrete style and port 17221. Switching changes one constant in `protocol.h`, plus the port and its description as "the IANA AVTP port" in [Architecture](architecture.md), and the Wireshark filter and the Python one-liner in [Debugging](debugging.md).

### G4: Changed-lines-only display updates without retries

The Requester sends only changed display lines and doesn't retry a Failed Transaction. If one Write Request on the Display Bus fails, its lines stay wrong on the panel until their content changes again. A full redraw every second avoids this (see [section 5](#5-requester-device-logic-and-demo)). If changed-lines-only updates stay, the lines of a failed request have to count as changed again.

## Related

- [Architecture](architecture.md)
- [Project Plan](project-plan.md)
- [Open1722 Notes](open1722-notes.md)
- [Decisions](adr/)
