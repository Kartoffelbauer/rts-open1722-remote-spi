#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zephyr/net/net_ip.h>

/* Fills addr from a dotted IPv4 string and a port. Returns 0 or -EINVAL. */
int transport_addr(struct net_sockaddr_in *addr, const char *ip, uint16_t port);

/*
 * Opens a UDP socket bound to port (0 = ephemeral). A recv_timeout_ms of 0 makes
 * transport_recv() block forever. Returns the socket or a negative errno.
 */
int transport_open(uint16_t port, int32_t recv_timeout_ms);

/* Returns 0 or a negative errno. */
int transport_send(int sock, const struct net_sockaddr_in *to, const void *buf, size_t len);

/*
 * Receives one datagram; from may be NULL. Returns its length, -EAGAIN on timeout
 * or another negative errno.
 */
int transport_recv(int sock, struct net_sockaddr_in *from, void *buf, size_t len);
