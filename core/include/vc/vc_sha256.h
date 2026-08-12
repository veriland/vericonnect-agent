/* vc_sha256.h - SHA-256 and HMAC-SHA256 (portable, no OS deps). */
#ifndef VC_SHA256_H
#define VC_SHA256_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VC_SHA256_DIGEST_LEN 32

typedef struct vc_sha256_ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   buflen;
} vc_sha256_ctx;

void vc_sha256_init(vc_sha256_ctx *ctx);
void vc_sha256_update(vc_sha256_ctx *ctx, const void *data, size_t len);
void vc_sha256_final(vc_sha256_ctx *ctx, uint8_t digest[VC_SHA256_DIGEST_LEN]);
void vc_sha256(const void *data, size_t len, uint8_t digest[VC_SHA256_DIGEST_LEN]);

void vc_hmac_sha256(const void *key, size_t key_len,
                    const void *msg, size_t msg_len,
                    uint8_t digest[VC_SHA256_DIGEST_LEN]);

#ifdef __cplusplus
}
#endif

#endif
