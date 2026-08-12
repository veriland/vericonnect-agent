/* vc_str.h - growable string/byte buffer. */
#ifndef VC_STR_H
#define VC_STR_H

#include "vc_common.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_buf {
    char  *data;  /* always NUL terminated (data[len] == 0) when non-NULL */
    size_t len;
    size_t cap;
} vc_buf;

void  vc_buf_init(vc_buf *b);
void  vc_buf_free(vc_buf *b);
int   vc_buf_reserve(vc_buf *b, size_t extra);
int   vc_buf_append(vc_buf *b, const void *data, size_t n);
int   vc_buf_append_str(vc_buf *b, const char *s);
int   vc_buf_append_char(vc_buf *b, char c);
int   vc_buf_appendf(vc_buf *b, const char *fmt, ...);
/* Detach the buffer (caller owns, free with vc_free). Resets b. */
char *vc_buf_take(vc_buf *b);
void  vc_buf_clear(vc_buf *b);
/* Remove the first n bytes. */
void  vc_buf_consume(vc_buf *b, size_t n);

/* Case-insensitive ASCII compare. */
int vc_stricmp(const char *a, const char *b);
int vc_strnicmp(const char *a, const char *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif
