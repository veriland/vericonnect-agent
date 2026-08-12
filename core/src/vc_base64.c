#include "vc/vc_base64.h"

static const char B64_TAB[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *vc_base64_encode(const void *data, size_t len)
{
    const uint8_t *p = data;
    size_t out_len = ((len + 2) / 3) * 4;
    char *out = vc_alloc(out_len + 1);
    if (!out) return NULL;
    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i+1] << 8 | p[i+2];
        out[o++] = B64_TAB[(v >> 18) & 63];
        out[o++] = B64_TAB[(v >> 12) & 63];
        out[o++] = B64_TAB[(v >> 6) & 63];
        out[o++] = B64_TAB[v & 63];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)p[i] << 16;
        out[o++] = B64_TAB[(v >> 18) & 63];
        out[o++] = B64_TAB[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i+1] << 8;
        out[o++] = B64_TAB[(v >> 18) & 63];
        out[o++] = B64_TAB[(v >> 12) & 63];
        out[o++] = B64_TAB[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

uint8_t *vc_base64_decode(const char *text, size_t *out_len)
{
    if (!text) return NULL;
    size_t tlen = strlen(text);
    uint8_t *out = vc_alloc(tlen / 4 * 3 + 3);
    if (!out) return NULL;

    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    bool done = false;

    for (size_t i = 0; i < tlen; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '=') { done = true; continue; }
        if (done) { vc_free(out); return NULL; } /* data after padding */
        int v = b64_val(c);
        if (v < 0) { vc_free(out); return NULL; }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)(acc >> bits);
        }
    }
    if (out_len) *out_len = o;
    return out;
}
