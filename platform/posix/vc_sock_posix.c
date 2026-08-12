/* POSIX TCP client socket (Linux/macOS). */
#include "vc/vc_sock.h"
#include "vc/vc_str.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

struct vc_sock { int fd; };

int  vc_sock_global_init(void)    { return VC_OK; }
void vc_sock_global_cleanup(void) { }

vc_sock *vc_sock_connect(const char *host, int port, int timeout_ms)
{
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return NULL;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int cr = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (cr == 0) { fcntl(fd, F_SETFL, fl); break; }
        if (errno == EINPROGRESS) {
            fd_set w; FD_ZERO(&w); FD_SET(fd, &w);
            struct timeval tv = { timeout_ms/1000, (timeout_ms%1000)*1000 };
            if (select(fd+1, NULL, &w, NULL, &tv) > 0) {
                int err = 0; socklen_t l = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
                if (err == 0) { fcntl(fd, F_SETFL, fl); break; }
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return NULL;
    int ka = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof ka);
    vc_sock *s = vc_alloc(sizeof *s);
    if (!s) { close(fd); return NULL; }
    s->fd = fd;
    return s;
}

void vc_sock_close(vc_sock *s)
{
    if (!s) return;
    if (s->fd >= 0) close(s->fd);
    vc_free(s);
}

int vc_sock_send(vc_sock *s, const void *data, size_t len)
{
    const char *p = data; size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(s->fd, p + sent, len - sent, 0);
        if (n <= 0) { if (errno == EINTR) continue; return VC_E_IO; }
        sent += (size_t)n;
    }
    return (int)sent;
}

int vc_sock_recv(vc_sock *s, void *buf, size_t len, int timeout_ms)
{
    fd_set r; FD_ZERO(&r); FD_SET(s->fd, &r);
    struct timeval tv = { timeout_ms/1000, (timeout_ms%1000)*1000 };
    int sel = select(s->fd+1, &r, NULL, NULL, timeout_ms < 0 ? NULL : &tv);
    if (sel == 0) return VC_E_TIMEOUT;
    if (sel < 0) return VC_E_IO;
    ssize_t n = recv(s->fd, buf, len, 0);
    if (n == 0) return 0;
    if (n < 0) return VC_E_IO;
    return (int)n;
}
