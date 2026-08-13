#include "vc/vc_sas.h"
#include "vc/vc_sha256.h"
#include "vc/vc_base64.h"
#include "vc/vc_url.h"
#include "vc/vc_str.h"
#include <stdio.h>
#include <time.h>
#include <ctype.h>

char *vc_sas_token(const char *namespace_host, const char *entity_path,
                   const char *key_name, const char *key,
                   unsigned ttl_seconds)
{
    if (!namespace_host || !entity_path || !key_name || !key)
        return NULL;
    if (ttl_seconds == 0) ttl_seconds = 3600;

    /* resource URI, lowercased: http://{ns}/{path} */
    vc_buf uri;
    vc_buf_init(&uri);
    vc_buf_appendf(&uri, "http://%s/%s", namespace_host, entity_path);
    for (size_t i = 0; i < uri.len; i++)
        uri.data[i] = (char)tolower((unsigned char)uri.data[i]);

    char *enc_uri = vc_url_encode(uri.data);
    vc_buf_free(&uri);
    if (!enc_uri) return NULL;

    unsigned long long expiry =
        (unsigned long long)time(NULL) + ttl_seconds;

    vc_buf sts; /* string to sign */
    vc_buf_init(&sts);
    vc_buf_appendf(&sts, "%s\n%llu", enc_uri, expiry);

    uint8_t digest[VC_SHA256_DIGEST_LEN];
    vc_hmac_sha256(key, strlen(key), sts.data, sts.len, digest);
    vc_buf_free(&sts);

    char *sig_b64 = vc_base64_encode(digest, sizeof digest);
    if (!sig_b64) { vc_free(enc_uri); return NULL; }
    char *sig_enc = vc_url_encode(sig_b64);
    vc_free(sig_b64);
    if (!sig_enc) { vc_free(enc_uri); return NULL; }

    vc_buf tok;
    vc_buf_init(&tok);
    vc_buf_appendf(&tok,
        "SharedAccessSignature sr=%s&sig=%s&se=%llu&skn=%s",
        enc_uri, sig_enc, expiry, key_name);

    vc_free(enc_uri);
    vc_free(sig_enc);
    return vc_buf_take(&tok);
}
