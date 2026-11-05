#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(display)) {
		printk("Error: the display is not ready\n");
		return 0;
	}

	if (cfb_framebuffer_init(display) != 0) {
		printk("Error: could not prepare the drawing area\n");
		return 0;
	}

	cfb_framebuffer_clear(display, false);
	cfb_framebuffer_set_font(display, 0);
	cfb_draw_text(display, "Hello World", 17, 76);
	cfb_framebuffer_finalize(display);

	return 0;
}
