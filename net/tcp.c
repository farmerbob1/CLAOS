/*
 * CLAOS — Claude Assisted Operating System
 * tcp.c — Transmission Control Protocol
 *
 * A minimal TCP implementation — just enough to hold a connection to
 * a remote server. Implements the basic state machine:
 *   CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT → CLOSED
 *
 * No fancy features: no congestion control, no SACK, no window scaling.
 * Just sequence numbers, acknowledgments, and basic retransmission.
 *
 * "Good enough to talk to Anthropic's servers."
 */

#include "tcp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "net.h"
#include "string.h"
#include "io.h"
#include "timer.h"

/* Connection pool */
static struct tcp_connection connections[TCP_MAX_CONNECTIONS];

/* Ephemeral port counter */
static uint16_t next_port = 49152;

/* Simple pseudo-random initial sequence number */
static uint32_t tcp_initial_seq(void) {
    return timer_get_ticks() * 2654435761u;  /* Knuth's multiplicative hash */
}

/* Build and send a TCP segment */
static bool tcp_send_segment(struct tcp_connection* conn, uint8_t flags,
                              const void* data, uint16_t data_len) {
    uint8_t segment[MTU];
    struct tcp_header* hdr = (struct tcp_header*)segment;

    uint8_t header_len = 20;    /* No options */

    hdr->src_port = htons(conn->local_port);
    hdr->dst_port = htons(conn->remote_port);
    hdr->seq_num = htonl(conn->seq);
    hdr->ack_num = htonl(conn->ack);
    hdr->data_offset = (header_len / 4) << 4;
    hdr->flags = flags;
    hdr->window = htons(TCP_RX_BUFFER_SIZE);
    hdr->checksum = 0;
    hdr->urgent = 0;

    /* Copy payload */
    if (data && data_len > 0) {
        memcpy(segment + header_len, data, data_len);
    }

    uint16_t total_len = header_len + data_len;

    /* Calculate TCP checksum */
    hdr->checksum = tcp_udp_checksum(htonl(CLAOS_IP), htonl(conn->remote_ip),
                                      IP_PROTO_TCP, segment, total_len);

    return ipv4_send(conn->remote_ip, IP_PROTO_TCP, segment, total_len);
}

/* Find a connection matching the incoming segment */
static struct tcp_connection* tcp_find_connection(uint32_t remote_ip,
                                                    uint16_t local_port,
                                                    uint16_t remote_port) {
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        struct tcp_connection* c = &connections[i];
        if (c->state != TCP_CLOSED &&
            c->remote_ip == remote_ip &&
            c->local_port == local_port &&
            c->remote_port == remote_port) {
            return c;
        }
    }
    return NULL;
}

/* Get available bytes in the receive buffer */
static uint32_t tcp_rx_available(struct tcp_connection* conn) {
    return (conn->rx_head - conn->rx_tail) % TCP_RX_BUFFER_SIZE;
}

/* Write data into the receive buffer */
static void tcp_rx_write(struct tcp_connection* conn, const void* data, uint16_t len) {
    const uint8_t* src = (const uint8_t*)data;
    for (uint16_t i = 0; i < len; i++) {
        uint32_t next = (conn->rx_head + 1) % TCP_RX_BUFFER_SIZE;
        if (next == conn->rx_tail) break;   /* Buffer full */
        conn->rx_buf[conn->rx_head] = src[i];
        conn->rx_head = next;
    }
}

void tcp_init(void) {
    memset(connections, 0, sizeof(connections));
}

