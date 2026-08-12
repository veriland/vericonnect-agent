#include "vc/vc_sock.h"
#include "vc/vc_str.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

struct vc_sock {
    SOCKET s;
};

static LONG g_ws_init = 0;

int vc_sock_global_init(void)
{
    if (InterlockedCompareExchange(&g_ws_init, 1, 0) == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            g_ws_init = 0;
            return VC_E_FAIL;
        }
    }
    return VC_OK;
}

void vc_sock_global_cleanup(void)
{
    if (InterlockedCompareExchange(&g_ws_init, 0, 1) == 1)
        WSACleanup();
}

vc_sock *vc_sock_connect(const char *host, int port, int timeout_ms)
{
    vc_sock_global_init();

    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
        return NULL;

    SOCKET sock = INVALID_SOCKET;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET) continue;

        /* non-blocking connect with timeout */
        u_long nb = 1;
        ioctlsocket(sock, FIONBIO, &nb);
        int cr = connect(sock, ai->ai_addr, (int)ai->ai_addrlen);
        if (cr == 0) {
            nb = 0; ioctlsocket(sock, FIONBIO, &nb);
            break;
        }
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            if (select(0, NULL, &wfds, NULL, &tv) > 0) {
                int err = 0; int len = sizeof err;
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
                if (err == 0) {
                    nb = 0; ioctlsocket(sock, FIONBIO, &nb);
                    break;
                }
            }
        }
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (sock == INVALID_SOCKET) return NULL;

    /* keepalive helps long-lived relay connections survive NAT idle */
    BOOL ka = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&ka, sizeof ka);

    vc_sock *vs = vc_alloc(sizeof *vs);
    if (!vs) { closesocket(sock); return NULL; }
    vs->s = sock;
    return vs;
}

void vc_sock_close(vc_sock *s)
{
    if (!s) return;
    if (s->s != INVALID_SOCKET) closesocket(s->s);
    vc_free(s);
}

int vc_sock_send(vc_sock *s, const void *data, size_t len)
{
    const char *p = data;
    size_t sent = 0;
    while (sent < len) {
        int n = send(s->s, p + sent, (int)(len - sent), 0);
        if (n == SOCKET_ERROR) return VC_E_IO;
        sent += (size_t)n;
    }
    return (int)sent;
}

int vc_sock_recv(vc_sock *s, void *buf, size_t len, int timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s->s, &rfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int sel = select(0, &rfds, NULL, NULL, timeout_ms < 0 ? NULL : &tv);
    if (sel == 0) return VC_E_TIMEOUT;
    if (sel == SOCKET_ERROR) return VC_E_IO;

    int n = recv(s->s, buf, (int)len, 0);
    if (n == 0) return 0;
    if (n == SOCKET_ERROR) return VC_E_IO;
    return n;
}
