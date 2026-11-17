#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "gbb.h"
#include "protocol.h"
#include "transport_udp.h"

static const struct gpio_dt_spec ok_led = GPIO_DT_SPEC_GET(DT_ALIAS(link_ok_led), gpios);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(link_err_led), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(page_button), gpios);
static struct gpio_callback button_cb;

/* Test payload for task 4: 9 bytes, so the message also carries 3 pad bytes */
static const uint8_t test_payload[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x55};

static uint8_t tx_buf[GBB_DATAGRAM_LEN(sizeof(test_payload))];
static struct gbb_stream stream;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* Placeholder until Display Pages exist (task 7) */
	printk("button pressed\n");
}

/* Builds and sends one Write Request with the test payload. Returns 0 or a negative errno. */
static int send_write_request(int sock, const struct net_sockaddr_in *responder)
{
	struct gbb_msg req;
	int len = gbb_build_write_request(&stream, BYTE_BUS_DISPLAY, test_payload,
					  sizeof(test_payload), tx_buf, sizeof(tx_buf), &req);

	if (len < 0) {
		printk("Write Request not built (%d)\n", len);
		return len;
	}

	int ret = transport_send(sock, responder, tx_buf, len);

	if (ret < 0) {
		printk("Write Request %u not sent (%d)\n", req.transaction_num, ret);
		return ret;
	}

	printk("Write Request %u sent: Byte Bus %u, %u payload bytes, %d-byte datagram\n",
	       req.transaction_num, req.byte_bus_id, (unsigned int)req.payload_len, len);
	return 0;
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

	stream.stream_id = transport_stream_id(PROTOCOL_STREAM_UNIQUE_ID);

	printk("Requester up, sending to %s:%d, stream_id %08x%08x\n",
	       CONFIG_NET_CONFIG_PEER_IPV4_ADDR, PROTOCOL_UDP_PORT,
	       (uint32_t)(stream.stream_id >> 32), (uint32_t)stream.stream_id);

	/*
	 * Task 4: one Write Request per second, logged by the Responder. Responses,
	 * matching and the status LEDs follow with the Responder side in task 5.
	 */
	for (;;) {
		int ret = send_write_request(sock, &responder);

		gpio_pin_set_dt(&err_led, ret < 0);
		k_msleep(1000);
	}
}
