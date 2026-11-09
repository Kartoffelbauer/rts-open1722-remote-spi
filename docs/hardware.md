# Hardware

| Item | Amount |
|---|---|
| NUCLEO-F439ZI | 2 (already owned) |
| Adafruit SHARP Memory Display 1.3" (LS013B7DH05, 144×168, monochrome) | 1 |
| BME68x breakout (BME680 or BME688), SPI-capable | 1 |
| Breadboard | 1 |
| Ethernet cable | 1 |
| USB cables for the boards (power, flashing, serial) | 2 |
| Jumper wires (F-M, F-F, M-M) | a bunch |
| Optional: USB-C to Ethernet adapter | 1 |

Double the amount would be nice for sub-group testing.

## Where it connects

- The display and the BME68x are wired to **Board B** only. See [Architecture](architecture.md#wiring-responder) for the pins.
- Both boards are linked by the Ethernet cable and each needs its own USB cable for power, flashing and serial. See [Getting Started](getting-started.md).
