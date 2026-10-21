# Hardware

| Item | Amount |
|---|---|
| NUCLEO-F439ZI | 2 (already owned) |
| SSD1306 SPI OLED display (e.g. Waveshare, Adafruit) | 1 |
| BME280, SPI-capable, or other SPI sensor for testing purposes | 1 |
| Breadboard | 1 |
| Ethernet cable | 1 |
| USB cables for the boards (power, flashing, serial) | 2 |
| Jumper wires (F-M, F-F, M-M) | a bunch |
| Optional: USB-C to Ethernet adapter | 1 |

Double the amount would be nice for sub-group testing.

## Where it connects

- The OLED and the BME280 are wired to **Board B** only. See [Architecture](architecture.md).
- Both boards are linked by the Ethernet cable and each needs its own USB cable for power, flashing and serial. See [Getting Started](getting-started.md).