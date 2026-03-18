/*
 * CLAOS — Claude Assisted Operating System
 * ethernet.c — Ethernet Frame Layer
 *
 * Constructs and parses Ethernet frames, and dispatches received
 * frames to the appropriate protocol handler (ARP or IPv4).
 */

#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "e1000.h"
#include "net.h"
#include "string.h"
#include "io.h"

/* Packet buffer for building outgoing frames */
static uint8_t eth_tx_buf[MAX_PACKET_SIZE];

/* Packet buffer for receiving frames */
static uint8_t eth_rx_buf[MAX_PACKET_SIZE];

bool eth_send(const uint8_t dest[6], uint16_t ethertype,
              const void* payload, uint16_t payload_len) {
    if (payload_len + ETH_HEADER_SIZE > MAX_PACKET_SIZE)
        return false;

    struct eth_header* hdr = (struct eth_header*)eth_tx_buf;

    /* Fill in the Ethernet header */
    memcpy(hdr->dest, dest, 6);
    e1000_get_mac(hdr->src);
    hdr->ethertype = htons(ethertype);

    /* Copy the payload after the header */
    memcpy(eth_tx_buf + ETH_HEADER_SIZE, payload, payload_len);

    /* Send it */
    return e1000_send(eth_tx_buf, ETH_HEADER_SIZE + payload_len);
}

void eth_receive(const void* frame, uint16_t length) {
    if (length < ETH_HEADER_SIZE) return;

    const struct eth_header* hdr = (const struct eth_header*)frame;
    const uint8_t* payload = (const uint8_t*)frame + ETH_HEADER_SIZE;
    uint16_t payload_len = length - ETH_HEADER_SIZE;
    uint16_t ethertype = ntohs(hdr->ethertype);

    switch (ethertype) {
        case ETHERTYPE_ARP:
            arp_receive(payload, payload_len);
            break;
        case ETHERTYPE_IPV4:
            ipv4_receive(payload, payload_len);
            break;
        default:
            break;  /* Ignore unknown protocols */
    }
}

void net_poll(void) {
    /* Check for incoming packets */
    uint16_t len = e1000_receive(eth_rx_buf, sizeof(eth_rx_buf));
    if (len > 0) {
        eth_receive(eth_rx_buf, len);
    }
}
