#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "gbb.h"
#include "protocol.h"
#include "transport_udp.h"

#include "avtp/Udp.h"
#include "avtp/acf/Ntscf.h"

#define SPI_NODE  DT_NODELABEL(spi1)

static const struct device *const spi = DEVICE_DT_GET(SPI_NODE);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(spi_err_led), gpios);

/* Large enough for the largest GBB datagram */
static uint8_t buf[GBB_DATAGRAM_LEN(GBB_MAX_PAYLOAD)];

typedef struct __attribute__((packed)) {
    //Avtp_Udp_t udp;
    Avtp_NtscfV0_t ntscf;
    Avtp_Gbb_t gbb;
	uint8_t gbb_payload[8];
} My1722Pdu_t;

static My1722Pdu_t pdu;
static uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
static uint8_t transaction_num = 0;

static size_t create_package() 
{
	//Avtp_Udp_Init(&pdu.udp);
	
	Avtp_Ntscf_Init((Avtp_Ntscf_t *)&pdu.ntscf, 0);
    Avtp_Ntscf_SetSequenceNum((Avtp_Ntscf_t *)&pdu.ntscf, 123);
    Avtp_Ntscf_SetStreamId((Avtp_Ntscf_t *)&pdu.ntscf, 0xAABBCCDDEEFF);
	
	// Create GBB message with the specified parameters
	Avtp_Gbb_CreateAcfMessage(
		&pdu.gbb, BYTE_BUS_SENSOR, 
		false, transaction_num, 
		payload, sizeof(payload)
	);
	
	Avtp_Ntscf_SetNtscfDataLength_V0(
		&pdu.ntscf,
		AVTP_GBB_HEADER_LEN + sizeof(payload)
	);
	
	return sizeof(pdu);
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

	printk("Responder up on %s:%d, SPI1 with %d CS lines\n", 
		CONFIG_NET_CONFIG_MY_IPV4_ADDR,
	    PROTOCOL_UDP_PORT, 
		DT_PROP_LEN(SPI_NODE, cs_gpios)
	);
	
	int len = 0;
	for (;;) 
	{		
		struct net_sockaddr_in from;

		if (len == 0) {
			len = transport_recv(sock, &from, buf, sizeof(buf));
		}
		if (len < 0) {
			printk("receive failed (%d)\n", len);
			continue; 
		}
		printk("received package with size %zu\n", len);

		transaction_num++;
		size_t pack_len = create_package();
		printk("created package with size %zu\n", pack_len);
		int ret = transport_send(sock, &from, (uint8_t *)&pdu, pack_len);
		
		if (ret < 0) {
			printk("echo failed (%d)\n", ret);
			continue;
		}
		
		printk("sent %d bytes, transaction %u\n", ret, transaction_num);
		k_msleep(2000);
	}
}
