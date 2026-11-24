# Architecture

Terms in **bold** are defined in the [Glossary](../GLOSSARY.md). The reasons behind the main decisions are recorded in the [ADRs](adr/).

## System overview

```mermaid
flowchart LR
    A["A: <b>NUCLEO-F439ZI</b><br/><i>Requester</i><br/>Zephyr drivers on the GBB SPI Controller"]
    B["B: <b>NUCLEO-F439ZI</b><br/><i>Responder</i><br/>GBB request → SPI transfer"]
    O["SHARP Memory Display<br/>LS013B7DH05<br/><i>SPI peripheral</i>"]
    S["BME68x<br/><i>SPI peripheral</i>"]

    A -- "GBB over UDP<br/>Ethernet cable" --> B
    B -. "GBB response" .-> A
    B -- "SPI1" --> O
    B -- "SPI1" --> S
```

| Link | Between | Medium | Protocol |
|---|---|---|---|
| Network | Requester ↔ Responder | Direct Ethernet cable, static IPs | IEEE 1722 over UDP (Annex J) |
| SPI | Responder ↔ Display / BME68x | Plain wires on the pins | SPI1, mode 0, 1 MHz, one CS per peripheral (settings per **Byte Bus**, see below) |

The peripherals are an Adafruit SHARP Memory Display 1.3" (LS013B7DH05, 144×168 pixels, monochrome, write-only) and a BME68x environmental sensor (BME680 or BME688: temperature, humidity, pressure, gas resistance). See [Hardware](hardware.md).

## Roles

- **Requester** (board A): holds all device knowledge, in Zephyr's own drivers. Zephyr's `bme680` and `ls0xx` drivers and its character framebuffer run on the **GBB SPI Controller**, so they use the remote Peripherals as if those were wired to the Requester ([ADR 0004](adr/0004-requester-runs-zephyr-drivers-on-a-gbb-spi-controller.md)). The Requester also handles the button, the **Display Pages** and the **Link Stats**.
- **Responder** (board B): a dumb byte bus. It runs each GBB Request as one raw SPI transfer on the selected **Byte Bus** and sends back a **GBB Response**. It does not use Zephyr's `bme680` or `ls0xx` drivers ([ADR 0001](adr/0001-responder-is-a-dumb-byte-bus.md)).

## GBB SPI Controller

A Zephyr SPI controller driver on the Requester. In the Requester's devicetree, the BME68x and the display are its child nodes, and each child's `reg` is its `byte_bus_id`.

