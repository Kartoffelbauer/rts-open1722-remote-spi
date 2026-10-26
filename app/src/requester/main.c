#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "protocol.h"

static const struct gpio_dt_spec ok_led = GPIO_DT_SPEC_GET(DT_ALIAS(link_ok_led), gpios);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(link_err_led), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(page_button), gpios);
static struct gpio_callback button_cb;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* Placeholder until Display Pages exist (task 7) */
	gpio_pin_toggle_dt(&err_led);
	printk("button pressed\n");
}

int main(void)
{
	if (!gpio_is_ready_dt(&ok_led) || !gpio_is_ready_dt(&err_led) || !gpio_is_ready_dt(&button)) {
		printk("Requester init failed: GPIOs not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&ok_led, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&err_led, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&button, GPIO_INPUT) < 0 ||
	    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE) < 0) {
		printk("Requester init failed: GPIO configuration\n");
		return 0;
	}

	gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb);

	printk("Requester up, Responder port %d\n", PROTOCOL_UDP_PORT);

	/* Placeholder heartbeat until GBB Responses drive this LED (task 4) */
	for (;;) {
		gpio_pin_toggle_dt(&ok_led);
		k_msleep(500);
	}
}
