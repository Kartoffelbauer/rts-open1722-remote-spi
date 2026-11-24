# The Requester runs Zephyr's drivers on a GBB SPI Controller

**Status:** accepted. Replaces the consequence of [ADR 0001](0001-responder-is-a-dumb-byte-bus.md) that the Requester needs its own device drivers.

The Requester should use the remote Peripherals as if they were wired to it. It therefore has the **GBB SPI Controller**, a Zephyr SPI controller driver that carries out each SPI transfer as a GBB Request on the Byte Bus given by the device's devicetree `reg`. Zephyr's `bme680` and `ls0xx` drivers and its character framebuffer run on it unchanged. The Responder stays a dumb byte bus (ADR 0001). Only the bus between the drivers and the Peripherals is remote.

## Considered Options

- **Own BME68x and SHARP drivers on the Requester** that build GBB Requests directly. That would mean writing two drivers Zephyr already has (register pages, calibration, compensation and gas heater; framebuffer, font, line writes and VCOM), and the device code would know that its Peripherals are remote.

## Consequences

- The controller supports what GBB carries: transmit-only and transmit-then-receive transfers in one CS window of at most 1440 bytes. Full-duplex transfers and larger windows (which would need GBB segmentation) are refused.
- `ls0xx` sends a display update as several transfers with `SPI_HOLD_ON_CS`, so the controller collects a held CS window and sends it as one Write Request when the driver releases the bus.
- The character framebuffer writes the whole screen at once (3362 bytes), so the Requester passes each frame to `ls0xx` in blocks of at most 71 lines.
- Both drivers talk to their Peripheral during init, possibly before the Responder is reachable. Their devicetree nodes are marked `zephyr,deferred-init`, and the Requester initializes them with `device_init()` once the Responder answers.
- The drivers' `spi_config` doesn't travel in GBB. The Responder's Byte Bus table holds the SPI settings used on the wire.
- `ls0xx` inverts VCOM itself: its VCOM thread sends a 2-byte command every second ([ADR 0003](0003-sharp-display-is-one-byte-bus.md)).
