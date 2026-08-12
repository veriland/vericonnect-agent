#include "vc/vc_json.h"
#include <math.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Construction / destruction                                          */
/* ------------------------------------------------------------------ */

static vc_json *json_new(vc_json_type t)
{
    vc_json *v = vc_alloc(sizeof *v);
    if (!v) return NULL;
    memset(v, 0, sizeof *v);
    v->type = t;
    return v;
}

vc_json *vc_json_new_null(void)   { return json_new(VC_JSON_NULL); }
vc_json *vc_json_new_array(void)  { return json_new(VC_JSON_ARRAY); }
vc_json *vc_json_new_object(void) { return json_new(VC_JSON_OBJECT); }

vc_json *vc_json_new_bool(bool b)
{
    vc_json *v = json_new(VC_JSON_BOOL);
    if (v) v->boolean = b;
    return v;
}

vc_json *vc_json_new_num(double n)
{
    vc_json *v = json_new(VC_JSON_NUMBER);
    if (v) v->number = n;
    return v;
}

vc_json *vc_json_new_str(const char *s)
{
    vc_json *v = json_new(VC_JSON_STRING);
    if (!v) return NULL;
    v->string = vc_strdup(s ? s : "");
    if (!v->string) { vc_free(v); return NULL; }
    return v;
}

void vc_json_free(vc_json *v)
{
    while (v) {
        vc_json *next = v->next;
        vc_json_free(v->child);
        vc_free(v->key);
        vc_free(v->string);
        vc_free(v);
        v = next;
    }
}

/* ------------------------------------------------------------------ */
/* Containers                                                          */
/* ------------------------------------------------------------------ */

static void container_append(vc_json *parent, vc_json *value)
{
    value->next = NULL;
    if (!parent->child) {
        parent->child = value;
        return;
    }
    vc_json *it = parent->child;
    while (it->next) it = it->next;
    it->next = value;
}

int vc_json_arr_add(vc_json *arr, vc_json *value)
{
    if (!arr || arr->type != VC_JSON_ARRAY || !value) return VC_E_INVALID_ARG;
    container_append(arr, value);
    return VC_OK;
}

int vc_json_obj_set(vc_json *obj, const char *key, vc_json *value)
{
    if (!obj || obj->type != VC_JSON_OBJECT || !key || !value) {
        vc_json_free(value);
        return VC_E_INVALID_ARG;
    }
    value->key = vc_strdup(key);
    if (!value->key) { vc_json_free(value); return VC_E_NOMEM; }
    container_append(obj, value);
    return VC_OK;
}

int vc_json_obj_set_str(vc_json *obj, const char *key, const char *s)
{
    vc_json *v = vc_json_new_str(s);
    return v ? vc_json_obj_set(obj, key, v) : VC_E_NOMEM;
}

int vc_json_obj_set_num(vc_json *obj, const char *key, double n)
{
    vc_json *v = vc_json_new_num(n);
    return v ? vc_json_obj_set(obj, key, v) : VC_E_NOMEM;
}

int vc_json_obj_set_bool(vc_json *obj, const char *key, bool b)
{
    vc_json *v = vc_json_new_bool(b);
    return v ? vc_json_obj_set(obj, key, v) : VC_E_NOMEM;
}

int vc_json_obj_set_null(vc_json *obj, const char *key)
{
    vc_json *v = vc_json_new_null();
    return v ? vc_json_obj_set(obj, key, v) : VC_E_NOMEM;
}

vc_json *vc_json_obj_get(const vc_json *obj, const char *key)
{
    if (!obj || obj->type != VC_JSON_OBJECT || !key) return NULL;
    for (vc_json *it = obj->child; it; it = it->next)
        if (it->key && strcmp(it->key, key) == 0) return it;
    return NULL;
}

vc_json *vc_json_obj_get_ci(const vc_json *obj, const char *key)
{
    if (!obj || obj->type != VC_JSON_OBJECT || !key) return NULL;
    for (vc_json *it = obj->child; it; it = it->next)
        if (it->key && vc_stricmp(it->key, key) == 0) return it;
    return NULL;
}

const char *vc_json_get_str(const vc_json *obj, const char *key, const char *def)
{
    vc_json *v = vc_json_obj_get_ci(obj, key);
    return (v && v->type == VC_JSON_STRING) ? v->string : def;
}

double vc_json_get_num(const vc_json *obj, const char *key, double def)
{
    vc_json *v = vc_json_obj_get_ci(obj, key);
    return (v && v->type == VC_JSON_NUMBER) ? v->number : def;
}

bool vc_json_get_bool(const vc_json *obj, const char *key, bool def)
{
    vc_json *v = vc_json_obj_get_ci(obj, key);
    return (v && v->type == VC_JSON_BOOL) ? v->boolean : def;
}

