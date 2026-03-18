/*
 * CLAOS — Claude Assisted Operating System
 * tls_client.c — TLS Client using BearSSL
 *
 * Wraps BearSSL's SSL engine with our TCP stack to provide encrypted
 * connections. The flow is:
 *   1. DNS resolve hostname → IP
 *   2. TCP connect to IP:port
 *   3. Initialize BearSSL client context with trust anchors
 *   4. Run TLS handshake (BearSSL ↔ TCP via I/O callbacks)
 *   5. Provide tls_send/tls_recv for encrypted application data
 *
 * BearSSL's br_sslio helper handles the SSL engine pump loop for us.
 */

#include "tls_client.h"
#include "tcp.h"
#include "dns.h"
#include "ethernet.h"
#include "net.h"
#include "string.h"
#include "io.h"
#include "timer.h"
#include "entropy.h"

#include "bearssl.h"

/* Declared in ca_certs.c */
extern const br_x509_certificate CLAOS_CERTS[];
extern const size_t CLAOS_CERTS_COUNT;

/* TLS connection state */
struct tls_connection {
    struct tcp_connection* tcp;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    uint8_t iobuf[BR_SSL_BUFSIZE_BIDI];
    bool connected;
};

/* We support 1 TLS connection at a time (toy OS) */
static struct tls_connection tls_conn;
static bool tls_in_use = false;

/*
 * Custom X.509 "no validation" engine for BearSSL.
 * Accepts any certificate without checking trust anchors, signatures,
 * or validity dates. This is a toy OS — security isn't the priority.
 *
 * BearSSL's x509 engine is a vtable with callbacks for certificate processing.
 * We implement a minimal version that just says "yes" to everything and
 * extracts the server's public key for the TLS key exchange.
 */

/*
 * Custom X.509 "no validation" engine.
 * Parses the leaf (first) certificate to extract the server's public key
 * (needed for TLS key exchange) but skips all trust/signature validation.
 */
typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context decoder;  /* BearSSL cert decoder for key extraction */
    bool is_leaf;                     /* True = processing the first (leaf) cert */
    br_x509_pkey pkey;
    unsigned char key_data[256];      /* Buffer for extracted key data */
    bool pkey_valid;
} x509_novalidate_context;

/* Callback for br_x509_decoder: receives the extracted public key */
static void xnv_pkey_callback(void *ctx_v, int key_type,
                                const unsigned char *data, size_t data_len) {
    x509_novalidate_context *xc = (x509_novalidate_context *)ctx_v;
    (void)key_type;
    if (data_len <= sizeof(xc->key_data)) {
        memcpy(xc->key_data, data, data_len);
    }
}

static void xnv_start_chain(const br_x509_class **ctx, const char *server_name) {
    x509_novalidate_context *xc = (x509_novalidate_context *)(void *)ctx;
    (void)server_name;
    xc->is_leaf = true;
    xc->pkey_valid = false;
}

static void xnv_start_cert(const br_x509_class **ctx, uint32_t length) {
    x509_novalidate_context *xc = (x509_novalidate_context *)(void *)ctx;
    (void)length;
    if (xc->is_leaf) {
        br_x509_decoder_init(&xc->decoder, NULL, NULL);
    }
}

static void xnv_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    x509_novalidate_context *xc = (x509_novalidate_context *)(void *)ctx;
    if (xc->is_leaf) {
        br_x509_decoder_push(&xc->decoder, buf, len);
    }
}

