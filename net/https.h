/*
 * CLAOS — Claude Assisted Operating System
 * https.h — HTTPS Client
 *
 * HTTP/1.1 over TLS. Sends POST requests and parses responses.
 * Uses Connection: close for simplicity (one request per connection).
 */

#ifndef CLAOS_HTTPS_H
#define CLAOS_HTTPS_H

#include "types.h"

/* HTTP response structure */
struct http_response {
    int  status_code;           /* HTTP status (200, 400, etc.) */
    char body[4096];            /* Response body */
    int  body_len;              /* Actual body length */
};

/* Send an HTTPS POST request.
 * Returns true on success, fills `resp` with the response.
 * `headers` is a null-terminated string of additional headers (each ending with \r\n). */
bool https_post(const char* hostname, uint16_t port, const char* path,
                const char* headers, const char* body, int body_len,
                struct http_response* resp);

#endif /* CLAOS_HTTPS_H */
