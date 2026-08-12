/*
 * TLS client stream using Windows SChannel (SSPI).
 * No external dependencies; validates the server certificate against
 * the Windows trust store using the SNI hostname.
 */
#include "vc/vc_tls.h"
#include "vc/vc_str.h"
#include "vc/vc_os.h"

#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#include <stdio.h>

#pragma comment(lib, "secur32.lib")

struct vc_tls {
    vc_sock       *sock;
    CredHandle     cred;
    CtxtHandle     ctx;
    bool           have_cred;
    bool           have_ctx;
    SecPkgContext_StreamSizes sizes;
    /* leftover decrypted plaintext not yet consumed by caller */
    vc_buf         plain;
    /* raw ciphertext received but not yet decrypted */
    vc_buf         enc;
    bool           closed;
};

static int recv_some(vc_tls *t, int timeout_ms)
{
    uint8_t tmp[8192];
    int n = vc_sock_recv(t->sock, tmp, sizeof tmp, timeout_ms);
    if (n > 0) {
        if (vc_buf_append(&t->enc, tmp, (size_t)n) != VC_OK) return VC_E_NOMEM;
        return n;
    }
    return n; /* 0 = closed, or VC_E_* */
}

static int do_handshake(vc_tls *t, const char *hostname, int timeout_ms)
{
    SecBufferDesc out_desc, in_desc;
    SecBuffer out_buf[1], in_buf[2];
    DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
                  ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                  ISC_REQ_USE_SUPPLIED_CREDS;
    DWORD out_flags = 0;
    SECURITY_STATUS ss;
    bool first = true;

    /* mutable copy for the SNI target name */
    char *target = vc_strdup(hostname);
    if (!target) return VC_E_NOMEM;

    uint64_t deadline = vc_os_monotonic_ms() + (uint64_t)timeout_ms;

    for (;;) {
        if (!first) {
            /* need data from server before continuing */
            while (t->enc.len == 0) {
                uint64_t now = vc_os_monotonic_ms();
                if (now >= deadline) { vc_free(target); return VC_E_TIMEOUT; }
                int r = recv_some(t, (int)(deadline - now));
                if (r == VC_E_TIMEOUT) continue;
                if (r <= 0) { vc_free(target); return VC_E_TLS; }
            }
        }

        in_buf[0].BufferType = SECBUFFER_TOKEN;
        in_buf[0].pvBuffer   = t->enc.data;
        in_buf[0].cbBuffer   = (unsigned long)t->enc.len;
        in_buf[1].BufferType = SECBUFFER_EMPTY;
        in_buf[1].pvBuffer   = NULL;
        in_buf[1].cbBuffer   = 0;
        in_desc.ulVersion = SECBUFFER_VERSION;
        in_desc.cBuffers  = 2;
        in_desc.pBuffers  = in_buf;

        out_buf[0].BufferType = SECBUFFER_TOKEN;
        out_buf[0].pvBuffer   = NULL;
        out_buf[0].cbBuffer   = 0;
        out_desc.ulVersion = SECBUFFER_VERSION;
        out_desc.cBuffers  = 1;
        out_desc.pBuffers  = out_buf;

        ss = InitializeSecurityContextA(
            &t->cred,
            first ? NULL : &t->ctx,
            first ? target : NULL,
            flags, 0, 0,
            first ? NULL : &in_desc,
            0,
            first ? &t->ctx : NULL,
            &out_desc, &out_flags, NULL);
        first = false;
        if (!t->have_ctx &&
            (ss == SEC_E_OK || ss == SEC_I_CONTINUE_NEEDED ||
             ss == SEC_E_INCOMPLETE_MESSAGE || FAILED(ss)))
            t->have_ctx = true;

        /* send any token SChannel produced */
        if (out_buf[0].cbBuffer > 0 && out_buf[0].pvBuffer) {
            int sr = vc_sock_send(t->sock, out_buf[0].pvBuffer, out_buf[0].cbBuffer);
            FreeContextBuffer(out_buf[0].pvBuffer);
            if (sr < 0) { vc_free(target); return VC_E_IO; }
        }

        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            /* keep the buffered enc data, read more */
            uint64_t now = vc_os_monotonic_ms();
            if (now >= deadline) { vc_free(target); return VC_E_TIMEOUT; }
            int r = recv_some(t, (int)(deadline - now));
            if (r < 0 && r != VC_E_TIMEOUT) { vc_free(target); return VC_E_TLS; }
            continue;
        }

        /* consume the token bytes SChannel processed; keep any 'extra' */
        if (in_buf[1].BufferType == SECBUFFER_EXTRA) {
            size_t extra = in_buf[1].cbBuffer;
            vc_buf_consume(&t->enc, t->enc.len - extra);
        } else if (ss != SEC_E_INCOMPLETE_MESSAGE) {
            vc_buf_clear(&t->enc);
        }

        if (ss == SEC_I_CONTINUE_NEEDED)
            continue;
        if (ss == SEC_E_OK) {
            vc_free(target);
            QueryContextAttributes(&t->ctx, SECPKG_ATTR_STREAM_SIZES, &t->sizes);
            return VC_OK;
        }
        vc_free(target);
        return VC_E_TLS;
    }
}

