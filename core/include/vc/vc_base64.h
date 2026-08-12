/* vc_base64.h - RFC 4648 base64 encode/decode. */
#ifndef VC_BASE64_H
#define VC_BASE64_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns malloc'd NUL terminated string (vc_free). NULL on OOM. */
char *vc_base64_encode(const void *data, size_t len);

/* Returns malloc'd buffer (vc_free), sets *out_len. NULL on invalid
 * input. Whitespace in the input is ignored. */
uint8_t *vc_base64_decode(const char *text, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
