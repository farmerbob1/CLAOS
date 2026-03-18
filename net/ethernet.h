/*
 * CLAOS — Claude Assisted Operating System
 * ethernet.h — Ethernet Frame Layer
 */

#ifndef CLAOS_ETHERNET_H
#define CLAOS_ETHERNET_H

#include "types.h"

/* EtherType values */
#define ETHERTYPE_IPV4  0x0800
#define ETHERTYPE_ARP   0x0806

/* Ethernet frame header */
struct eth_header {
    uint8_t  dest[6];       /* Destination MAC */
    uint8_t  src[6];        /* Source MAC */
    uint16_t ethertype;     /* Protocol type (network byte order) */
} __attribute__((packed));

#define ETH_BROADCAST ((uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF})

/* Send an Ethernet frame with the given payload */
bool eth_send(const uint8_t dest[6], uint16_t ethertype,
              const void* payload, uint16_t payload_len);

/* Process a received Ethernet frame (dispatches to ARP/IPv4 handlers) */
void eth_receive(const void* frame, uint16_t length);

/* Poll for incoming packets and process them */
void net_poll(void);

#endif /* CLAOS_ETHERNET_H */
