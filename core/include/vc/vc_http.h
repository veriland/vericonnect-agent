/*
 * vc_http.h - minimal HTTPS/1.1 client (used by the sender side of the
 * test app to POST commands through the Azure Relay HTTP endpoint).
 */
#ifndef VC_HTTP_H
#define VC_HTTP_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_http_response {
    int     status;
    char   *status_text;   /* vc_free */
    char   *headers;       /* raw header block, vc_free */
    uint8_t*body;          /* vc_free */
    size_t  body_len;
} vc_http_response;

/*
 * Performs a single HTTPS request.
 * extra_headers: optional "Header: value\r\n" lines (may be NULL).
 * body may be NULL for GET.
 */
int vc_http_request(const char *method, const char *host, int port,
                    const char *path_and_query,
                    const char *extra_headers,
                    const void *body, size_t body_len,
                    const char *content_type,
                    int timeout_ms,
                    vc_http_response *out);

void vc_http_response_free(vc_http_response *r);

#ifdef __cplusplus
}
#endif

#endif