- One CS window becomes one GBB Request. A transfer that only transmits becomes a Write Request. A transfer that transmits and then receives becomes a Read Request: the transmitted bytes are the payload, and the bytes received after them set `read_size`. Receive buffers over the transmit part get no data, because Annex O doesn't transfer those bytes.
- A transfer with `SPI_HOLD_ON_CS` keeps the CS window open. The controller collects the bytes and sends them as one Write Request when the driver releases the bus. `ls0xx` sends each display update this way: the mode byte, one transfer per line, then the trailer.
- A CS window over 1440 bytes (see [Protocol](#protocol)) is refused with `-EMSGSIZE`. GBB segmentation isn't implemented.
- Transfers from several threads (the `ls0xx` VCOM thread and the main thread) go out one at a time.
- The drivers' own SPI settings (`spi_config`: mode, bit order, CS polarity, frequency) don't travel in GBB. The Responder's Byte Bus table holds the settings used on the wire.
- The BME68x and display nodes are marked `zephyr,deferred-init`, because both drivers talk to their Peripheral during init. The Requester initializes them with `device_init()` once the Responder answers.
- The Requester's own SPI1 is disabled, because its default MOSI pin PA7 carries the Ethernet RMII CRS_DV signal.

## Byte Buses

| `byte_bus_id` | Byte Bus | Peripheral | SPI settings |
|---|---|---|---|
| 0 | (unused, the spec's "unspecified" default) | none | answered with `err`=1 |
| 1 | Sensor Bus | BME68x | MSB first, CS active-low |
| 2 | Display Bus | SHARP Memory Display | LSB first, CS active-high, 3 µs CS setup / 1 µs CS hold |

The display's VCOM inversion is done by the Requester over SPI ([ADR 0003](adr/0003-sharp-display-is-one-byte-bus.md)).

## Protocol

**Framing.** One GBB message per UDP datagram:

```
UDP → Annex J header (4) → NTSCF v0 header (12) → GBB header (16) → payload → pad (0-3)
```

- Requester: 192.168.1.10, sends from an ephemeral port.
- Responder: 192.168.1.11, listens on UDP 17221. It sends each response from a second socket on an ephemeral port, to the source address and port of the request, because Annex J.4 doesn't allow 17221 as a source port.
- 17221 is the destination port for discrete encapsulation (Annex J.5). The stream has no reservation, so it is a best-effort transport, and best-effort NTSCF uses discrete encapsulation (Table 7).
- Open1722 builds and parses the headers, wrapped by `src/common/gbb.h`.
- NTSCF version 0 with `sv`=0 (stream without a reservation, §9.2.2). The `stream_id` is the sender's MAC address followed by a 16-bit ID (§4.6).
- Each sender increments the Annex J `encapsulation_sequence_num` and the NTSCF `sequence_num_lsb` per datagram. The receiver ignores both, and the `stream_id`.
- A datagram that isn't exactly one GBB message (a second ACF message, ABB, bytes left over) is dropped.
- A datagram is at most 1472 bytes: IPv4 fragmentation isn't enabled, so it has to fit in one Ethernet frame (MTU 1500 − 20 bytes IPv4 header − 8 bytes UDP header). After the 32 header bytes, a GBB message carries at most 1440 payload bytes.

**Requests.**

- **Write Request** (`op`=1): the payload is clocked out in one CS assertion.
- **Read Request** (`op`=0): the payload (e.g. a register address) is clocked out, then `read_size` bytes are read in the same CS assertion. The response carries only the bytes read (Annex O → Annex M, Figures M.7 and M.8). Only the Sensor Bus uses Read Requests; the display has no MISO.
- `read_size` is decoded as in §9.4.14.14: with bit 11 set, the value is in KiB (`0x800` also means 1 KiB).
- Segmentation (`ms`, `segment_num`) isn't used. A request carries at most 1440 payload bytes, and a Read Request asks for at most 1440 bytes, so every request and response fits in one datagram.
- A full-screen display update is one CS window of 3362 bytes (1 mode byte, 168 lines of 20 bytes, 1 trailer byte), which doesn't fit in one GBB message. The character framebuffer writes the whole screen at once, so the Requester passes each frame to `ls0xx` in blocks of at most 71 lines (1422 bytes).

**Transactions.**

- At most one request is outstanding at a time. The timeout is 100 ms, with no retry.
- The Requester increments `transaction_num` for each request (wrapping at 255). The Responder echoes it together with `byte_bus_id` and `op`, and the Requester throws away any response where one of them doesn't match.
- A matching response fails if `err`=1 or if a read response doesn't carry exactly `read_size` bytes.
- The Requester redraws the current Display Page every second, so a block lost to a Failed Transaction is rewritten with the next frame.

**Errors.**

| Situation on the Responder | Reaction |
|---|---|
| Valid GBB Request that can't be executed: unknown Byte Bus, `read_size` of 0 bytes or more than 1440 bytes, empty Write Request, segmented request (`ms`=1 or `segment_num`≠0), SPI failure | GBB Response with `err`=1 |
| Datagram that doesn't parse: too short, not NTSCF v0 with exactly one GBB message, `rsp`=1 | Drop and log, no response |

On the Requester, a timeout and a failed matching response both count as a **Failed Transaction**, and the SPI transfer that caused it returns an error to its driver.

## Status LEDs

| Board | LED | Meaning |
|---|---|---|
| Requester | Red (LD3) | On after a Failed Transaction, off again after the next successful one |
| Requester | Green (LD1) | Toggles on every successful GBB Response |
| Responder | Red (LD3) | On after a local SPI failure |

## Code layout

One Zephyr app. The shared core is used by both roles, and each role has its own `main.c`.

```
app/
  src/common/     GBB framing/parsing (gbb.h), UDP transport, protocol constants
  src/requester/  main, GBB SPI Controller, Display Pages
  src/responder/  main, Byte Bus table (byte_bus_id → spi_config incl. CS)
  dts/bindings/   devicetree binding of the GBB SPI Controller
  tests/common/   ztest suite for gbb.h on native_sim
  requester.conf / responder.conf
  boards/         per-role overlays for nucleo_f439zi
lib/open1722/     git submodule, pinned commit
```

The role is chosen at build time with `-DROLE=requester` or `-DROLE=responder`. From that, `app/CMakeLists.txt` adds `<role>.conf` and `boards/<board>_<role>.overlay`, and compiles `src/common/` plus `src/<role>/`. Open1722 is used header-only: its functions are `static inline`, so the build only adds `lib/open1722/include`.

## Wiring (Responder)

| Signal | MCU pin | Nucleo header | Peripheral pin |
|---|---|---|---|
| SPI1 SCK | PA5 | D13 | BME68x SCL/SCK, display CLK |
| SPI1 MISO | PA6 | D12 | BME68x SDO (the display has no MISO) |
| SPI1 MOSI | **PB5** | D22 (ZIO) | BME68x SDA/SDI, display DI |
| CS display | PD14 | D10 | display CS (active-high) |
| CS BME68x | PD15 | D9 | BME68x CS (active-low) |

The chip selects match the standalone [`air_quality`](../examples/air_quality) example, so the same wiring works for both. Only MOSI differs: the examples use the board's default PA7 (D11), but the app uses PB5, because PA7 carries the Ethernet RMII CRS_DV signal. Move the MOSI wire from D11 to PB5 when switching from an example to the app, and check the header position of PB5 against the NUCLEO-144 user manual (UM1974) before wiring.

The display's DISP and EXTMODE/EXTCOMIN pins stay unconnected (the breakout's defaults: display on, VCOM inverted over SPI).

## Demo flow

```mermaid
sequenceDiagram
    participant A as Requester
    participant B as Responder
    participant S as BME68x
    participant O as SHARP Display

    Note over A,B: Startup: the Requester retries every 1 s until the Responder answers, then initializes<br/>the bme680 driver (chip ID, calibration data) and the ls0xx driver (clear screen).
    loop Every 1 s
        A->>B: Write Request (Byte Bus 1: start a forced measurement)
        B->>S: SPI transfer (one CS assertion)
        B-->>A: GBB Response
        loop Until the measurement is done
            A->>B: Read Request (Byte Bus 1: status register)
            B->>S: SPI transfer (one CS assertion)
            B-->>A: GBB Response (status)
        end
        A->>B: Read Request (Byte Bus 1: data registers)
        B->>S: SPI transfer (one CS assertion)
        B-->>A: GBB Response (data)
        Note over A: Draw the current Display Page
        loop For each block of up to 71 lines
            A->>B: Write Request (Byte Bus 2: mode byte with VCOM bit, lines, trailer)
            B->>O: SPI transfer, LSB first
            B-->>A: GBB Response
        end
    end
    Note over A,O: Every 1 s, on its own: the ls0xx VCOM thread sends a 2-byte Write Request on Byte Bus 2
    Note over A: Button press → next Display Page
```

See [Project Plan](project-plan.md) for how this is built up step by step, and [Hardware](hardware.md) for the parts.
