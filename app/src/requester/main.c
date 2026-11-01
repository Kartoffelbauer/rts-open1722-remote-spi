#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "protocol.h"
#include "transport_udp.h"

static const struct gpio_dt_spec ok_led = GPIO_DT_SPEC_GET(DT_ALIAS(link_ok_led), gpios);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(link_err_led), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(page_button), gpios);
static struct gpio_callback button_cb;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* Placeholder until Display Pages exist (task 7) */
	printk("button pressed\n");
}

/* Sends one ping and waits for its echo. Returns 0, -EAGAIN on timeout or a negative errno. */
static int ping(int sock, const struct net_sockaddr_in *responder, uint32_t seq)
{
	char tx[32];
	char rx[32];
	int len = snprintk(tx, sizeof(tx), "ping %u", seq);
	int ret = transport_send(sock, responder, tx, len);

	if (ret < 0) {
		return ret;
	}

	/* Drop stale echoes of earlier pings that timed out */
	for (;;) {
		ret = transport_recv(sock, NULL, rx, sizeof(rx));
		if (ret < 0) {
			return ret;
		}
		if (ret == len && memcmp(rx, tx, len) == 0) {
			return 0;
		}
		printk("ping %u: dropped stale echo\n", seq);
	}
}

int main(void)
{
	struct net_sockaddr_in responder;
	int sock;

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

	if (transport_addr(&responder, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, PROTOCOL_UDP_PORT) < 0) {
		printk("Requester init failed: bad Responder address\n");
		return 0;
	}

	sock = transport_open(0, PROTOCOL_TIMEOUT_MS);
	if (sock < 0) {
		printk("Requester init failed: socket (%d)\n", sock);
		return 0;
	}

	printk("Requester up, pinging %s:%d\n", CONFIG_NET_CONFIG_PEER_IPV4_ADDR, PROTOCOL_UDP_PORT);

	/* Temporary echo test (task 3), replaced by GBB Requests in task 4 */
	for (uint32_t seq = 0;; seq++) {
		uint32_t start = k_cycle_get_32();
		int ret = ping(sock, &responder, seq);
		uint32_t rtt_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);

		if (ret == 0) {
			gpio_pin_toggle_dt(&ok_led);
			gpio_pin_set_dt(&err_led, 0);
			printk("ping %u: %u us\n", seq, rtt_us);
		} else {
			gpio_pin_set_dt(&err_led, 1);
			printk("ping %u: %s (%d)\n", seq, ret == -EAGAIN ? "timeout" : "error", ret);
		}

		k_msleep(1000);
	}
}
