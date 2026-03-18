/*
 * CLAOS — Claude Assisted Operating System
 * https.c — HTTPS Client
 *
 * Builds HTTP/1.1 requests over TLS and parses responses.
 * Uses Connection: close so each request gets its own TCP+TLS connection.
 * This is simpler than managing persistent connections.
 */

#include "https.h"
#include "tls_client.h"
#include "net.h"
#include "string.h"
#include "io.h"

/* Append a string to the request buffer, return new position */
static int req_append(char* buf, int pos, int max, const char* str) {
    int len = strlen(str);
    if (pos + len < max) {
        memcpy(buf + pos, str, len);
        return pos + len;
    }
    return pos;
}

/* Build and send an HTTP request, then read the response */
bool https_post(const char* hostname, uint16_t port, const char* path,
                const char* headers, const char* body, int body_len,
                struct http_response* resp) {
    memset(resp, 0, sizeof(*resp));

    /* Connect via TLS */
    tls_connection_t* tls = tls_connect(hostname, port);
    if (!tls) {
        serial_print("[HTTPS] TLS connect failed\n");
        return false;
    }

    /* Build the entire HTTP request (headers + body) in one buffer.
     * Sending as one chunk avoids BearSSL flush issues between writes. */
    static char req[6144];
    int pos = 0;

    /* Request line */
    pos = req_append(req, pos, sizeof(req), "POST ");
    pos = req_append(req, pos, sizeof(req), path);
    pos = req_append(req, pos, sizeof(req), " HTTP/1.1\r\n");

    /* Host header */
    pos = req_append(req, pos, sizeof(req), "Host: ");
    pos = req_append(req, pos, sizeof(req), hostname);
    pos = req_append(req, pos, sizeof(req), "\r\n");

    /* Content-Length header */
    pos = req_append(req, pos, sizeof(req), "Content-Length: ");
    char len_str[12];
    int len_pos = 0;
    int tmp = body_len;
    if (tmp == 0) { len_str[len_pos++] = '0'; }
    else {
        char rev[12]; int rpos = 0;
        while (tmp > 0) { rev[rpos++] = '0' + tmp % 10; tmp /= 10; }
        while (rpos > 0) len_str[len_pos++] = rev[--rpos];
    }
    len_str[len_pos] = '\0';
    pos = req_append(req, pos, sizeof(req), len_str);
    pos = req_append(req, pos, sizeof(req), "\r\n");

    /* Connection: close */
    pos = req_append(req, pos, sizeof(req), "Connection: close\r\n");

    /* Additional headers from caller */
    if (headers) {
        pos = req_append(req, pos, sizeof(req), headers);
    }

    /* End of headers */
    memcpy(req + pos, "\r\n", 2); pos += 2;

    /* Append the body directly after the headers */
    if (body && body_len > 0 && pos + body_len < (int)sizeof(req)) {
        memcpy(req + pos, body, body_len);
        pos += body_len;
    }

    /* Send everything in one shot */
    serial_print("[HTTPS] Sending request...\n");
    if (tls_send(tls, req, pos) < 0) {
        serial_print("[HTTPS] Failed to send request\n");
        tls_close(tls);
        return false;
    }

    /* Read response */
    serial_print("[HTTPS] Reading response...\n");
    static char raw_resp[8192];
    int total = 0;

    while (total < (int)sizeof(raw_resp) - 1) {
        int n = tls_recv(tls, raw_resp + total, sizeof(raw_resp) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    raw_resp[total] = '\0';

    tls_close(tls);

    if (total == 0) {
        serial_print("[HTTPS] Empty response\n");
        return false;
    }

    serial_print("[HTTPS] Got response, parsing...\n");

    /* Parse status line: "HTTP/1.1 200 OK\r\n" */
    char* p = raw_resp;
    /* Skip "HTTP/x.x " */
    while (*p && *p != ' ') p++;
    if (*p) p++;
    /* Parse status code */
    resp->status_code = 0;
    while (*p >= '0' && *p <= '9') {
        resp->status_code = resp->status_code * 10 + (*p - '0');
        p++;
    }

    /* Find the body (after \r\n\r\n) */
    char* body_start = NULL;
    for (int i = 0; i < total - 3; i++) {
        if (raw_resp[i] == '\r' && raw_resp[i+1] == '\n' &&
            raw_resp[i+2] == '\r' && raw_resp[i+3] == '\n') {
            body_start = &raw_resp[i + 4];
            break;
        }
    }

    if (body_start) {
        int raw_body_len = total - (body_start - raw_resp);

        /* Check if the response uses chunked transfer encoding */
        bool chunked = false;
        for (char* h = raw_resp; h < body_start - 2; h++) {
            if ((*h == 'T' || *h == 't') &&
                strncmp(h, "Transfer-Encoding: chunked", 26) == 0) {
                chunked = true;
                break;
            }
            /* Also check lowercase */
            if ((*h == 't') &&
                strncmp(h, "transfer-encoding: chunked", 26) == 0) {
                chunked = true;
                break;
            }
        }

        if (chunked) {
            /* Decode chunked transfer encoding:
             * Format: <hex-size>\r\n<data>\r\n<hex-size>\r\n<data>...0\r\n */
            serial_print("[HTTPS] Decoding chunked response\n");
            resp->body_len = 0;
            char* cp = body_start;
            char* end = raw_resp + total;

            while (cp < end) {
                /* Parse hex chunk size */
                uint32_t chunk_size = 0;
                while (cp < end && *cp != '\r') {
                    char c = *cp;
                    if (c >= '0' && c <= '9') chunk_size = chunk_size * 16 + (c - '0');
                    else if (c >= 'a' && c <= 'f') chunk_size = chunk_size * 16 + (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') chunk_size = chunk_size * 16 + (c - 'A' + 10);
                    cp++;
                }
                /* Skip \r\n after chunk size */
                if (cp + 1 < end) cp += 2;

                if (chunk_size == 0) break;  /* Last chunk */

                /* Copy chunk data */
                int to_copy = (int)chunk_size;
                if (resp->body_len + to_copy > (int)sizeof(resp->body) - 1)
                    to_copy = sizeof(resp->body) - 1 - resp->body_len;
                if (to_copy > 0 && cp + to_copy <= end) {
                    memcpy(resp->body + resp->body_len, cp, to_copy);
                    resp->body_len += to_copy;
                }
                cp += chunk_size;

                /* Skip \r\n after chunk data */
                if (cp + 1 < end) cp += 2;
            }
            resp->body[resp->body_len] = '\0';
        } else {
            /* Regular Content-Length response */
            resp->body_len = raw_body_len;
            if (resp->body_len > (int)sizeof(resp->body) - 1)
                resp->body_len = sizeof(resp->body) - 1;
            memcpy(resp->body, body_start, resp->body_len);
            resp->body[resp->body_len] = '\0';
        }
    }

    serial_print("[HTTPS] Status: ");
    /* Print status to serial */
    char sbuf[8]; int si = 0;
    tmp = resp->status_code;
    if (tmp == 0) sbuf[si++] = '0';
    else { char r[8]; int ri = 0; while(tmp>0){r[ri++]='0'+tmp%10;tmp/=10;} while(ri>0)sbuf[si++]=r[--ri]; }
    sbuf[si] = '\0';
    serial_print(sbuf);
    serial_print(", body_len=");
    si = 0; tmp = resp->body_len;
    if (tmp == 0) sbuf[si++] = '0';
    else { char r[8]; int ri = 0; while(tmp>0){r[ri++]='0'+tmp%10;tmp/=10;} while(ri>0)sbuf[si++]=r[--ri]; }
    sbuf[si] = '\0';
    serial_print(sbuf);
    serial_putchar('\n');

    return resp->status_code > 0;
}
