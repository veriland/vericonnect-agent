/* Minimal HTTPS/1.1 client used by the test app sender. */
#include "vc/vc_http.h"
#include "vc/vc_str.h"
#include "vc/vc_tls.h"
#include "vc/vc_os.h"
#include <stdio.h>

static int find_header_int(const char *headers, const char *name, long *out)
{
    const char *p = headers;
    size_t nlen = strlen(name);
    while (p && *p) {
        if (!vc_strnicmp(p, name, nlen) && p[nlen] == ':') {
            *out = atol(p + nlen + 1);
            return VC_OK;
        }
        p = strstr(p, "\r\n");
        if (p) p += 2;
    }
    return VC_E_NOT_FOUND;
}

static bool header_has_value(const char *headers, const char *name,
                             const char *value)
{
    const char *p = headers;
    size_t nlen = strlen(name);
    while (p && *p) {
        if (!vc_strnicmp(p, name, nlen) && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            const char *eol = strstr(v, "\r\n");
            size_t vlen = eol ? (size_t)(eol - v) : strlen(v);
            char tmp[256];
            if (vlen >= sizeof tmp) vlen = sizeof tmp - 1;
            memcpy(tmp, v, vlen);
            tmp[vlen] = 0;
            /* trim + case-insensitive substring match */
            char *s = tmp;
            while (*s == ' ' || *s == '\t') s++;
            size_t sl = strlen(s), wl = strlen(value);
            for (size_t i = 0; i + wl <= sl; i++)
                if (!vc_strnicmp(s + i, value, wl)) return true;
            return false;
        }
        p = strstr(p, "\r\n");
        if (p) p += 2;
    }
    return false;
}

int vc_http_request(const char *method, const char *host, int port,
                    const char *path_and_query,
                    const char *extra_headers,
                    const void *body, size_t body_len,
                    const char *content_type,
                    int timeout_ms,
                    vc_http_response *out)
{
    memset(out, 0, sizeof *out);

    vc_sock *sock = vc_sock_connect(host, port, timeout_ms);
    if (!sock) return VC_E_IO;
    vc_tls *tls = vc_tls_connect(sock, host, timeout_ms);
    if (!tls) return VC_E_TLS;

    int result = VC_E_FAIL;
    vc_buf req, in;
    vc_buf_init(&req);
    vc_buf_init(&in);

    vc_buf_appendf(&req, "%s %s HTTP/1.1\r\nHost: %s\r\n",
                   method, path_and_query, host);
    if (body && body_len) {
        vc_buf_appendf(&req, "Content-Length: %zu\r\n", body_len);
        vc_buf_appendf(&req, "Content-Type: %s\r\n",
                       content_type ? content_type : "application/json");
    }
    if (extra_headers) vc_buf_append_str(&req, extra_headers);
    vc_buf_append_str(&req, "Connection: close\r\n\r\n");

    if (vc_tls_send(tls, req.data, req.len) != VC_OK) goto done;
    if (body && body_len)
        if (vc_tls_send(tls, body, body_len) != VC_OK) goto done;

    /* read everything until close (Connection: close) with timeout */
    {
        uint64_t deadline = vc_os_monotonic_ms() + (uint64_t)timeout_ms;
        uint8_t tmp[8192];
        for (;;) {
            uint64_t now = vc_os_monotonic_ms();
            if (now >= deadline) break;
            int n = vc_tls_recv(tls, tmp, sizeof tmp, (int)(deadline - now));
            if (n <= 0) break;
            vc_buf_append(&in, tmp, (size_t)n);
            /* early exit when Content-Length satisfied */
            char *he = in.data ? strstr(in.data, "\r\n\r\n") : NULL;
            if (he) {
                long cl = -1;
                char saved = *he;
                *he = 0;
                int has_cl = find_header_int(in.data, "Content-Length", &cl);
                bool chunked = header_has_value(in.data, "Transfer-Encoding", "chunked");
                *he = saved;
                size_t hdr_len = (size_t)(he + 4 - in.data);
                if (has_cl == VC_OK && !chunked && in.len >= hdr_len + (size_t)cl)
                    break;
            }
        }
    }

    if (!in.data) goto done;
    {
        char *he = strstr(in.data, "\r\n\r\n");
        if (!he) goto done;
        size_t hdr_len = (size_t)(he - in.data);
        size_t body_off = hdr_len + 4;

        /* status line */
        if (vc_strnicmp(in.data, "HTTP/", 5)) goto done;
        const char *sp = strchr(in.data, ' ');
        if (!sp) goto done;
        out->status = atoi(sp + 1);
        const char *sp2 = strchr(sp + 1, ' ');
        const char *eol = strstr(in.data, "\r\n");
        if (sp2 && eol && sp2 < eol)
            out->status_text = vc_strndup(sp2 + 1, (size_t)(eol - sp2 - 1));
        else
            out->status_text = vc_strdup("");

        out->headers = vc_strndup(in.data, hdr_len);

        /* body: chunked or plain */
        if (header_has_value(out->headers, "Transfer-Encoding", "chunked")) {
            vc_buf dec;
            vc_buf_init(&dec);
            const char *p = in.data + body_off;
            const char *end = in.data + in.len;
            while (p < end) {
                char *after = NULL;
                long chunk = strtol(p, &after, 16);
                if (!after || chunk < 0) break;
                p = strstr(after, "\r\n");
                if (!p) break;
                p += 2;
                if (chunk == 0) break;
                if (p + chunk > end) chunk = (long)(end - p);
                vc_buf_append(&dec, p, (size_t)chunk);
                p += chunk;
                if (p + 2 <= end && p[0] == '\r' && p[1] == '\n') p += 2;
            }
            out->body_len = dec.len;
            out->body = (uint8_t *)vc_buf_take(&dec);
        } else {
            out->body_len = in.len - body_off;
            out->body = static_cast<uint8_t*>(vc_alloc(out->body_len + 1));
            if (out->body) {
                memcpy(out->body, in.data + body_off, out->body_len);
                out->body[out->body_len] = 0;
            }
        }
        result = VC_OK;
    }

done:
    vc_buf_free(&req);
    vc_buf_free(&in);
    vc_tls_close(tls);
    if (result != VC_OK) vc_http_response_free(out);
    return result;
}

void vc_http_response_free(vc_http_response *r)
{
    if (!r) return;
    vc_free(r->status_text);
    vc_free(r->headers);
    vc_free(r->body);
    memset(r, 0, sizeof *r);
}