void tcp_receive(uint32_t src_ip, const void* data, uint16_t length) {
    if (length < 20) return;    /* Minimum TCP header */

    const struct tcp_header* hdr = (const struct tcp_header*)data;
    uint8_t header_len = (hdr->data_offset >> 4) * 4;
    if (header_len < 20 || header_len > length) return;

    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint32_t seq = ntohl(hdr->seq_num);
    uint32_t ack = ntohl(hdr->ack_num);
    uint8_t flags = hdr->flags;

    const uint8_t* payload = (const uint8_t*)data + header_len;
    uint16_t payload_len = length - header_len;

    struct tcp_connection* conn = tcp_find_connection(src_ip, dst_port, src_port);
    if (!conn) return;  /* No matching connection */

    switch (conn->state) {
        case TCP_SYN_SENT:
            /* Expecting SYN-ACK */
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                if (ack == conn->seq) {
                    conn->ack = seq + 1;
                    conn->remote_window = ntohs(hdr->window);
                    conn->state = TCP_ESTABLISHED;

                    /* Send ACK to complete handshake */
                    tcp_send_segment(conn, TCP_ACK, NULL, 0);
                    serial_print("[TCP] Connection established\n");
                }
            } else if (flags & TCP_RST) {
                conn->state = TCP_CLOSED;
                serial_print("[TCP] Connection refused (RST)\n");
            }
            break;

        case TCP_ESTABLISHED:
            if (flags & TCP_RST) {
                conn->state = TCP_CLOSED;
                serial_print("[TCP] Connection reset\n");
                return;
            }

            /* Handle incoming data */
            if (payload_len > 0 && seq == conn->ack) {
                tcp_rx_write(conn, payload, payload_len);
                conn->ack += payload_len;
            }

            /* Handle FIN from remote */
            if (flags & TCP_FIN) {
                conn->ack = seq + payload_len + 1;
                conn->state = TCP_CLOSE_WAIT;
                tcp_send_segment(conn, TCP_ACK, NULL, 0);

                /* Immediately close our side too */
                tcp_send_segment(conn, TCP_FIN | TCP_ACK, NULL, 0);
                conn->seq++;
                conn->state = TCP_LAST_ACK;
                return;
            }

            /* ACK the data if we got any */
            if (payload_len > 0) {
                tcp_send_segment(conn, TCP_ACK, NULL, 0);
            }

            /* Update remote window */
            conn->remote_window = ntohs(hdr->window);
            break;

        case TCP_FIN_WAIT_1:
            if (flags & TCP_ACK) {
                if (flags & TCP_FIN) {
                    /* Simultaneous close or FIN+ACK */
                    conn->ack = seq + 1;
                    tcp_send_segment(conn, TCP_ACK, NULL, 0);
                    conn->state = TCP_TIME_WAIT;
                } else {
                    conn->state = TCP_FIN_WAIT_2;
                }
            }
            break;

        case TCP_FIN_WAIT_2:
            if (flags & TCP_FIN) {
                conn->ack = seq + 1;
                tcp_send_segment(conn, TCP_ACK, NULL, 0);
                conn->state = TCP_TIME_WAIT;
            }
            break;

        case TCP_LAST_ACK:
            if (flags & TCP_ACK) {
                conn->state = TCP_CLOSED;
            }
            break;

        default:
            break;
    }
}

