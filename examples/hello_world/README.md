# Hello World example

> **Note:** this is test code for task 2 (local test on Board B) of the [Project Plan](../../docs/project-plan.md), not the final app. It drives a display directly over SPI, with no network and no GBB. The real project code lives in [`app/`](../../app).

Standalone Zephyr app that prints "Hello World" on an Adafruit SHARP Memory Display 1.3" (LS013B7DH05) wired directly to SPI1 of a NUCLEO-F439ZI. Wiring is listed in [`app.overlay`](app.overlay).

Build and flash from the workspace folder in the dev container:

```bash
west build -p always -b nucleo_f439zi examples/hello_world -d build/hello_world
west flash -d build/hello_world
```
