#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "gbb.h"
#include "protocol.h"
#include "transport_udp.h"

#include "avtp/Udp.h"
#include "avtp/acf/Ntscf.h"

#define SPI_NODE  DT_NODELABEL(spi1)
#define USER_NODE DT_PATH(zephyr_user)

static const struct device *const spi = DEVICE_DT_GET(SPI_NODE);
static const struct gpio_dt_spec err_led = GPIO_DT_SPEC_GET(DT_ALIAS(spi_err_led), gpios);

/* Large enough for any GBB message (acf_msg_length is at most 2044 bytes) */
#define GBB_PAYLOAD_LEN 8

static uint8_t payload[GBB_PAYLOAD_LEN] = { 0x11, 0x22 };


typedef struct __attribute__((packed)) {
    Avtp_Udp_t udp;
    Avtp_NtscfV1_t ntscf;
    Avtp_Gbb_t gbb;
	uint8_t gbbPayload[GBB_PAYLOAD_LEN];

} My1722Pdu_t;

static uint8_t tx_buf [
	//AVTP_UDP_HEADER_LEN + 
	AVTP_NTSCF_HEADER_LEN_V1 + 
	AVTP_GBB_HEADER_LEN + 
	GBB_PAYLOAD_LEN
];

static uint8_t transaction_num = 0;
My1722Pdu_t *pdu;
//static uint16_t sensor_bytes[GBB_PAYLOAD_LEN] = { 0x11, 0x22 };
    
static void create_package() 
{
	// Create UDP header
	

	// Create NTSCF header
    uint8_t *ntscf_bytes = tx_buf;
    Avtp_NtscfV1_t *ntscf = (Avtp_NtscfV1_t *)ntscf_bytes;
    uint8_t *gbb_bytes = ntscf_bytes + AVTP_NTSCF_HEADER_LEN_V1;

	Avtp_Ntscf_Init((Avtp_Ntscf_t *)ntscf, 1);
    Avtp_Ntscf_SetSequenceNum((Avtp_Ntscf_t *)ntscf, 123);
    Avtp_Ntscf_SetStreamId((Avtp_Ntscf_t *)ntscf, 0xAABBCCDDEEFF);
	
	// Create a GBB message with the specified parameters
	Avtp_Gbb_t *gbb = (Avtp_Gbb_t *)gbb_bytes;
	Avtp_Gbb_CreateAcfMessage(gbb, BYTE_BUS_BME280, false, transaction_num, payload, GBB_PAYLOAD_LEN);

	Avtp_Ntscf_SetNtscfDataLength_V1(ntscf, AVTP_GBB_HEADER_LEN+GBB_PAYLOAD_LEN);
}
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
	
	
	/* Temporary echo test (task 3), replaced by GBB handling in task 4 

	bool p = true;

	while (p) {

		struct net_sockaddr_in from;
		int len = transport_recv(sock, &from, tx_buf, sizeof(tx_buf));

		if (len < 0) {
			printk("receive failed (%d)\n", len);
			continue; 
		}

		int ret = transport_send(sock, &from, tx_buf, len);
		if (ret >= 0) {
			printk("echo success (%d)\n", ret);
			break;
		}
	}
	*/ 	
	int len = 0;
	for (;;) 
	{		
		struct net_sockaddr_in from;

		if (len == 0) {
			len = transport_recv(sock, &from, tx_buf, sizeof(tx_buf));
		}

		if (len < 0) {
			printk("receive failed (%d)\n", len);
			continue; 
		}

		transaction_num++;
		create_package();
		int ret = transport_send(sock, &from, tx_buf, sizeof(tx_buf));
		if (ret < 0) {
			printk("echo failed (%d)\n", ret);
			continue;
		}
		printk("successful (%d)\n", transaction_num);
		k_msleep(8000);
	}
}