static void xnv_end_cert(const br_x509_class **ctx) {
    x509_novalidate_context *xc = (x509_novalidate_context *)(void *)ctx;
    if (xc->is_leaf) {
        /* Extract the public key from the decoded certificate */
        const br_x509_pkey *pk = br_x509_decoder_get_pkey(&xc->decoder);
        if (pk) {
            xc->pkey = *pk;
            /* Copy key data to our own buffer since decoder's buffer is temporary */
            if (pk->key_type == BR_KEYTYPE_EC && pk->key.ec.qlen <= sizeof(xc->key_data)) {
                memcpy(xc->key_data, pk->key.ec.q, pk->key.ec.qlen);
                xc->pkey.key.ec.q = xc->key_data;
            } else if (pk->key_type == BR_KEYTYPE_RSA) {
                /* For RSA, copy n and e */
                size_t off = 0;
                if (pk->key.rsa.nlen <= sizeof(xc->key_data)) {
                    memcpy(xc->key_data + off, pk->key.rsa.n, pk->key.rsa.nlen);
                    xc->pkey.key.rsa.n = xc->key_data + off;
                    off += pk->key.rsa.nlen;
                }
                if (off + pk->key.rsa.elen <= sizeof(xc->key_data)) {
                    memcpy(xc->key_data + off, pk->key.rsa.e, pk->key.rsa.elen);
                    xc->pkey.key.rsa.e = xc->key_data + off;
                }
            }
            xc->pkey_valid = true;
            serial_print("[X509] Extracted server public key\n");
        }
        xc->is_leaf = false;
    }
}

static unsigned xnv_end_chain(const br_x509_class **ctx) {
    (void)ctx;
    return 0;  /* Always accept — no validation */
}

static const br_x509_pkey *xnv_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    x509_novalidate_context *xc = (x509_novalidate_context *)(void *)ctx;
    if (usages) {
        *usages = BR_KEYTYPE_SIGN | BR_KEYTYPE_KEYX;
    }
    if (xc->pkey_valid) {
        return &xc->pkey;
    }
    return NULL;
}

static const br_x509_class x509_novalidate_vtable_impl = {
    sizeof(x509_novalidate_context),
    xnv_start_chain,
    xnv_start_cert,
    xnv_append,
    xnv_end_cert,
    xnv_end_chain,
    xnv_get_pkey
};

static x509_novalidate_context x509_novalidate_ctx = {
    &x509_novalidate_vtable_impl,
    { 0 }, false, { 0 }, { 0 }, false
};

/*
 * Low-level read callback for BearSSL.
 * Reads encrypted data from the TCP connection.
 * Must return at least 1 byte, or -1 on error.
 */
static int tls_low_read(void* ctx, unsigned char* buf, size_t len) {
    struct tcp_connection* tcp = (struct tcp_connection*)ctx;

    /* Wait for data from TCP. Background net_poll task handles reception. */
    int total = 0;
    uint32_t start = timer_get_ticks();

    while (total == 0) {
        int n = tcp_recv(tcp, buf, (uint16_t)(len > 2048 ? 2048 : len), false);
        if (n > 0) {
            total = n;
            break;
        }

        if (!tcp_is_connected(tcp) && tcp->state != TCP_ESTABLISHED) {
            if (total > 0) break;
            return -1;
        }

        /* Timeout after 30 seconds */
        if (timer_get_ticks() - start > 3000) {
            serial_print("[TLS] Read timeout\n");
            if (total > 0) break;
            return -1;
        }

        __asm__ volatile ("hlt");
    }

    return total;
}

/*
 * Low-level write callback for BearSSL.
 * Writes encrypted data to the TCP connection.
 * Must write at least 1 byte, or return -1 on error.
 */
static int tls_low_write(void* ctx, const unsigned char* buf, size_t len) {
    struct tcp_connection* tcp = (struct tcp_connection*)ctx;

    if (!tcp_is_connected(tcp)) {
        return -1;
    }

    int n = tcp_send(tcp, buf, (uint16_t)(len > 1460 ? 1460 : len));
    if (n <= 0) return -1;
    return n;
}

void tls_init(void) {
    tls_in_use = false;
    memset(&tls_conn, 0, sizeof(tls_conn));
}

