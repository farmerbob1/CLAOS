/*
 * CLAOS — Claude Assisted Operating System
 * ipv4.h — IPv4 Layer
 */

#ifndef CLAOS_IPV4_H
#define CLAOS_IPV4_H

#include "types.h"

/* IP protocols */
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

/* IPv4 header */
struct ipv4_header {
    uint8_t  version_ihl;   /* Version (4) + IHL (5 = 20 bytes) */
    uint8_t  tos;           /* Type of Service */
    uint16_t total_length;  /* Total packet length */
    uint16_t id;            /* Identification */
    uint16_t flags_offset;  /* Flags + Fragment Offset */
    uint8_t  ttl;           /* Time to Live */
    uint8_t  protocol;      /* Upper-layer protocol (TCP=6, UDP=17) */
    uint16_t checksum;      /* Header checksum */
    uint32_t src_ip;        /* Source IP (network byte order) */
    uint32_t dst_ip;        /* Destination IP (network byte order) */
} __attribute__((packed));

/* Calculate an IP/TCP/UDP checksum */
uint16_t ip_checksum(const void* data, int length);

/* Send an IPv4 packet */
bool ipv4_send(uint32_t dst_ip, uint8_t protocol,
               const void* payload, uint16_t payload_len);

/* Process a received IPv4 packet */
void ipv4_receive(const void* data, uint16_t length);

/* Calculate TCP/UDP pseudo-header checksum */
uint16_t tcp_udp_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
                          const void* data, uint16_t length);

#endif /* CLAOS_IPV4_H */
