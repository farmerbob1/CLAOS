/*
 * CLAOS — Claude Assisted Operating System
 * arp.h — Address Resolution Protocol
 */

#ifndef CLAOS_ARP_H
#define CLAOS_ARP_H

#include "types.h"

/* ARP opcodes */
#define ARP_REQUEST  1
#define ARP_REPLY    2

/* ARP packet for Ethernet + IPv4 */
struct arp_packet {
    uint16_t htype;         /* Hardware type (1 = Ethernet) */
    uint16_t ptype;         /* Protocol type (0x0800 = IPv4) */
    uint8_t  hlen;          /* Hardware address length (6) */
    uint8_t  plen;          /* Protocol address length (4) */
    uint16_t opcode;        /* 1 = request, 2 = reply */
    uint8_t  sender_mac[6]; /* Sender hardware address */
    uint32_t sender_ip;     /* Sender protocol address */
    uint8_t  target_mac[6]; /* Target hardware address */
    uint32_t target_ip;     /* Target protocol address */
} __attribute__((packed));

/* Initialize the ARP subsystem */
void arp_init(void);

/* Process a received ARP packet */
void arp_receive(const void* data, uint16_t length);

/* Resolve an IP address to a MAC address.
 * Returns true if resolved (may block while waiting for reply).
 * Stores the MAC in `mac_out`. */
bool arp_resolve(uint32_t ip, uint8_t mac_out[6]);

/* Get the gateway MAC (resolved at boot) */
bool arp_get_gateway_mac(uint8_t mac_out[6]);

#endif /* CLAOS_ARP_H */
