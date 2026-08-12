/* vc_url.h - URL percent-encoding and URL parsing. */
#ifndef VC_URL_H
#define VC_URL_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Percent-encodes everything except unreserved characters
 * (A-Z a-z 0-9 - _ . ~). Returns malloc'd string (vc_free). */
char *vc_url_encode(const char *s);

typedef struct vc_url {
    char *scheme;   /* "wss", "https", ... */
    char *host;
    int   port;     /* resolved default (443 for wss/https) if absent */
    char *path;     /* includes leading '/', without query            */
    char *query;    /* without '?', may be NULL                       */
} vc_url;

/* Parse an absolute URL. Returns VC_OK / VC_E_INVALID_ARG. */
int  vc_url_parse(const char *url, vc_url *out);
void vc_url_free(vc_url *u);

#ifdef __cplusplus
}
#endif

#endif
