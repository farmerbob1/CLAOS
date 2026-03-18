/*
 * CLAOS — Claude Assisted Operating System
 * udp.h — User Datagram Protocol
 */

#ifndef CLAOS_UDP_H
#define CLAOS_UDP_H

#include "types.h"

/* UDP header */
struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;        /* Header + data length */
    uint16_t checksum;
} __attribute__((packed));

/* Send a UDP packet */
bool udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const void* data, uint16_t data_len);

/* Process a received UDP packet */
void udp_receive(uint32_t src_ip, const void* data, uint16_t length);

/* Register a callback for a specific UDP port */
typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port,
                               const void* data, uint16_t length);
void udp_bind(uint16_t port, udp_handler_t handler);

#endif /* CLAOS_UDP_H */
