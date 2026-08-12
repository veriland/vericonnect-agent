/*
 * vc_tls.h - TLS client stream abstraction.
 *
 * Windows: SChannel (platform/win/vc_tls_schannel.c) - no extra deps.
 * POSIX:   OpenSSL   (platform/posix/vc_tls_openssl.c).
 */
#ifndef VC_TLS_H
#define VC_TLS_H

#include "vc_common.h"
#include "vc_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_tls vc_tls;

/* Performs the TLS handshake over an existing connected socket.
 * hostname is used for SNI + certificate validation.
 * On success the vc_tls owns the socket. NULL on failure. */
vc_tls *vc_tls_connect(vc_sock *sock, const char *hostname, int timeout_ms);

/* Sends all len bytes. Returns VC_OK or VC_E_*. */
int vc_tls_send(vc_tls *t, const void *data, size_t len);

/* Returns decrypted bytes read (>0), 0 on orderly close,
 * VC_E_TIMEOUT, or VC_E_* on error. */
int vc_tls_recv(vc_tls *t, void *buf, size_t len, int timeout_ms);

void vc_tls_close(vc_tls *t);   /* also closes the underlying socket */

#ifdef __cplusplus
}
#endif

#endif
