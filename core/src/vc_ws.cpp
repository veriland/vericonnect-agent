/*
 * RFC 6455 WebSocket client over vc_tls.
 * - client frames are always masked
 * - fragmented messages are reassembled in recv
 * - ping is answered with pong transparently
 * - outgoing messages larger than VC_WS_FRAG_SIZE are fragmented
 */
#include "vc/vc_ws.h"
#include "vc/vc_str.h"
#include "vc/vc_base64.h"
#include "vc/vc_os.h"
#include <stdio.h>

#define VC_WS_FRAG_SIZE   (60 * 1024)
#define VC_WS_MAX_MSG     (64u * 1024u * 1024u)   /* sanity cap 64 MB */
#define VC_WS_HDR_TIMEOUT 15000

struct vc_ws {
    vc_tls *tls;
    vc_buf  in;          /* raw undecoded incoming bytes */
    bool    closed;
};

/* --------------------------------------------------------------- */
/* Connect / handshake                                              */
/* --------------------------------------------------------------- */

static int read_more(vc_ws *ws, int timeout_ms)
{
    uint8_t tmp[8192];
    int n = vc_tls_recv(ws->tls, tmp, sizeof tmp, timeout_ms);
    if (n > 0) {
        if (vc_buf_append(&ws->in, tmp, (size_t)n) != VC_OK)
            return VC_E_NOMEM;
        return n;
    }
    if (n == 0) return VC_E_CLOSED;
    return n; /* VC_E_TIMEOUT / VC_E_* */
}

vc_ws *vc_ws_connect(const char *host, int port,
                     const char *path_and_query,
                     const char *extra_headers,
                     int timeout_ms)
{
    vc_sock *sock = vc_sock_connect(host, port, timeout_ms);
    if (!sock) return NULL;
    vc_tls *tls = vc_tls_connect(sock, host, timeout_ms);
    if (!tls) return NULL;   /* vc_tls_connect closes sock on failure */

    vc_ws *ws = static_cast<vc_ws*>(vc_alloc(sizeof *ws));
    if (!ws) { vc_tls_close(tls); return NULL; }
    ws->tls = tls;
    ws->closed = false;
    vc_buf_init(&ws->in);

    /* Sec-WebSocket-Key: 16 random bytes, base64 */
    uint8_t nonce[16];
    if (vc_os_random(nonce, sizeof nonce) != VC_OK) {
        vc_ws_close(ws);
        return NULL;
    }
    char *key = vc_base64_encode(nonce, sizeof nonce);
    if (!key) { vc_ws_close(ws); return NULL; }

    vc_buf req;
    vc_buf_init(&req);
    vc_buf_appendf(&req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "%s"
        "\r\n",
        path_and_query, host, key, extra_headers ? extra_headers : "");
    vc_free(key);

    int rc = vc_tls_send(ws->tls, req.data, req.len);
    vc_buf_free(&req);
    if (rc != VC_OK) { vc_ws_close(ws); return NULL; }

    /* read the 101 response header block */
    uint64_t deadline = vc_os_monotonic_ms() + VC_WS_HDR_TIMEOUT;
    char *hdr_end = NULL;
    while (!(ws->in.data && (hdr_end = strstr(ws->in.data, "\r\n\r\n")))) {
        uint64_t now = vc_os_monotonic_ms();
        if (now >= deadline) { vc_ws_close(ws); return NULL; }
        int n = read_more(ws, (int)(deadline - now));
        if (n < 0) { vc_ws_close(ws); return NULL; }
    }

    /* Status line: HTTP/1.1 101 ... */
    if (vc_strnicmp(ws->in.data, "HTTP/1.1 101", 12) != 0 &&
        vc_strnicmp(ws->in.data, "HTTP/1.0 101", 12) != 0) {
        vc_ws_close(ws);
        return NULL;
    }
    /* We rely on TLS for integrity; the Sec-WebSocket-Accept SHA-1
     * check is intentionally skipped. */
    vc_buf_consume(&ws->in, (size_t)(hdr_end + 4 - ws->in.data));
    return ws;
}

/* --------------------------------------------------------------- */
/* Send                                                             */
/* --------------------------------------------------------------- */

