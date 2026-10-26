# The Responder is a dumb byte bus; the Requester owns all device knowledge

The Responder only executes raw byte transfers from GBB Requests on the Byte Bus selected by `byte_bus_id`. It does not use Zephyr's `bme280` or `ssd1306` drivers and knows nothing about register maps, compensation formulas, SSD1306 commands or fonts. All of that lives on the Requester. This is what makes GBB a *generic* byte bus: adding a new Peripheral only changes the Requester and the Byte Bus table on the Responder.

## Considered Options

- **Smart Responder using Zephyr drivers** and exposing higher-level operations (e.g. "read temperature", "draw text"). Rejected because GBB would then become a custom RPC protocol carried in GBB frames, and the remote-SPI part of the project would disappear.

## Consequences

- The Requester needs its own BME280 driver (register reads, calibration data, compensation) and its own SSD1306 driver (init sequence, framebuffer, font rendering). It can't reuse Zephyr's drivers because those talk to a local bus.
- The Responder's devicetree must not bind the `bme280` / `ssd1306` drivers to these Peripherals. They are reached through raw `spi_transceive` with per-Byte-Bus `spi_config`.
