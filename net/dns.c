/*
 * CLAOS — Claude Assisted Operating System
 * dns.c — DNS Resolver
 *
 * Minimal DNS client over UDP. Sends A record queries to the configured
 * DNS server (QEMU's 10.0.2.3) and parses the response to extract
 * the IPv4 address.
 *
 * DNS packet format:
 *   - 12-byte header (ID, flags, counts)
 *   - Question section (encoded hostname + type + class)
 *   - Answer section (in response)
 */

#include "dns.h"
#include "udp.h"
#include "ethernet.h"
#include "net.h"
#include "string.h"
#include "io.h"
#include "timer.h"

/* DNS port */
#define DNS_PORT 53

/* Our ephemeral port for DNS queries */
#define DNS_LOCAL_PORT 10053

/* DNS header */
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;   /* Number of questions */
    uint16_t ancount;   /* Number of answers */
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

/* DNS query types */
#define DNS_TYPE_A     1    /* IPv4 address */
#define DNS_CLASS_IN   1    /* Internet */

/* Resolved IP (set by the UDP callback) */
static volatile uint32_t resolved_ip = 0;
static volatile bool dns_response_received = false;
static uint16_t dns_query_id = 0x1234;

/* Encode a hostname in DNS wire format.
 * "api.anthropic.com" → "\x03api\x09anthropic\x03com\x00"
 * Returns the encoded length. */
static int dns_encode_name(const char* hostname, uint8_t* buf) {
    int pos = 0;
    const char* start = hostname;

    while (*hostname) {
        if (*hostname == '.') {
            int len = hostname - start;
            buf[pos++] = (uint8_t)len;
            memcpy(&buf[pos], start, len);
            pos += len;
            start = hostname + 1;
        }
        hostname++;
    }

    /* Last label */
    int len = hostname - start;
    if (len > 0) {
        buf[pos++] = (uint8_t)len;
        memcpy(&buf[pos], start, len);
        pos += len;
    }

    buf[pos++] = 0;    /* Root label (end of name) */
    return pos;
}

/* Handle DNS response */
static void dns_udp_handler(uint32_t src_ip, uint16_t src_port,
                             const void* data, uint16_t length) {
    (void)src_ip;
    (void)src_port;

    if (length < sizeof(struct dns_header)) return;

    const struct dns_header* hdr = (const struct dns_header*)data;

    /* Check it's a response to our query */
    if (ntohs(hdr->id) != dns_query_id) return;

    /* Check for errors (RCODE in low 4 bits of flags) */
    uint16_t flags = ntohs(hdr->flags);
    if ((flags & 0x000F) != 0) {
        serial_print("[DNS] Error in response\n");
        dns_response_received = true;
        return;
    }

    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) {
        serial_print("[DNS] No answers\n");
        dns_response_received = true;
        return;
    }

    /* Skip the question section to reach answers.
     * The question section starts right after the 12-byte header. */
    const uint8_t* ptr = (const uint8_t*)data + sizeof(struct dns_header);
    const uint8_t* end = (const uint8_t*)data + length;

    /* Skip question name (labels until we hit a 0 byte or pointer) */
    uint16_t qdcount = ntohs(hdr->qdcount);
    for (int q = 0; q < qdcount && ptr < end; q++) {
        while (ptr < end && *ptr != 0) {
            if ((*ptr & 0xC0) == 0xC0) {
                ptr += 2;   /* Compressed name pointer */
                goto skip_qtype;
            }
            ptr += *ptr + 1;    /* Skip label */
        }
        if (ptr < end) ptr++;   /* Skip the null terminator */
skip_qtype:
        ptr += 4;               /* Skip QTYPE (2) + QCLASS (2) */
    }

    /* Parse answer records */
    for (int a = 0; a < ancount && ptr + 12 <= end; a++) {
        /* Skip name (may be compressed) */
        if ((*ptr & 0xC0) == 0xC0) {
            ptr += 2;   /* Compressed pointer */
        } else {
            while (ptr < end && *ptr != 0) ptr += *ptr + 1;
            if (ptr < end) ptr++;
        }

        if (ptr + 10 > end) break;

        uint16_t rtype = (ptr[0] << 8) | ptr[1];
        /* uint16_t rclass = (ptr[2] << 8) | ptr[3]; */
        /* uint32_t ttl */
        uint16_t rdlength = (ptr[8] << 8) | ptr[9];
        ptr += 10;

        if (ptr + rdlength > end) break;

        /* A record: type=1, rdlength=4 (IPv4 address) */
        if (rtype == DNS_TYPE_A && rdlength == 4) {
            resolved_ip = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
                          ((uint32_t)ptr[2] << 8)  | (uint32_t)ptr[3];
            dns_response_received = true;
            serial_print("[DNS] Resolved to ");
            char ip_str[16];
            int pos = 0;
            for (int i = 0; i < 4; i++) {
                if (i > 0) ip_str[pos++] = '.';
                uint8_t octet = (resolved_ip >> (24 - i * 8)) & 0xFF;
                if (octet >= 100) ip_str[pos++] = '0' + octet / 100;
                if (octet >= 10)  ip_str[pos++] = '0' + (octet / 10) % 10;
                ip_str[pos++] = '0' + octet % 10;
            }
            ip_str[pos] = '\0';
            serial_print(ip_str);
            serial_putchar('\n');
            return;
        }

        ptr += rdlength;
    }

    dns_response_received = true;
}

void dns_init(void) {
    udp_bind(DNS_LOCAL_PORT, dns_udp_handler);
}

uint32_t dns_resolve(const char* hostname) {
    resolved_ip = 0;
    dns_response_received = false;
    dns_query_id++;

    /* Build DNS query packet */
    uint8_t query[256];
    int pos = 0;

    /* Header */
    struct dns_header* hdr = (struct dns_header*)query;
    hdr->id = htons(dns_query_id);
    hdr->flags = htons(0x0100);     /* Standard query, recursion desired */
    hdr->qdcount = htons(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;
    pos += sizeof(struct dns_header);

    /* Question: encode hostname */
    pos += dns_encode_name(hostname, query + pos);

    /* QTYPE = A (1), QCLASS = IN (1) */
    query[pos++] = 0; query[pos++] = DNS_TYPE_A;
    query[pos++] = 0; query[pos++] = DNS_CLASS_IN;

    /* Send query to DNS server */
    serial_print("[DNS] Resolving: ");
    serial_print(hostname);
    serial_print(" (");
    /* Print query size */
    char sz[8]; int szi = 0;
    int tmp = pos;
    if (tmp == 0) sz[szi++] = '0';
    else { while(tmp > 0) { sz[szi++] = '0' + tmp%10; tmp/=10; } }
    for (int j = szi-1; j >= 0; j--) serial_putchar(sz[j]);
    serial_print(" bytes)\n");

    bool sent = udp_send(CLAOS_DNS, DNS_LOCAL_PORT, DNS_PORT, query, pos);
    serial_print("[DNS] UDP send: ");
    serial_print(sent ? "OK" : "FAILED");
    serial_putchar('\n');

    /* Wait for response (up to 5 seconds) */
    uint32_t start = timer_get_ticks();
    while (!dns_response_received && (timer_get_ticks() - start) < 500) {
        if (net_bg_poll_active())
            __asm__ volatile ("hlt");
        else
            net_poll();
    }

    if (!dns_response_received) {
        serial_print("[DNS] Timeout\n");
        return 0;
    }

    return resolved_ip;
}