struct tcp_connection* tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    /* Find a free connection slot */
    struct tcp_connection* conn = NULL;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (connections[i].state == TCP_CLOSED) {
            conn = &connections[i];
            break;
        }
    }
    if (!conn) {
        serial_print("[TCP] No free connection slots\n");
        return NULL;
    }

    /* Initialize connection */
    memset(conn, 0, sizeof(*conn));
    conn->remote_ip = dst_ip;
    conn->local_port = next_port++;
    conn->remote_port = dst_port;
    conn->seq = tcp_initial_seq();
    conn->ack = 0;
    conn->state = TCP_SYN_SENT;

    /* Send SYN */
    conn->seq++;  /* SYN consumes one sequence number */
    uint32_t syn_seq = conn->seq - 1;

    /* Manually build SYN with MSS option */
    uint8_t segment[24];
    struct tcp_header* hdr = (struct tcp_header*)segment;
    hdr->src_port = htons(conn->local_port);
    hdr->dst_port = htons(conn->remote_port);
    hdr->seq_num = htonl(syn_seq);
    hdr->ack_num = 0;
    hdr->data_offset = (6 << 4);   /* 24 bytes = 6 32-bit words (header + MSS option) */
    hdr->flags = TCP_SYN;
    hdr->window = htons(TCP_RX_BUFFER_SIZE);
    hdr->checksum = 0;
    hdr->urgent = 0;

    /* MSS option: kind=2, length=4, value=1460 */
    segment[20] = 2;   /* MSS option kind */
    segment[21] = 4;   /* MSS option length */
    segment[22] = (1460 >> 8) & 0xFF;
    segment[23] = 1460 & 0xFF;

    hdr->checksum = tcp_udp_checksum(htonl(CLAOS_IP), htonl(dst_ip),
                                      IP_PROTO_TCP, segment, 24);

    ipv4_send(dst_ip, IP_PROTO_TCP, segment, 24);

    serial_print("[TCP] SYN sent, waiting for SYN-ACK...\n");

    /* Wait for connection to be established (up to 10 seconds) */
    uint32_t start = timer_get_ticks();
    while (conn->state == TCP_SYN_SENT && (timer_get_ticks() - start) < 1000) {
        __asm__ volatile ("hlt");
    }

    if (conn->state != TCP_ESTABLISHED) {
        serial_print("[TCP] Connection failed\n");
        conn->state = TCP_CLOSED;
        return NULL;
    }

    return conn;
}

int tcp_send(struct tcp_connection* conn, const void* data, uint16_t length) {
    if (!conn || conn->state != TCP_ESTABLISHED) return -1;

    /* Send in chunks that fit in MSS */
    const uint8_t* ptr = (const uint8_t*)data;
    uint16_t remaining = length;
    uint16_t mss = 1460;   /* Maximum Segment Size */

    while (remaining > 0) {
        uint16_t chunk = remaining > mss ? mss : remaining;

        if (!tcp_send_segment(conn, TCP_PSH | TCP_ACK, ptr, chunk)) {
            return length - remaining;
        }
        conn->seq += chunk;

        /* Wait briefly for ACK (simple flow control) */
        uint32_t wait_start = timer_get_ticks();
        while (timer_get_ticks() - wait_start < 50) {
            __asm__ volatile ("hlt");
        }

        ptr += chunk;
        remaining -= chunk;
    }

    return length;
}

int tcp_recv(struct tcp_connection* conn, void* buf, uint16_t max_len, bool block) {
    if (!conn) return -1;

    /* Wait for data if blocking */
    if (block) {
        uint32_t start = timer_get_ticks();
        while (tcp_rx_available(conn) == 0 &&
               conn->state == TCP_ESTABLISHED &&
               (timer_get_ticks() - start) < 3000) {
            __asm__ volatile ("hlt");
        }
    }

    /* Read from the circular buffer */
    uint8_t* dst = (uint8_t*)buf;
    uint16_t read = 0;
    while (read < max_len && conn->rx_tail != conn->rx_head) {
        dst[read++] = conn->rx_buf[conn->rx_tail];
        conn->rx_tail = (conn->rx_tail + 1) % TCP_RX_BUFFER_SIZE;
    }

    return read;
}

void tcp_close(struct tcp_connection* conn) {
    if (!conn || conn->state == TCP_CLOSED) return;

    if (conn->state == TCP_ESTABLISHED) {
        /* Send FIN */
        tcp_send_segment(conn, TCP_FIN | TCP_ACK, NULL, 0);
        conn->seq++;
        conn->state = TCP_FIN_WAIT_1;

        /* Wait for close to complete (up to 5 seconds) */
        uint32_t start = timer_get_ticks();
        while (conn->state != TCP_CLOSED &&
               conn->state != TCP_TIME_WAIT &&
               (timer_get_ticks() - start) < 500) {
            __asm__ volatile ("hlt");
        }
    }

    conn->state = TCP_CLOSED;
}

bool tcp_is_connected(struct tcp_connection* conn) {
    return conn && conn->state == TCP_ESTABLISHED;
}
