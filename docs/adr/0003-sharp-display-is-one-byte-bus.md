# The SHARP Memory Display is one Byte Bus; the Requester inverts VCOM

**Status:** proposed. Supersedes [ADR 0002](0002-oled-dc-line-via-two-byte-buses.md).

The project switched from an SSD1306 OLED and a BME280 to an Adafruit SHARP Memory Display 1.3" (LS013B7DH05) and a BME68x, because that is the hardware we have. The SHARP display has no D/C line and no reset line: commands and pixel data share one SPI stream, told apart by a mode byte at the start of each transfer. So the display is reached through a single **Display Bus**, and the reason for ADR 0002 is gone.

The panel needs its VCOM polarity inverted regularly. The breakout can do that through an EXTCOMIN pin, or the M1 bit of the mode byte can do it over SPI. We use the SPI variant, driven by the Requester: every Write Request on the Display Bus flips the VCOM bit, and the Requester sends at least one per second (a 2-byte VCOM-only transfer if nothing on screen changed).

## Considered Options

- **EXTCOMIN driven by a timer on the Responder**: keeps the panel safe even when the Requester is down, but the Responder would then know about a Peripheral's needs, which breaks [ADR 0001](0001-responder-is-a-dumb-byte-bus.md), and it needs an extra wire.
- **Responder sends VCOM-only transfers on its own**: same problem with ADR 0001, and the Responder would inject SPI traffic that no GBB Request asked for.

## Consequences

- One Byte Bus per Peripheral. The Responder's Byte Bus table only maps `byte_bus_id` to an `spi_config` (bit order, CS line and polarity, CS delays). It drives no GPIOs besides the chip selects.
- The Display Bus is LSB first with an active-high CS. The Sensor Bus is MSB first with an active-low CS. Both differences live in the per-bus `spi_config`, not in the payload.
- If the Requester or the link stops, VCOM stops being inverted. The datasheet warns that a panel left without inversion degrades over time, so don't leave the Responder powered for long with the Requester stopped.
