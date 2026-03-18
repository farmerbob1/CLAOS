/*
 * CLAOS — Claude Assisted Operating System
 * udp.c — User Datagram Protocol
 *
 * Simple connectionless transport. Used by DNS.
 */

#include "udp.h"
#include "ipv4.h"
#include "net.h"
#include "string.h"
#include "io.h"

/* Port bindings */
#define MAX_UDP_BINDINGS 8
static struct {
    uint16_t port;
    udp_handler_t handler;
} udp_bindings[MAX_UDP_BINDINGS];

static uint8_t udp_tx_buf[MTU];

void udp_bind(uint16_t port, udp_handler_t handler) {
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (udp_bindings[i].handler == NULL) {
            udp_bindings[i].port = port;
            udp_bindings[i].handler = handler;
            return;
        }
    }
}

bool udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const void* data, uint16_t data_len) {
    uint16_t total_len = sizeof(struct udp_header) + data_len;
    if (total_len > MTU) return false;

    struct udp_header* hdr = (struct udp_header*)udp_tx_buf;
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length = htons(total_len);
    hdr->checksum = 0;

    memcpy(udp_tx_buf + sizeof(struct udp_header), data, data_len);

    /* Calculate UDP checksum over pseudo-header + UDP segment */
    hdr->checksum = tcp_udp_checksum(htonl(CLAOS_IP), htonl(dst_ip),
                                      IP_PROTO_UDP, udp_tx_buf, total_len);
    if (hdr->checksum == 0) hdr->checksum = 0xFFFF;

    return ipv4_send(dst_ip, IP_PROTO_UDP, udp_tx_buf, total_len);
}

void udp_receive(uint32_t src_ip, const void* data, uint16_t length) {
    if (length < sizeof(struct udp_header)) return;

    const struct udp_header* hdr = (const struct udp_header*)data;
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t udp_len = ntohs(hdr->length);

    if (udp_len > length) return;

    const void* payload = (const uint8_t*)data + sizeof(struct udp_header);
    uint16_t payload_len = udp_len - sizeof(struct udp_header);

    /* Dispatch to registered handler */
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (udp_bindings[i].handler && udp_bindings[i].port == dst_port) {
            udp_bindings[i].handler(src_ip, src_port, payload, payload_len);
            return;
        }
    }
}
