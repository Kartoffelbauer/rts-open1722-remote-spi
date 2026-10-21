#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

int main(void)
{
	if (!gpio_is_ready_dt(&led) || gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
		printk("LED init failed\n");
		return 0;
	}

	for (int i = 0;; i++) {
		gpio_pin_toggle_dt(&led);
		printk("tick %d\n", i);
		k_msleep(500);
	}
}