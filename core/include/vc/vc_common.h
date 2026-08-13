/*
 * vc_common.h - shared basics for the VeriConnect C code base.
 *
 * All strings crossing module boundaries are UTF-8 encoded, NUL
 * terminated char*. Platform layers convert to native encodings
 * (UTF-16 on Windows) internally.
 */
#ifndef VC_COMMON_H
#define VC_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generic result codes (negative = error). */
typedef enum vc_err {
    VC_OK             = 0,
    VC_E_FAIL         = -1,
    VC_E_NOMEM        = -2,
    VC_E_INVALID_ARG  = -3,
    VC_E_NOT_FOUND    = -4,
    VC_E_IO           = -5,
    VC_E_TIMEOUT      = -6,
    VC_E_CLOSED       = -7,
    VC_E_PROTOCOL     = -8,
    VC_E_TLS          = -9,
    VC_E_EXISTS       = -10,
    VC_E_UNSUPPORTED  = -11
} vc_err;

/* malloc that mirrors the adapter ABI contract: buffers returned by an
 * adapter are allocated with vc_alloc and released with vc_free. */
void *vc_alloc(size_t n);
void *vc_realloc(void *p, size_t n);
void  vc_free(void *p);
char *vc_strdup(const char *s);
char *vc_strndup(const char *s, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* VC_COMMON_H */
