#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "gbb.h"
#include "protocol.h"
#include "transport_udp.h"

#define SPI_NODE DT_NODELABEL(spi1)

static const struct device *const spi = DEVICE_DT_GET(SPI_NODE);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(spi_err_led), gpios);

/* Large enough for the largest GBB datagram */
static uint8_t buf[GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD)];

int main(void)
{
	int sock;

	if (!device_is_ready(spi) || !gpio_is_ready_dt(&err_led)) {
		printk("Responder init failed: SPI1 or GPIOs not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&err_led, GPIO_OUTPUT_INACTIVE) < 0) {
		printk("Responder init failed: GPIO configuration\n");
		return 0;
	}

	sock = transport_open(PROTOCOL_UDP_PORT, 0);
	if (sock < 0) {
		printk("Responder init failed: socket (%d)\n", sock);
		return 0;
	}

	printk("Responder up on %s:%d, SPI1 with %d CS lines\n", CONFIG_NET_CONFIG_MY_IPV4_ADDR,
	       PROTOCOL_UDP_PORT, DT_PROP_LEN(SPI_NODE, cs_gpios));

	/* Temporary echo test (task 3), replaced by GBB handling in task 4 */
	for (;;) {
		struct net_sockaddr_in from;
		int len = transport_recv(sock, &from, buf, sizeof(buf));

		if (len < 0) {
			printk("receive failed (%d)\n", len);
			continue;
		}

		int ret = transport_send(sock, &from, buf, len);

		if (ret < 0) {
			printk("echo failed (%d)\n", ret);
		}
	}
}
