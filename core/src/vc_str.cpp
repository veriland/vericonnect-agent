#include "vc/vc_str.h"
#include <stdio.h>
#include <ctype.h>

void *vc_alloc(size_t n)            { return malloc(n ? n : 1); }
void *vc_realloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void  vc_free(void *p)              { free(p); }

char *vc_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = static_cast<char*>(vc_alloc(n + 1));
    if (p) memcpy(p, s, n + 1);
    return p;
}

char *vc_strndup(const char *s, size_t n)
{
    if (!s) return NULL;
    char *p = static_cast<char*>(vc_alloc(n + 1));
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void vc_buf_init(vc_buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

void vc_buf_free(vc_buf *b)
{
    vc_free(b->data);
    vc_buf_init(b);
}

int vc_buf_reserve(vc_buf *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return VC_OK;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) cap *= 2;
    char *p = static_cast<char*>(vc_realloc(b->data, cap));
    if (!p) return VC_E_NOMEM;
    b->data = p;
    b->cap = cap;
    return VC_OK;
}

int vc_buf_append(vc_buf *b, const void *data, size_t n)
{
    if (n == 0) { if (!b->data) { if (vc_buf_reserve(b, 0)) return VC_E_NOMEM; b->data[0] = 0; } return VC_OK; }
    if (vc_buf_reserve(b, n)) return VC_E_NOMEM;
    memcpy(b->data + b->len, data, n);
    b->len += n;
    b->data[b->len] = 0;
    return VC_OK;
}

int vc_buf_append_str(vc_buf *b, const char *s)
{
    return vc_buf_append(b, s, s ? strlen(s) : 0);
}

int vc_buf_append_char(vc_buf *b, char c)
{
    return vc_buf_append(b, &c, 1);
}

int vc_buf_appendf(vc_buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return VC_E_FAIL; }
    if (vc_buf_reserve(b, (size_t)n)) { va_end(ap2); return VC_E_NOMEM; }
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
    return VC_OK;
}

char *vc_buf_take(vc_buf *b)
{
    if (!b->data) {
        char *e = static_cast<char*>(vc_alloc(1));
        if (e) e[0] = 0;
        return e;
    }
    char *p = b->data;
    vc_buf_init(b);
    return p;
}

void vc_buf_clear(vc_buf *b)
{
    b->len = 0;
    if (b->data) b->data[0] = 0;
}

void vc_buf_consume(vc_buf *b, size_t n)
{
    if (n >= b->len) { vc_buf_clear(b); return; }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = 0;
}

int vc_stricmp(const char *a, const char *b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d) return d;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int vc_strnicmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}
