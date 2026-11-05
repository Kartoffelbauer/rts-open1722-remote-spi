#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(bme68x));
	struct sensor_value temp, humidity, pressure, gas;
	char line[20];

	if (!device_is_ready(display)) {
		printk("Error: the display is not ready\n");
		return 0;
	}

	if (!device_is_ready(sensor)) {
		printk("Error: the sensor is not ready\n");
		return 0;
	}

	if (cfb_framebuffer_init(display) != 0) {
		printk("Error: could not prepare the drawing area\n");
		return 0;
	}

	cfb_framebuffer_set_font(display, 0);

	while (1) {
		/* Take a new measurement, then read each value from it */
		if (sensor_sample_fetch(sensor) != 0) {
			printk("Error: could not read the sensor\n");
			k_sleep(K_SECONDS(2));
			continue;
		}

		sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(sensor, SENSOR_CHAN_HUMIDITY, &humidity);
		sensor_channel_get(sensor, SENSOR_CHAN_PRESS, &pressure);
		sensor_channel_get(sensor, SENSOR_CHAN_GAS_RES, &gas);

		cfb_framebuffer_clear(display, false);
		cfb_draw_text(display, "Air Quality", 17, 20);

		snprintf(line, sizeof(line), "T: %.2f C", sensor_value_to_double(&temp));
		cfb_draw_text(display, line, 2, 52);

		snprintf(line, sizeof(line), "H: %.2f %%", sensor_value_to_double(&humidity));
		cfb_draw_text(display, line, 2, 76);

		snprintf(line, sizeof(line), "P: %.2f kPa", sensor_value_to_double(&pressure));
		cfb_draw_text(display, line, 2, 100);

		/* Gas resistance in ohms: higher means cleaner air */
		snprintf(line, sizeof(line), "G: %d Ohm", gas.val1);
		cfb_draw_text(display, line, 2, 124);

		cfb_framebuffer_finalize(display);

		/* Update the screen every 2 seconds */
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
