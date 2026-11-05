# Air Quality example

> **Note:** this is test code for task 2 (local test on Board B) of the [Project Plan](../../docs/project-plan.md), not the final app. It drives a display and a sensor directly over SPI, with no network and no GBB. The real project code lives in [`app/`](../../app).

Standalone Zephyr app that reads a BME68x sensor (BME680/BME688) and shows its temperature, humidity, pressure and gas resistance on an Adafruit SHARP Memory Display 1.3" (LS013B7DH05). Both devices share SPI1 of a NUCLEO-F439ZI and each one has its own chip select. Wiring is listed in [`app.overlay`](app.overlay).

Build and flash from the workspace folder in the dev container:

```bash
west build -p always -b nucleo_f439zi examples/air_quality -d build/air_quality
west flash -d build/air_quality
```
