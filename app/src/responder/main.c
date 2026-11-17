#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "gbb.h"
#include "protocol.h"
#include "transport_udp.h"

#define SPI_NODE DT_NODELABEL(spi1)

/* Payload bytes printed per request, the rest is only counted */
#define LOG_MAX_BYTES 32

static const struct device *const spi = DEVICE_DT_GET(SPI_NODE);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(spi_err_led), gpios);

/* Large enough for the largest GBB datagram */
static uint8_t buf[GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD)];

static void log_request(const struct gbb_msg *req)
{
	if (req->op == GBB_OP_WRITE) {
		printk("Write Request %u: Byte Bus %u, segment_num %u, ms %d\n", req->transaction_num,
		       req->byte_bus_id, req->read_size_segment_num, req->ms);
	} else {
		printk("Read Request %u: Byte Bus %u, read_size 0x%03x (%u bytes), ms %d\n",
		       req->transaction_num, req->byte_bus_id, req->read_size_segment_num,
		       (unsigned int)gbb_read_size_bytes(req->read_size_segment_num), req->ms);
	}

	printk("  %u payload bytes:", (unsigned int)req->payload_len);
	for (size_t i = 0; i < MIN(req->payload_len, LOG_MAX_BYTES); i++) {
		printk(" %02x", req->payload[i]);
	}
	if (req->payload_len > LOG_MAX_BYTES) {
		printk(" ... (%u more)", (unsigned int)(req->payload_len - LOG_MAX_BYTES));
	}
	printk("\n");
}

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

	/*
	 * Task 4: parse and log each GBB Request. The SPI transfer and the GBB
	 * Response follow in task 5.
	 */
	for (;;) {
		struct gbb_msg req;
		int len = transport_recv(sock, NULL, buf, sizeof(buf));

		if (len < 0) {
			printk("receive failed (%d)\n", len);
			continue;
		}

		if (gbb_parse(buf, len, &req) < 0) {
			printk("dropped %d-byte datagram: not one GBB message in NTSCF v0\n", len);
			continue;
		}

		if (req.rsp) {
			printk("dropped GBB Response %u: the Responder only takes requests\n",
			       req.transaction_num);
			continue;
		}

		log_request(&req);
	}
}