vc_tls *vc_tls_connect(vc_sock *sock, const char *hostname, int timeout_ms)
{
    vc_tls *t = vc_alloc(sizeof *t);
    if (!t) { vc_sock_close(sock); return NULL; }
    memset(t, 0, sizeof *t);
    t->sock = sock;
    vc_buf_init(&t->plain);
    vc_buf_init(&t->enc);

    SCHANNEL_CRED sc;
    memset(&sc, 0, sizeof sc);
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    sc.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS |
                 SCH_USE_STRONG_CRYPTO;

    SECURITY_STATUS ss = AcquireCredentialsHandleA(
        NULL, (LPSTR)UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL, &sc,
        NULL, NULL, &t->cred, NULL);
    if (ss != SEC_E_OK) { vc_tls_close(t); return NULL; }
    t->have_cred = true;

    if (do_handshake(t, hostname, timeout_ms) != VC_OK) {
        vc_tls_close(t);
        return NULL;
    }
    return t;
}

int vc_tls_send(vc_tls *t, const void *data, size_t len)
{
    if (!t || t->closed) return VC_E_CLOSED;
    const uint8_t *p = data;
    size_t off = 0;

    size_t hdr = t->sizes.cbHeader;
    size_t trl = t->sizes.cbTrailer;
    size_t maxmsg = t->sizes.cbMaximumMessage;

    uint8_t *buf = vc_alloc(hdr + maxmsg + trl);
    if (!buf) return VC_E_NOMEM;

    int result = VC_OK;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > maxmsg) chunk = maxmsg;

        memcpy(buf + hdr, p + off, chunk);

        SecBuffer bufs[4];
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        bufs[0].pvBuffer   = buf;
        bufs[0].cbBuffer   = (unsigned long)hdr;
        bufs[1].BufferType = SECBUFFER_DATA;
        bufs[1].pvBuffer   = buf + hdr;
        bufs[1].cbBuffer   = (unsigned long)chunk;
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        bufs[2].pvBuffer   = buf + hdr + chunk;
        bufs[2].cbBuffer   = (unsigned long)trl;
        bufs[3].BufferType = SECBUFFER_EMPTY;
        bufs[3].pvBuffer   = NULL;
        bufs[3].cbBuffer   = 0;

        SecBufferDesc desc;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers  = 4;
        desc.pBuffers  = bufs;

        SECURITY_STATUS ss = EncryptMessage(&t->ctx, 0, &desc, 0);
        if (ss != SEC_E_OK) { result = VC_E_TLS; break; }

        size_t total = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
        if (vc_sock_send(t->sock, buf, total) < 0) { result = VC_E_IO; break; }
        off += chunk;
    }
    vc_free(buf);
    return result;
}

