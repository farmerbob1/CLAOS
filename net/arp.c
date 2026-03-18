/*
 * CLAOS — Claude Assisted Operating System
 * arp.c — Address Resolution Protocol
 *
 * Resolves IPv4 addresses to Ethernet MAC addresses. We maintain a
 * small static cache. When we need a MAC we don't have, we send an
 * ARP request and wait for the reply.
 *
 * For CLAOS, the critical resolution is the gateway (10.0.2.2) — once
 * we have that, all outbound traffic goes through it.
 */

#include "arp.h"
#include "ethernet.h"
#include "e1000.h"
#include "net.h"
#include "string.h"
#include "io.h"
#include "timer.h"

/* ARP cache */
#define ARP_CACHE_SIZE 16
struct arp_entry {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
};
static struct arp_entry arp_cache[ARP_CACHE_SIZE];

/* Gateway MAC (resolved at init) */
static uint8_t gateway_mac[6];
static bool gateway_resolved = false;

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    gateway_resolved = false;
}

/* Look up an IP in the cache */
static struct arp_entry* arp_cache_lookup(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip)
            return &arp_cache[i];
    }
    return NULL;
}

/* Add or update an entry in the cache */
static void arp_cache_add(uint32_t ip, const uint8_t mac[6]) {
    /* Update existing entry */
    struct arp_entry* entry = arp_cache_lookup(ip);
    if (entry) {
        memcpy(entry->mac, mac, 6);
        return;
    }

    /* Find an empty slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            return;
        }
    }

    /* Cache full — overwrite first entry (simple eviction) */
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = true;
}

/* Send an ARP request for the given IP */
static void arp_send_request(uint32_t target_ip) {
    struct arp_packet pkt;
    pkt.htype = htons(1);           /* Ethernet */
    pkt.ptype = htons(0x0800);      /* IPv4 */
    pkt.hlen = 6;
    pkt.plen = 4;
    pkt.opcode = htons(ARP_REQUEST);

    e1000_get_mac(pkt.sender_mac);
    pkt.sender_ip = htonl(CLAOS_IP);

    memset(pkt.target_mac, 0, 6);   /* Unknown — that's what we're asking for */
    pkt.target_ip = htonl(target_ip);

    /* Send as broadcast */
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(broadcast, ETHERTYPE_ARP, &pkt, sizeof(pkt));
}

/* Send an ARP reply */
static void arp_send_reply(uint32_t target_ip, const uint8_t target_mac[6]) {
    struct arp_packet pkt;
    pkt.htype = htons(1);
    pkt.ptype = htons(0x0800);
    pkt.hlen = 6;
    pkt.plen = 4;
    pkt.opcode = htons(ARP_REPLY);

    e1000_get_mac(pkt.sender_mac);
    pkt.sender_ip = htonl(CLAOS_IP);

    memcpy(pkt.target_mac, target_mac, 6);
    pkt.target_ip = htonl(target_ip);

    eth_send(target_mac, ETHERTYPE_ARP, &pkt, sizeof(pkt));
}

void arp_receive(const void* data, uint16_t length) {
    if (length < sizeof(struct arp_packet)) return;

    const struct arp_packet* pkt = (const struct arp_packet*)data;

    /* Only handle Ethernet/IPv4 ARP */
    if (ntohs(pkt->htype) != 1 || ntohs(pkt->ptype) != 0x0800)
        return;

    uint32_t sender_ip = ntohl(pkt->sender_ip);
    uint32_t target_ip = ntohl(pkt->target_ip);

    /* Always learn from ARP packets we see */
    arp_cache_add(sender_ip, pkt->sender_mac);

    /* Check if gateway MAC was resolved */
    if (sender_ip == CLAOS_GW && !gateway_resolved) {
        memcpy(gateway_mac, pkt->sender_mac, 6);
        gateway_resolved = true;
        serial_print("[ARP] Gateway MAC resolved\n");
    }

    /* If this is a request for our IP, send a reply */
    if (ntohs(pkt->opcode) == ARP_REQUEST && target_ip == CLAOS_IP) {
        arp_send_reply(sender_ip, pkt->sender_mac);
    }
}

bool arp_resolve(uint32_t ip, uint8_t mac_out[6]) {
    /* Check cache first */
    struct arp_entry* entry = arp_cache_lookup(ip);
    if (entry) {
        memcpy(mac_out, entry->mac, 6);
        return true;
    }

    /* Send ARP request and wait for reply */
    arp_send_request(ip);

    /* Poll for up to 3 seconds */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 300) {  /* 300 ticks = 3 seconds at 100Hz */
        net_poll();
        entry = arp_cache_lookup(ip);
        if (entry) {
            memcpy(mac_out, entry->mac, 6);
            return true;
        }
    }

    serial_print("[ARP] Resolution timeout\n");
    return false;
}

bool arp_get_gateway_mac(uint8_t mac_out[6]) {
    if (gateway_resolved) {
        memcpy(mac_out, gateway_mac, 6);
        return true;
    }

    /* Try to resolve the gateway */
    return arp_resolve(CLAOS_GW, mac_out);
}
