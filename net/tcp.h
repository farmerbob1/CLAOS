/*
 * CLAOS — Claude Assisted Operating System
 * tcp.h — Transmission Control Protocol
 */

#ifndef CLAOS_TCP_H
#define CLAOS_TCP_H

#include "types.h"

/* TCP flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

/* TCP header */
struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;   /* Upper 4 bits = header length in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

/* TCP connection states */
typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
} tcp_state_t;

/* TCP connection (socket) */
#define TCP_RX_BUFFER_SIZE 8192
#define TCP_MAX_CONNECTIONS 4

struct tcp_connection {
    tcp_state_t state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq;           /* Our next sequence number */
    uint32_t ack;           /* Next expected byte from peer */
    uint16_t remote_window;

    /* Receive buffer (circular) */
    uint8_t  rx_buf[TCP_RX_BUFFER_SIZE];
    uint32_t rx_head;       /* Write position */
    uint32_t rx_tail;       /* Read position */
};

/* Initialize TCP subsystem */
void tcp_init(void);

/* Process a received TCP segment */
void tcp_receive(uint32_t src_ip, const void* data, uint16_t length);

/* Open a TCP connection (blocking). Returns connection handle or NULL. */
struct tcp_connection* tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/* Send data on an established connection. Returns bytes sent, or -1 on error. */
int tcp_send(struct tcp_connection* conn, const void* data, uint16_t length);

/* Read data from the receive buffer. Returns bytes read, or 0 if empty.
 * If block=true, waits until data is available or connection closes. */
int tcp_recv(struct tcp_connection* conn, void* buf, uint16_t max_len, bool block);

/* Close a connection */
void tcp_close(struct tcp_connection* conn);

/* Check if connection is still alive */
bool tcp_is_connected(struct tcp_connection* conn);

#endif /* CLAOS_TCP_H */