int vc_tls_recv(vc_tls *t, void *buf, size_t len, int timeout_ms)
{
    if (!t) return VC_E_INVALID_ARG;

    /* serve buffered plaintext first */
    if (t->plain.len > 0) {
        size_t n = t->plain.len < len ? t->plain.len : len;
        memcpy(buf, t->plain.data, n);
        vc_buf_consume(&t->plain, n);
        return (int)n;
    }
    if (t->closed) return 0;

    uint64_t deadline = vc_os_monotonic_ms() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);

    for (;;) {
        /* try to decrypt whatever ciphertext we have */
        if (t->enc.len > 0) {
            SecBuffer bufs[4];
            bufs[0].BufferType = SECBUFFER_DATA;
            bufs[0].pvBuffer   = t->enc.data;
            bufs[0].cbBuffer   = (unsigned long)t->enc.len;
            bufs[1].BufferType = SECBUFFER_EMPTY;
            bufs[2].BufferType = SECBUFFER_EMPTY;
            bufs[3].BufferType = SECBUFFER_EMPTY;
            SecBufferDesc desc;
            desc.ulVersion = SECBUFFER_VERSION;
            desc.cBuffers  = 4;
            desc.pBuffers  = bufs;

            SECURITY_STATUS ss = DecryptMessage(&t->ctx, &desc, 0, NULL);
            if (ss == SEC_E_OK) {
                SecBuffer *data_buf = NULL, *extra_buf = NULL;
                for (int i = 0; i < 4; i++) {
                    if (bufs[i].BufferType == SECBUFFER_DATA && !data_buf)
                        data_buf = &bufs[i];
                    else if (bufs[i].BufferType == SECBUFFER_EXTRA && !extra_buf)
                        extra_buf = &bufs[i];
                }
                if (data_buf && data_buf->cbBuffer > 0)
                    vc_buf_append(&t->plain, data_buf->pvBuffer, data_buf->cbBuffer);

                if (extra_buf && extra_buf->cbBuffer > 0) {
                    size_t extra = extra_buf->cbBuffer;
                    vc_buf_consume(&t->enc, t->enc.len - extra);
                } else {
                    vc_buf_clear(&t->enc);
                }

                if (t->plain.len > 0) {
                    size_t n = t->plain.len < len ? t->plain.len : len;
                    memcpy(buf, t->plain.data, n);
                    vc_buf_consume(&t->plain, n);
                    return (int)n;
                }
                continue; /* decrypted 0 bytes, loop for more */
            } else if (ss == SEC_I_CONTEXT_EXPIRED) {
                t->closed = true;
                return 0;
            } else if (ss != SEC_E_INCOMPLETE_MESSAGE) {
                return VC_E_TLS;
            }
            /* incomplete: fall through and read more */
        }

        uint64_t now = vc_os_monotonic_ms();
        int wait = timeout_ms < 0 ? -1 : (now >= deadline ? 0 : (int)(deadline - now));
        if (timeout_ms >= 0 && wait == 0 && t->enc.len == 0)
            return VC_E_TIMEOUT;
        int r = recv_some(t, wait < 0 ? 60000 : wait);
        if (r == VC_E_TIMEOUT) {
            if (timeout_ms < 0) continue;
            return VC_E_TIMEOUT;
        }
        if (r == 0) { t->closed = true; return 0; }
        if (r < 0) return r;
    }
}

void vc_tls_close(vc_tls *t)
{
    if (!t) return;
    if (t->have_ctx) DeleteSecurityContext(&t->ctx);
    if (t->have_cred) FreeCredentialsHandle(&t->cred);
    vc_buf_free(&t->plain);
    vc_buf_free(&t->enc);
    if (t->sock) vc_sock_close(t->sock);
    vc_free(t);
}
