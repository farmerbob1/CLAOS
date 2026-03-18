/*
 * CLAOS — Claude Assisted Operating System
 * tls_client.h — High-level TLS Client API
 *
 * Wraps BearSSL to provide a simple connect/send/recv/close interface
 * for encrypted communication. Used by the HTTPS client to talk to
 * api.anthropic.com.
 */

#ifndef CLAOS_TLS_CLIENT_H
#define CLAOS_TLS_CLIENT_H

#include "types.h"
#include "tcp.h"

/* Opaque TLS connection handle */
typedef struct tls_connection tls_connection_t;

/* Initialize the TLS subsystem (call once at boot) */
void tls_init(void);

/* Establish a TLS connection to hostname:port.
 * Performs DNS resolution, TCP connect, and TLS handshake.
 * Returns a connection handle, or NULL on failure. */
tls_connection_t* tls_connect(const char* hostname, uint16_t port);

/* Send data over the TLS connection. Returns bytes sent, or -1 on error. */
int tls_send(tls_connection_t* conn, const void* data, size_t len);

/* Receive data from the TLS connection. Returns bytes read, or -1 on error.
 * Blocks until data is available or connection closes. */
int tls_recv(tls_connection_t* conn, void* buf, size_t max_len);

/* Close the TLS connection (sends close_notify, closes TCP). */
void tls_close(tls_connection_t* conn);

/* Check if the TLS connection is still active */
bool tls_is_connected(tls_connection_t* conn);

#endif /* CLAOS_TLS_CLIENT_H */