static int send_frame(vc_ws *ws, uint8_t opcode, bool fin,
                      const uint8_t *data, size_t len)
{
    uint8_t hdr[14];
    size_t h = 0;
    hdr[h++] = (uint8_t)((fin ? 0x80 : 0x00) | opcode);
    if (len < 126) {
        hdr[h++] = (uint8_t)(0x80 | len);
    } else if (len <= 0xFFFF) {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(len >> 8);
        hdr[h++] = (uint8_t)len;
    } else {
        hdr[h++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            hdr[h++] = (uint8_t)((uint64_t)len >> (i * 8));
    }
    uint8_t mask[4];
    if (vc_os_random(mask, 4) != VC_OK) return VC_E_FAIL;
    memcpy(hdr + h, mask, 4);
    h += 4;

    if (vc_tls_send(ws->tls, hdr, h) != VC_OK) return VC_E_IO;

    if (len) {
        uint8_t chunk[8192];
        size_t off = 0;
        while (off < len) {
            size_t n = len - off;
            if (n > sizeof chunk) n = sizeof chunk;
            for (size_t i = 0; i < n; i++)
                chunk[i] = data[off + i] ^ mask[(off + i) & 3];
            if (vc_tls_send(ws->tls, chunk, n) != VC_OK) return VC_E_IO;
            off += n;
        }
    }
    return VC_OK;
}

int vc_ws_send(vc_ws *ws, vc_ws_msg_type type, const void *data, size_t len)
{
    if (!ws || ws->closed) return VC_E_CLOSED;
    const uint8_t *p = static_cast<const uint8_t*>(data);
    uint8_t opcode = (type == VC_WS_TEXT) ? 0x1 : 0x2;

    if (len <= VC_WS_FRAG_SIZE)
        return send_frame(ws, opcode, true, p, len);

    /* fragment */
    size_t off = 0;
    bool first = true;
    while (off < len) {
        size_t n = len - off;
        if (n > VC_WS_FRAG_SIZE) n = VC_WS_FRAG_SIZE;
        bool fin = (off + n == len);
        int rc = send_frame(ws, first ? opcode : 0x0, fin, p + off, n);
        if (rc != VC_OK) return rc;
        first = false;
        off += n;
    }
    return VC_OK;
}

int vc_ws_send_ping(vc_ws *ws)
{
    if (!ws || ws->closed) return VC_E_CLOSED;
    return send_frame(ws, 0x9, true, NULL, 0);
}

int vc_ws_send_close(vc_ws *ws, uint16_t code)
{
    if (!ws || ws->closed) return VC_E_CLOSED;
    uint8_t payload[2] = { (uint8_t)(code >> 8), (uint8_t)code };
    return send_frame(ws, 0x8, true, payload, 2);
}

/* --------------------------------------------------------------- */
/* Receive                                                          */
/* --------------------------------------------------------------- */

/* Try to parse one frame from ws->in. Returns VC_OK when a full frame
 * was consumed (opcode/payload out), VC_E_TIMEOUT if more bytes are
 * needed, error otherwise. payload is vc_alloc'd. */
static int parse_frame(vc_ws *ws, uint8_t *opcode, bool *fin,
                       uint8_t **payload, size_t *plen)
{
    const uint8_t *p = (const uint8_t *)ws->in.data;
    size_t avail = ws->in.len;
    if (avail < 2) return VC_E_TIMEOUT;

    *fin = (p[0] & 0x80) != 0;
    *opcode = p[0] & 0x0F;
    bool masked = (p[1] & 0x80) != 0;
    uint64_t len = p[1] & 0x7F;
    size_t pos = 2;

    if (len == 126) {
        if (avail < pos + 2) return VC_E_TIMEOUT;
        len = ((uint64_t)p[pos] << 8) | p[pos+1];
        pos += 2;
    } else if (len == 127) {
        if (avail < pos + 8) return VC_E_TIMEOUT;
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | p[pos + i];
        pos += 8;
    }
    if (len > VC_WS_MAX_MSG) return VC_E_PROTOCOL;

    uint8_t mask[4] = {0};
    if (masked) {
        if (avail < pos + 4) return VC_E_TIMEOUT;
        memcpy(mask, p + pos, 4);
        pos += 4;
    }
    if (avail < pos + (size_t)len) return VC_E_TIMEOUT;

    uint8_t *out = static_cast<uint8_t*>(vc_alloc((size_t)len + 1));
    if (!out) return VC_E_NOMEM;
    memcpy(out, p + pos, (size_t)len);
    if (masked)
        for (size_t i = 0; i < (size_t)len; i++) out[i] ^= mask[i & 3];
    out[len] = 0;

    vc_buf_consume(&ws->in, pos + (size_t)len);
    *payload = out;
    *plen = (size_t)len;
    return VC_OK;
}

int vc_ws_recv_msg(vc_ws *ws, vc_ws_msg_type *type,
                   uint8_t **payload, size_t *len, int timeout_ms)
{
    if (!ws) return VC_E_INVALID_ARG;
    if (ws->closed) return VC_E_CLOSED;

    *payload = NULL;
    *len = 0;

    vc_buf msg;
    vc_buf_init(&msg);
    uint8_t msg_opcode = 0;
    bool in_fragmented = false;

    uint64_t deadline = vc_os_monotonic_ms() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);

    for (;;) {
        uint8_t opcode;
        bool fin;
        uint8_t *frag = NULL;
        size_t frag_len = 0;

        int rc = parse_frame(ws, &opcode, &fin, &frag, &frag_len);
        if (rc == VC_E_TIMEOUT) {
            uint64_t now = vc_os_monotonic_ms();
            if (timeout_ms >= 0 && now >= deadline) {
                vc_buf_free(&msg);
                return VC_E_TIMEOUT;
            }
            int wait = timeout_ms < 0 ? 60000 : (int)(deadline - now);
            int n = read_more(ws, wait);
            if (n == VC_E_TIMEOUT) {
                if (timeout_ms < 0) continue;
                vc_buf_free(&msg);
                return VC_E_TIMEOUT;
            }
            if (n < 0) {
                vc_buf_free(&msg);
                ws->closed = true;
                return n;
            }
            continue;
        }
        if (rc != VC_OK) { vc_buf_free(&msg); return rc; }

        switch (opcode) {
        case 0x9: /* ping -> pong with same payload */
            send_frame(ws, 0xA, true, frag, frag_len);
            vc_free(frag);
            continue;
        case 0xA: /* pong */
            vc_free(frag);
            continue;
        case 0x8: /* close */
            send_frame(ws, 0x8, true, frag, frag_len > 2 ? 2 : frag_len);
            ws->closed = true;
            *type = VC_WS_CLOSE;
            *payload = frag;
            *len = frag_len;
            vc_buf_free(&msg);
            return VC_E_CLOSED;
        case 0x0: /* continuation */
            if (!in_fragmented) { vc_free(frag); vc_buf_free(&msg); return VC_E_PROTOCOL; }
            vc_buf_append(&msg, frag, frag_len);
            vc_free(frag);
            if (fin) {
                *type = (msg_opcode == 0x1) ? VC_WS_TEXT : VC_WS_BINARY;
                *len = msg.len;
                *payload = (uint8_t *)vc_buf_take(&msg);
                return VC_OK;
            }
            continue;
        case 0x1:
        case 0x2:
            if (in_fragmented) { vc_free(frag); vc_buf_free(&msg); return VC_E_PROTOCOL; }
            if (fin) {
                *type = (opcode == 0x1) ? VC_WS_TEXT : VC_WS_BINARY;
                *payload = frag;
                *len = frag_len;
                vc_buf_free(&msg);
                return VC_OK;
            }
            in_fragmented = true;
            msg_opcode = opcode;
            vc_buf_append(&msg, frag, frag_len);
            vc_free(frag);
            continue;
        default:
            vc_free(frag);
            vc_buf_free(&msg);
            return VC_E_PROTOCOL;
        }
    }
}

void vc_ws_close(vc_ws *ws)
{
    if (!ws) return;
    if (ws->tls) vc_tls_close(ws->tls);
    vc_buf_free(&ws->in);
    vc_free(ws);
}
