#include "vc/vc_url.h"
#include "vc/vc_str.h"
#include <stdio.h>

static bool is_unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~';
}

char *vc_url_encode(const char *s)
{
    if (!s) return vc_strdup("");
    vc_buf b;
    vc_buf_init(&b);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (is_unreserved(*p))
            vc_buf_append_char(&b, (char)*p);
        else
            vc_buf_appendf(&b, "%%%02X", *p);
    }
    return vc_buf_take(&b);
}

int vc_url_parse(const char *url, vc_url *out)
{
    memset(out, 0, sizeof *out);
    if (!url) return VC_E_INVALID_ARG;

    const char *p = strstr(url, "://");
    if (!p) return VC_E_INVALID_ARG;
    out->scheme = vc_strndup(url, (size_t)(p - url));
    p += 3;

    const char *host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '?') p++;
    if (p == host_start) { vc_url_free(out); return VC_E_INVALID_ARG; }
    out->host = vc_strndup(host_start, (size_t)(p - host_start));

    out->port = 0;
    if (*p == ':') {
        p++;
        out->port = atoi(p);
        while (*p && *p != '/' && *p != '?') p++;
    }
    if (out->port == 0) {
        if (!vc_stricmp(out->scheme, "https") || !vc_stricmp(out->scheme, "wss"))
            out->port = 443;
        else if (!vc_stricmp(out->scheme, "http") || !vc_stricmp(out->scheme, "ws"))
            out->port = 80;
        else
            out->port = 443;
    }

    if (*p == '/') {
        const char *path_start = p;
        while (*p && *p != '?') p++;
        out->path = vc_strndup(path_start, (size_t)(p - path_start));
    } else {
        out->path = vc_strdup("/");
        if (*p && *p != '?') { vc_url_free(out); return VC_E_INVALID_ARG; }
    }

    if (*p == '?')
        out->query = vc_strdup(p + 1);

    if (!out->scheme || !out->host || !out->path) {
        vc_url_free(out);
        return VC_E_NOMEM;
    }
    return VC_OK;
}

void vc_url_free(vc_url *u)
{
    vc_free(u->scheme);
    vc_free(u->host);
    vc_free(u->path);
    vc_free(u->query);
    memset(u, 0, sizeof *u);
}
