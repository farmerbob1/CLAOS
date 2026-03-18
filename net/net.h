/*
 * CLAOS — Claude Assisted Operating System
 * net.h — Network configuration and shared definitions
 *
 * Hardcoded network config for QEMU's user-mode networking.
 * QEMU gives us a simple NAT environment:
 *   Guest:   10.0.2.15
 *   Gateway: 10.0.2.2 (also the host)
 *   DNS:     10.0.2.3 (QEMU's built-in DNS proxy)
 */

#ifndef CLAOS_NET_H
#define CLAOS_NET_H

#include "types.h"

/* CLAOS static network configuration */
#define CLAOS_IP_A      10
#define CLAOS_IP_B      0
#define CLAOS_IP_C      2
#define CLAOS_IP_D      15

#define CLAOS_GW_A      10
#define CLAOS_GW_B      0
#define CLAOS_GW_C      2
#define CLAOS_GW_D      2

#define CLAOS_MASK_A    255
#define CLAOS_MASK_B    255
#define CLAOS_MASK_C    255
#define CLAOS_MASK_D    0

#define CLAOS_DNS_A     10
#define CLAOS_DNS_B     0
#define CLAOS_DNS_C     2
#define CLAOS_DNS_D     3

/* Pack 4 bytes into a 32-bit IP address (network byte order = big-endian) */
#define IP_ADDR(a,b,c,d) ((uint32_t)((a)<<24|(b)<<16|(c)<<8|(d)))

/* Our IP addresses as 32-bit values */
#define CLAOS_IP    IP_ADDR(CLAOS_IP_A, CLAOS_IP_B, CLAOS_IP_C, CLAOS_IP_D)
#define CLAOS_GW    IP_ADDR(CLAOS_GW_A, CLAOS_GW_B, CLAOS_GW_C, CLAOS_GW_D)
#define CLAOS_MASK  IP_ADDR(CLAOS_MASK_A, CLAOS_MASK_B, CLAOS_MASK_C, CLAOS_MASK_D)
#define CLAOS_DNS   IP_ADDR(CLAOS_DNS_A, CLAOS_DNS_B, CLAOS_DNS_C, CLAOS_DNS_D)

/* Convert between host and network byte order (x86 is little-endian, network is big-endian) */
static inline uint16_t htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}
static inline uint16_t ntohs(uint16_t v) {
    return htons(v);
}
static inline uint32_t htonl(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}
static inline uint32_t ntohl(uint32_t v) {
    return htonl(v);
}

/* Maximum Transmission Unit */
#define MTU 1500

/* Ethernet header size */
#define ETH_HEADER_SIZE 14

/* Maximum packet buffer size */
#define MAX_PACKET_SIZE (MTU + ETH_HEADER_SIZE)

#endif /* CLAOS_NET_H */
