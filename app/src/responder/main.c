#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "protocol.h"

#define SPI_NODE  DT_NODELABEL(spi1)
#define USER_NODE DT_PATH(zephyr_user)

static const struct device *const spi = DEVICE_DT_GET(SPI_NODE);
static const struct gpio_dt_spec oled_dc = GPIO_DT_SPEC_GET(USER_NODE, oled_dc_gpios);
static const struct gpio_dt_spec oled_res = GPIO_DT_SPEC_GET(USER_NODE, oled_res_gpios);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(spi_err_led), gpios);

int main(void)
{
	if (!device_is_ready(spi) || !gpio_is_ready_dt(&oled_dc) || !gpio_is_ready_dt(&oled_res) ||
	    !gpio_is_ready_dt(&err_led)) {
		printk("Responder init failed: SPI1 or GPIOs not ready\n");
		return 0;
	}

	/* RES starts asserted, so the OLED is held in reset until the pulse ends */
	if (gpio_pin_configure_dt(&err_led, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&oled_dc, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&oled_res, GPIO_OUTPUT_ACTIVE) < 0) {
		printk("Responder init failed: GPIO configuration\n");
		return 0;
	}

	/* Pulse the OLED's RES line once; the Requester sends the init sequence */
	k_msleep(1);
	gpio_pin_set_dt(&oled_res, 0);

	printk("Responder up on port %d, SPI1 with %d CS lines, OLED reset done\n",
	       PROTOCOL_UDP_PORT, DT_PROP_LEN(SPI_NODE, cs_gpios));
	return 0;
}