size_t vc_json_array_size(const vc_json *arr)
{
    if (!arr || (arr->type != VC_JSON_ARRAY && arr->type != VC_JSON_OBJECT))
        return 0;
    size_t n = 0;
    for (vc_json *it = arr->child; it; it = it->next) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

typedef struct parser {
    const char *p;
    const char *end;
    int depth;
} parser;

#define VC_JSON_MAX_DEPTH 128

static void skip_ws(parser *ps)
{
    while (ps->p < ps->end &&
           (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n'))
        ps->p++;
}

static vc_json *parse_value(parser *ps);

static int append_utf8(vc_buf *b, uint32_t cp)
{
    char tmp[4];
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        return vc_buf_append(b, tmp, 1);
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        return vc_buf_append(b, tmp, 2);
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        return vc_buf_append(b, tmp, 3);
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        return vc_buf_append(b, tmp, 4);
    }
}

static int parse_hex4(parser *ps, uint32_t *out)
{
    if (ps->end - ps->p < 4) return VC_E_PROTOCOL;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = ps->p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return VC_E_PROTOCOL;
    }
    ps->p += 4;
    *out = v;
    return VC_OK;
}

/* Parses a JSON string literal (cursor on opening quote). Returns
 * malloc'd string or NULL. */
static char *parse_string_raw(parser *ps)
{
    if (ps->p >= ps->end || *ps->p != '"') return NULL;
    ps->p++;
    vc_buf b;
    vc_buf_init(&b);
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p;
        if (c == '"') {
            ps->p++;
            return vc_buf_take(&b);
        }
        if (c == '\\') {
            ps->p++;
            if (ps->p >= ps->end) break;
            char e = *ps->p++;
            switch (e) {
            case '"':  vc_buf_append_char(&b, '"');  break;
            case '\\': vc_buf_append_char(&b, '\\'); break;
            case '/':  vc_buf_append_char(&b, '/');  break;
            case 'b':  vc_buf_append_char(&b, '\b'); break;
            case 'f':  vc_buf_append_char(&b, '\f'); break;
            case 'n':  vc_buf_append_char(&b, '\n'); break;
            case 'r':  vc_buf_append_char(&b, '\r'); break;
            case 't':  vc_buf_append_char(&b, '\t'); break;
            case 'u': {
                uint32_t cp;
                if (parse_hex4(ps, &cp) != VC_OK) goto fail;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    /* surrogate pair */
                    if (ps->end - ps->p < 6 || ps->p[0] != '\\' || ps->p[1] != 'u')
                        goto fail;
                    ps->p += 2;
                    uint32_t lo;
                    if (parse_hex4(ps, &lo) != VC_OK) goto fail;
                    if (lo < 0xDC00 || lo > 0xDFFF) goto fail;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    goto fail;
                }
                if (append_utf8(&b, cp) != VC_OK) goto fail;
                break;
            }
            default:
                goto fail;
            }
        } else if (c < 0x20) {
            goto fail; /* unescaped control char */
        } else {
            vc_buf_append_char(&b, (char)c);
            ps->p++;
        }
    }
fail:
    vc_buf_free(&b);
    return NULL;
}

static vc_json *parse_object(parser *ps)
{
    vc_json *obj = vc_json_new_object();
    if (!obj) return NULL;
    ps->p++; /* '{' */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') { ps->p++; return obj; }
    for (;;) {
        skip_ws(ps);
        char *key = parse_string_raw(ps);
        if (!key) goto fail;
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') { vc_free(key); goto fail; }
        ps->p++;
        vc_json *val = parse_value(ps);
        if (!val) { vc_free(key); goto fail; }
        val->key = key;
        container_append(obj, val);
        skip_ws(ps);
        if (ps->p >= ps->end) goto fail;
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; return obj; }
        goto fail;
    }
fail:
    vc_json_free(obj);
    return NULL;
}

static vc_json *parse_array(parser *ps)
{
    vc_json *arr = vc_json_new_array();
    if (!arr) return NULL;
    ps->p++; /* '[' */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') { ps->p++; return arr; }
    for (;;) {
        vc_json *val = parse_value(ps);
        if (!val) goto fail;
        container_append(arr, val);
        skip_ws(ps);
        if (ps->p >= ps->end) goto fail;
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; return arr; }
        goto fail;
    }
fail:
    vc_json_free(arr);
    return NULL;
}