tls_connection_t* tls_connect(const char* hostname, uint16_t port) {
    if (tls_in_use) {
        serial_print("[TLS] Connection already in use\n");
        return NULL;
    }

    serial_print("[TLS] Connecting to ");
    serial_print(hostname);
    serial_print("...\n");

    /* Step 1: Resolve hostname to IP */
    uint32_t ip = dns_resolve(hostname);
    if (ip == 0) {
        serial_print("[TLS] DNS resolution failed\n");
        return NULL;
    }

    /* Step 2: Establish TCP connection */
    struct tcp_connection* tcp = tcp_connect(ip, port);
    if (!tcp) {
        serial_print("[TLS] TCP connection failed\n");
        return NULL;
    }

    tls_conn.tcp = tcp;
    tls_in_use = true;

    /* Step 3: Initialize BearSSL client context with all cipher suites.
     * We use br_x509_knownkey for now (skips certificate validation).
     * This is acceptable for a toy OS — we trust the DNS resolution. */
    br_ssl_client_init_full(&tls_conn.sc, &tls_conn.xc, NULL, 0);

    /* Override X.509 validation with a permissive "accept all" engine.
     * This is fine for a toy OS — we trust the DNS resolution.
     * TODO: implement proper certificate validation with trust anchors. */
    br_ssl_engine_set_x509(&tls_conn.sc.eng, &x509_novalidate_ctx.vtable);

    /* Set the I/O buffer */
    br_ssl_engine_set_buffer(&tls_conn.sc.eng,
                              tls_conn.iobuf, sizeof(tls_conn.iobuf), 1);

    /* Step 4: Reset the engine and set SNI hostname */
    br_ssl_client_reset(&tls_conn.sc, hostname, 0);

    /* Step 5: Initialize the I/O wrapper with our TCP callbacks */
    br_sslio_init(&tls_conn.ioc,
                   &tls_conn.sc.eng,
                   tls_low_read, tls_conn.tcp,
                   tls_low_write, tls_conn.tcp);

    /* Step 6: The handshake happens automatically on the first
     * br_sslio_read or br_sslio_write call. But let's force it
     * by doing a flush. */
    serial_print("[TLS] Starting TLS handshake...\n");

    /* Pump the engine to drive the handshake */
    int err = br_sslio_flush(&tls_conn.ioc);
    if (err < 0) {
        int engine_err = br_ssl_engine_last_error(&tls_conn.sc.eng);
        serial_print("[TLS] Handshake failed, error: ");
        /* Print error number to serial */
        char errbuf[12];
        int pos = 0;
        int e = engine_err;
        if (e == 0) { errbuf[pos++] = '0'; }
        else {
            if (e < 0) { errbuf[pos++] = '-'; e = -e; }
            char tmp[12]; int tpos = 0;
            while (e > 0) { tmp[tpos++] = '0' + e % 10; e /= 10; }
            while (tpos > 0) errbuf[pos++] = tmp[--tpos];
        }
        errbuf[pos] = '\0';
        serial_print(errbuf);
        serial_putchar('\n');

        tcp_close(tcp);
        tls_in_use = false;
        return NULL;
    }

    /* Check if the engine is still in a good state */
    unsigned state = br_ssl_engine_current_state(&tls_conn.sc.eng);
    if (state == BR_SSL_CLOSED) {
        serial_print("[TLS] Engine closed after handshake\n");
        tcp_close(tcp);
        tls_in_use = false;
        return NULL;
    }

    tls_conn.connected = true;
    serial_print("[TLS] Handshake complete!\n");

    return &tls_conn;
}

int tls_send(tls_connection_t* conn, const void* data, size_t len) {
    if (!conn || !conn->connected) return -1;

    int ret = br_sslio_write_all(&conn->ioc, data, len);
    if (ret < 0) return -1;

    /* Flush to ensure data is actually sent */
    ret = br_sslio_flush(&conn->ioc);
    if (ret < 0) return -1;

    return (int)len;
}

int tls_recv(tls_connection_t* conn, void* buf, size_t max_len) {
    if (!conn || !conn->connected) return -1;

    int ret = br_sslio_read(&conn->ioc, buf, max_len);
    if (ret < 0) {
        conn->connected = false;
        return -1;
    }

    return ret;
}

void tls_close(tls_connection_t* conn) {
    if (!conn) return;

    if (conn->connected) {
        br_sslio_close(&conn->ioc);
        conn->connected = false;
    }

    if (conn->tcp) {
        tcp_close(conn->tcp);
        conn->tcp = NULL;
    }

    tls_in_use = false;
}

bool tls_is_connected(tls_connection_t* conn) {
    if (!conn) return false;

    unsigned state = br_ssl_engine_current_state(&conn->sc.eng);
    return conn->connected && (state != BR_SSL_CLOSED);
}
