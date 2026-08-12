/*
 * vc_sock.h - minimal TCP client socket abstraction.
 * Implemented per platform (platform/win, platform/posix).
 */
#ifndef VC_SOCK_H
#define VC_SOCK_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_sock vc_sock;

int  vc_sock_global_init(void);       /* WSAStartup on Windows */
void vc_sock_global_cleanup(void);

/* Connect to host:port (TCP). timeout_ms for the connect itself. */
vc_sock *vc_sock_connect(const char *host, int port, int timeout_ms);
void     vc_sock_close(vc_sock *s);

/* Returns bytes sent, or VC_E_* (<0). Sends all bytes unless error. */
int vc_sock_send(vc_sock *s, const void *data, size_t len);

/* Returns bytes received (>0), 0 on orderly close, VC_E_TIMEOUT if
 * nothing arrived within timeout_ms, other VC_E_* on error. */
int vc_sock_recv(vc_sock *s, void *buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
