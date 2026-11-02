# Cookie Clicker example

> **Note:** this is a demo app, not the final app. It drives a display directly over SPI, with no network and no GBB. The real project code lives in [`app/`](../../app).

> **AI disclaimer:** this example was generated entirely by AI and is just there for demonstration purposes.

Standalone Zephyr app that runs a small cookie clicker game on an Adafruit SHARP Memory Display 1.3" (LS013B7DH05) wired directly to SPI1 of a NUCLEO-F439ZI. Wiring is listed in [`app.overlay`](app.overlay).

Controls use the board's user button: tap to bake cookies, hold to open the shop (the highlight cycles through the buildings, release to buy). Tap while a golden cookie is showing to start a x7 frenzy. LED0 shows taps, purchases, frenzies and whether something in the shop is affordable.

Build and flash from the workspace folder in the dev container:

```bash
west build -p always -b nucleo_f439zi examples/cookie_clicker -d build/cookie_clicker
west flash -d build/cookie_clicker
```
