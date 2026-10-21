# Architecture

## System overview

```mermaid
flowchart LR
    A["A: <b>NUCLEO-F439ZI</b><br/><i>Requester</i><br/>Builds GBB requests"]
    B["B: <b>NUCLEO-F439ZI</b><br/><i>Responder / SPI master</i><br/>GBB request → SPI transfer"]
    O["SSD1306 OLED<br/><i>SPI slave</i>"]
    S["BME280<br/><i>SPI slave</i>"]

    A -- "GBB over UDP<br/>Ethernet cable" --> B
    B -. "GBB response" .-> A
    B -- "SPI wires" --> O
    B -- "SPI wires" --> S
```

| Link | Between | Medium | Protocol |
|---|---|---|---|
| Network | Board A ↔ Board B | Ethernet cable, static IPs | IEEE 1722 over UDP (Annex J) |
| SPI | Board B ↔ OLED / BME280 | Plain wires on the pins | SPI, no network |

## Roles

- **Board A (Requester):** builds GBB requests and sends them to Board B. Handles the button and the demo logic.
- **Board B (Responder / SPI master):** receives GBB requests, executes the SPI transfer on its pins and sends back a GBB response.

## Design notes

- Zephyr RTOS runs on both boards.
- [Open1722](https://github.com/COVESA/Open1722) is used for GBB message handling.
- The `byte_bus_id` field selects the SPI device on Board B.
- The BME280 is used first as the simple test of the remote link, then the OLED.
- Relevant spec sections: §9.4.14 (GBB), Annex O, Annex J (1722 over UDP).

## Demo flow

```mermaid
sequenceDiagram
    participant A as Board A (Requester)
    participant B as Board B (Responder)
    participant S as BME280
    participant O as SSD1306 OLED

    Note over A: Button press
    A->>B: GBB request (read temperature)
    B->>S: SPI transfer
    S-->>B: Sensor data
    B-->>A: GBB response
    A->>B: GBB request (write display data)
    B->>O: SPI transfer
```

See [Project Plan](project-plan.md) for how this is built up step by step, and [Hardware](hardware.md) for the parts.