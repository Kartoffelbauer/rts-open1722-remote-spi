#include <errno.h>
#include <zephyr/net/socket.h>

#include "transport_udp.h"

int transport_addr(struct net_sockaddr_in *addr, const char *ip, uint16_t port)
{
	*addr = (struct net_sockaddr_in){
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(port),
	};

	return net_addr_pton(NET_AF_INET, ip, &addr->sin_addr);
}

int transport_open(uint16_t port, int32_t recv_timeout_ms)
{
	struct net_sockaddr_in local = {
		.sin_family = NET_AF_INET,
		.sin_port = net_htons(port),
	};
	int sock = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);

	if (sock < 0) {
		return -errno;
	}

	if (zsock_bind(sock, (struct net_sockaddr *)&local, sizeof(local)) < 0) {
		int err = -errno;

		zsock_close(sock);
		return err;
	}

	if (recv_timeout_ms > 0) {
		struct zsock_timeval tv = {
			.tv_sec = recv_timeout_ms / 1000,
			.tv_usec = (recv_timeout_ms % 1000) * 1000,
		};

		if (zsock_setsockopt(sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
			int err = -errno;

			zsock_close(sock);
			return err;
		}
	}

	return sock;
}

int transport_send(int sock, const struct net_sockaddr_in *to, const void *buf, size_t len)
{
	if (zsock_sendto(sock, buf, len, 0, (const struct net_sockaddr *)to, sizeof(*to)) < 0) {
		return -errno;
	}

	return 0;
}

int transport_recv(int sock, struct net_sockaddr_in *from, void *buf, size_t len)
{
	net_socklen_t from_len = sizeof(*from);
	ssize_t n = zsock_recvfrom(sock, buf, len, 0, (struct net_sockaddr *)from,
				   from ? &from_len : NULL);

	if (n < 0) {
		return errno == EWOULDBLOCK ? -EAGAIN : -errno;
	}

	return (int)n;
}
