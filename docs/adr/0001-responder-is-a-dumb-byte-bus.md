# The Responder is a dumb byte bus; the Requester owns all device knowledge

The Responder only executes raw byte transfers from GBB Requests on the Byte Bus selected by `byte_bus_id`. It does not use Zephyr's `bme680` or `ls0xx` drivers and knows nothing about register maps, compensation formulas, display commands or fonts. All of that lives on the Requester. This is what makes GBB a *generic* byte bus: adding a new Peripheral only changes the Requester and the Byte Bus table on the Responder.

## Considered Options

- **Smart Responder using Zephyr drivers** and exposing higher-level operations (e.g. "read temperature", "draw text"). Rejected because GBB would then become a custom RPC protocol carried in GBB frames, and the remote-SPI part of the project would disappear.

## Consequences

- The Requester runs Zephyr's `bme680` and `ls0xx` drivers on the GBB SPI Controller, which sends their SPI transfers to the Responder ([ADR 0004](0004-requester-runs-zephyr-drivers-on-a-gbb-spi-controller.md)).
- The Responder's devicetree must not bind the `bme680` / `ls0xx` drivers to these Peripherals. They are reached through raw `spi_transceive` with per-Byte-Bus `spi_config`.
