/* TLS client stream over OpenSSL (Linux/macOS). */
#include "vc/vc_tls.h"
#include "vc/vc_str.h"
#include "vc/vc_os.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

struct vc_tls {
    vc_sock *sock;
    SSL_CTX *ctx;
    SSL     *ssl;
    BIO     *rbio;   /* network -> SSL */
    BIO     *wbio;   /* SSL -> network */
    bool     closed;
};

static int flush_wbio(vc_tls *t)
{
    char tmp[8192];
    int pending;
    while ((pending = BIO_read(t->wbio, tmp, sizeof tmp)) > 0)
        if (vc_sock_send(t->sock, tmp, (size_t)pending) < 0) return VC_E_IO;
    return VC_OK;
}

static int pump_in(vc_tls *t, int timeout_ms)
{
    char tmp[8192];
    int n = vc_sock_recv(t->sock, tmp, sizeof tmp, timeout_ms);
    if (n > 0) { BIO_write(t->rbio, tmp, n); return n; }
    return n;
}

vc_tls *vc_tls_connect(vc_sock *sock, const char *hostname, int timeout_ms)
{
    vc_tls *t = vc_alloc(sizeof *t);
    if (!t) { vc_sock_close(sock); return NULL; }
    memset(t, 0, sizeof *t);
    t->sock = sock;

    t->ctx = SSL_CTX_new(TLS_client_method());
    if (!t->ctx) { vc_tls_close(t); return NULL; }
    SSL_CTX_set_verify(t->ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_default_verify_paths(t->ctx);
    SSL_CTX_set_min_proto_version(t->ctx, TLS1_2_VERSION);

    t->ssl = SSL_new(t->ctx);
    t->rbio = BIO_new(BIO_s_mem());
    t->wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(t->ssl, t->rbio, t->wbio);
    SSL_set_connect_state(t->ssl);
    SSL_set_tlsext_host_name(t->ssl, hostname);
    X509_VERIFY_PARAM_set1_host(SSL_get0_param(t->ssl), hostname, 0);

    uint64_t deadline = vc_os_monotonic_ms() + (uint64_t)timeout_ms;
    for (;;) {
        int r = SSL_do_handshake(t->ssl);
        if (r == 1) break;
        int err = SSL_get_error(t->ssl, r);
        if (flush_wbio(t) != VC_OK) { vc_tls_close(t); return NULL; }
        if (err == SSL_ERROR_WANT_READ) {
            uint64_t now = vc_os_monotonic_ms();
            if (now >= deadline) { vc_tls_close(t); return NULL; }
            int pr = pump_in(t, (int)(deadline - now));
            if (pr <= 0 && pr != VC_E_TIMEOUT) { vc_tls_close(t); return NULL; }
        } else if (err != SSL_ERROR_WANT_WRITE) {
            vc_tls_close(t); return NULL;
        }
    }
    flush_wbio(t);
    return t;
}

int vc_tls_send(vc_tls *t, const void *data, size_t len)
{
    if (!t || t->closed) return VC_E_CLOSED;
    size_t off = 0;
    const char *p = data;
    while (off < len) {
        int n = SSL_write(t->ssl, p + off, (int)(len - off));
        if (n <= 0) {
            int err = SSL_get_error(t->ssl, n);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                if (flush_wbio(t) != VC_OK) return VC_E_IO;
                continue;
            }
            return VC_E_TLS;
        }
        off += (size_t)n;
        if (flush_wbio(t) != VC_OK) return VC_E_IO;
    }
    return VC_OK;
}

int vc_tls_recv(vc_tls *t, void *buf, size_t len, int timeout_ms)
{
    if (!t) return VC_E_INVALID_ARG;
    if (t->closed) return 0;
    uint64_t deadline = vc_os_monotonic_ms() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);
    for (;;) {
        int n = SSL_read(t->ssl, buf, (int)len);
        if (n > 0) return n;
        int err = SSL_get_error(t->ssl, n);
        if (err == SSL_ERROR_ZERO_RETURN) { t->closed = true; return 0; }
        if (err == SSL_ERROR_WANT_READ) {
            uint64_t now = vc_os_monotonic_ms();
            int wait = timeout_ms < 0 ? 60000 : (now >= deadline ? 0 : (int)(deadline - now));
            if (timeout_ms >= 0 && wait == 0) return VC_E_TIMEOUT;
            int pr = pump_in(t, wait);
            if (pr == VC_E_TIMEOUT) { if (timeout_ms < 0) continue; return VC_E_TIMEOUT; }
            if (pr == 0) { t->closed = true; return 0; }
            if (pr < 0) return pr;
        } else {
            return VC_E_TLS;
        }
    }
}

void vc_tls_close(vc_tls *t)
{
    if (!t) return;
    if (t->ssl) { SSL_shutdown(t->ssl); SSL_free(t->ssl); } /* frees BIOs */
    if (t->ctx) SSL_CTX_free(t->ctx);
    if (t->sock) vc_sock_close(t->sock);
    vc_free(t);
}