static vc_json *parse_value(parser *ps)
{
    skip_ws(ps);
    if (ps->p >= ps->end) return NULL;
    if (++ps->depth > VC_JSON_MAX_DEPTH) { ps->depth--; return NULL; }

    vc_json *result = NULL;
    char c = *ps->p;

    if (c == '{') {
        result = parse_object(ps);
    } else if (c == '[') {
        result = parse_array(ps);
    } else if (c == '"') {
        char *s = parse_string_raw(ps);
        if (s) {
            result = json_new(VC_JSON_STRING);
            if (result) result->string = s;
            else vc_free(s);
        }
    } else if (c == 't' && ps->end - ps->p >= 4 && !memcmp(ps->p, "true", 4)) {
        ps->p += 4;
        result = vc_json_new_bool(true);
    } else if (c == 'f' && ps->end - ps->p >= 5 && !memcmp(ps->p, "false", 5)) {
        ps->p += 5;
        result = vc_json_new_bool(false);
    } else if (c == 'n' && ps->end - ps->p >= 4 && !memcmp(ps->p, "null", 4)) {
        ps->p += 4;
        result = vc_json_new_null();
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        char *endp = NULL;
        /* strtod stops at the first non-number char; the slice is NUL
         * terminated by the parse entry points. */
        double d = strtod(ps->p, &endp);
        if (endp && endp != ps->p) {
            ps->p = endp;
            result = vc_json_new_num(d);
        }
    }

    ps->depth--;
    return result;
}

vc_json *vc_json_parse_len(const char *text, size_t len)
{
    if (!text) return NULL;
    /* copy so strtod sees a terminated buffer even for slices */
    char *copy = vc_strndup(text, len);
    if (!copy) return NULL;

    parser ps = { copy, copy + strlen(copy), 0 };
    vc_json *v = parse_value(&ps);
    if (v) {
        skip_ws(&ps);
        if (ps.p != ps.end) { vc_json_free(v); v = NULL; } /* trailing junk */
    }
    vc_free(copy);
    return v;
}

vc_json *vc_json_parse(const char *text)
{
    return text ? vc_json_parse_len(text, strlen(text)) : NULL;
}

/* ------------------------------------------------------------------ */
/* Writer                                                              */
/* ------------------------------------------------------------------ */

int vc_json_escape_str(const char *s, vc_buf *out)
{
    vc_buf_append_char(out, '"');
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        switch (*p) {
        case '"':  vc_buf_append_str(out, "\\\""); break;
        case '\\': vc_buf_append_str(out, "\\\\"); break;
        case '\b': vc_buf_append_str(out, "\\b");  break;
        case '\f': vc_buf_append_str(out, "\\f");  break;
        case '\n': vc_buf_append_str(out, "\\n");  break;
        case '\r': vc_buf_append_str(out, "\\r");  break;
        case '\t': vc_buf_append_str(out, "\\t");  break;
        default:
            if (*p < 0x20)
                vc_buf_appendf(out, "\\u%04x", *p);
            else
                vc_buf_append_char(out, (char)*p);
        }
    }
    return vc_buf_append_char(out, '"');
}

static int write_number(double n, vc_buf *out)
{
    if (isnan(n) || isinf(n))
        return vc_buf_append_str(out, "null");
    double r = floor(n);
    if (r == n && n >= -9007199254740992.0 && n <= 9007199254740992.0)
        return vc_buf_appendf(out, "%lld", (long long)n);
    return vc_buf_appendf(out, "%.17g", n);
}

int vc_json_write_buf(const vc_json *v, vc_buf *out)
{
    if (!v) return vc_buf_append_str(out, "null");
    switch (v->type) {
    case VC_JSON_NULL:   return vc_buf_append_str(out, "null");
    case VC_JSON_BOOL:   return vc_buf_append_str(out, v->boolean ? "true" : "false");
    case VC_JSON_NUMBER: return write_number(v->number, out);
    case VC_JSON_STRING: return vc_json_escape_str(v->string, out);
    case VC_JSON_ARRAY: {
        vc_buf_append_char(out, '[');
        for (vc_json *it = v->child; it; it = it->next) {
            if (it != v->child) vc_buf_append_char(out, ',');
            int rc = vc_json_write_buf(it, out);
            if (rc != VC_OK) return rc;
        }
        return vc_buf_append_char(out, ']');
    }
    case VC_JSON_OBJECT: {
        vc_buf_append_char(out, '{');
        for (vc_json *it = v->child; it; it = it->next) {
            if (it != v->child) vc_buf_append_char(out, ',');
            vc_json_escape_str(it->key ? it->key : "", out);
            vc_buf_append_char(out, ':');
            int rc = vc_json_write_buf(it, out);
            if (rc != VC_OK) return rc;
        }
        return vc_buf_append_char(out, '}');
    }
    }
    return VC_E_INVALID_ARG;
}

char *vc_json_write(const vc_json *v)
{
    vc_buf b;
    vc_buf_init(&b);
    if (vc_json_write_buf(v, &b) != VC_OK) {
        vc_buf_free(&b);
        return NULL;
    }
    return vc_buf_take(&b);
}
