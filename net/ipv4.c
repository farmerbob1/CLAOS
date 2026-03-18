/*
 * CLAOS — Claude Assisted Operating System
 * ipv4.c — IPv4 Layer
 *
 * Constructs and parses IPv4 packets. Handles routing (all non-local
 * traffic goes through the gateway). Dispatches to TCP/UDP handlers.
 */

#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include "net.h"
#include "string.h"
#include "io.h"

/* Packet ID counter */
static uint16_t ip_id_counter = 1;

/* IP header + payload buffer */
static uint8_t ipv4_tx_buf[MAX_PACKET_SIZE];

uint16_t ip_checksum(const void* data, int length) {
    const uint16_t* ptr = (const uint16_t*)data;
    uint32_t sum = 0;

    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    /* Handle odd byte */
    if (length == 1) {
        sum += *(const uint8_t*)ptr;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

uint16_t tcp_udp_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
                          const void* data, uint16_t length) {
    /*
     * TCP/UDP checksum is computed over a pseudo-header + the segment data.
     * All values in the pseudo-header must be in network byte order.
     * src_ip and dst_ip are already in network byte order (caller uses htonl).
     *
     * Pseudo-header: src_ip(4) + dst_ip(4) + zero(1) + protocol(1) + length(2)
     */
    uint32_t sum = 0;

    /* Sum source IP (two 16-bit words) */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;

    /* Sum destination IP (two 16-bit words) */
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;

    /* Protocol + zero byte = (0x00 << 8 | protocol) in network byte order */
    sum += htons((uint16_t)protocol);

    /* Length in network byte order */
    sum += htons(length);

    /* Sum the actual data (TCP or UDP header + payload) */
    const uint16_t* ptr = (const uint16_t*)data;
    int remaining = length;
    while (remaining > 1) {
        sum += *ptr++;
        remaining -= 2;
    }
    if (remaining == 1) {
        sum += *(const uint8_t*)ptr;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

bool ipv4_send(uint32_t dst_ip, uint8_t protocol,
               const void* payload, uint16_t payload_len) {
    uint16_t total_len = sizeof(struct ipv4_header) + payload_len;
    if (total_len > MAX_PACKET_SIZE) return false;

    /* Build IP header */
    struct ipv4_header* hdr = (struct ipv4_header*)ipv4_tx_buf;
    hdr->version_ihl = 0x45;   /* IPv4, 20-byte header (IHL=5) */
    hdr->tos = 0;
    hdr->total_length = htons(total_len);
    hdr->id = htons(ip_id_counter++);
    hdr->flags_offset = htons(0x4000);  /* Don't Fragment */
    hdr->ttl = 64;
    hdr->protocol = protocol;
    hdr->checksum = 0;
    hdr->src_ip = htonl(CLAOS_IP);
    hdr->dst_ip = htonl(dst_ip);

    /* Calculate header checksum */
    hdr->checksum = ip_checksum(hdr, sizeof(struct ipv4_header));

    /* Copy payload after header */
    memcpy(ipv4_tx_buf + sizeof(struct ipv4_header), payload, payload_len);

    /* In QEMU's user-mode networking, ALL traffic goes through the gateway.
     * Even "local" subnet IPs like 10.0.2.3 (DNS) are virtual — the gateway
     * handles routing for all of them. Always use the gateway MAC. */
    uint32_t next_hop = CLAOS_GW;

    uint8_t dest_mac[6];
    if (!arp_resolve(next_hop, dest_mac)) {
        serial_print("[IPv4] ARP failed for next hop\n");
        return false;
    }

    return eth_send(dest_mac, ETHERTYPE_IPV4, ipv4_tx_buf, total_len);
}

void ipv4_receive(const void* data, uint16_t length) {
    if (length < sizeof(struct ipv4_header)) return;

    const struct ipv4_header* hdr = (const struct ipv4_header*)data;

    /* Basic validation */
    if ((hdr->version_ihl >> 4) != 4) return;  /* Must be IPv4 */

    uint16_t ihl = (hdr->version_ihl & 0x0F) * 4;
    uint16_t total_len = ntohs(hdr->total_length);
    if (total_len > length) return;

    /* Check if this packet is for us (or broadcast) */
    uint32_t dst = ntohl(hdr->dst_ip);
    if (dst != CLAOS_IP && dst != 0xFFFFFFFF)
        return;  /* Not for us */

    const uint8_t* payload = (const uint8_t*)data + ihl;
    uint16_t payload_len = total_len - ihl;

    serial_print("[IPv4] RX proto=");
    char hex[] = "0123456789ABCDEF";
    serial_putchar(hex[(hdr->protocol >> 4) & 0xF]);
    serial_putchar(hex[hdr->protocol & 0xF]);
    serial_print(" len=");
    char buf[8]; int bpos = 0; int bv = payload_len;
    if (bv == 0) buf[bpos++] = '0';
    else { while(bv > 0) { buf[bpos++] = '0' + bv%10; bv/=10; } }
    for (int j = bpos-1; j >= 0; j--) serial_putchar(buf[j]);
    serial_putchar('\n');

    /* Dispatch by protocol */
    switch (hdr->protocol) {
        case IP_PROTO_UDP:
            udp_receive(ntohl(hdr->src_ip), payload, payload_len);
            break;
        case IP_PROTO_TCP:
            tcp_receive(ntohl(hdr->src_ip), payload, payload_len);
            break;
        default:
            break;
    }
}
