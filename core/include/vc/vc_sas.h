/*
 * vc_sas.h - Azure Service Bus / Relay Shared Access Signature tokens.
 *
 * Token format:
 *   SharedAccessSignature sr=<url-enc resource>&sig=<url-enc b64 hmac>
 *                          &se=<unix expiry>&skn=<key name>
 * String to sign: url-encoded resource URI + "\n" + expiry.
 */
#ifndef VC_SAS_H
#define VC_SAS_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * namespace_host : e.g. "contoso.servicebus.windows.net"
 * entity_path    : e.g. "myhybridconnection"
 * ttl_seconds    : lifetime from now (e.g. 3600)
 * Returns malloc'd token string (vc_free), NULL on error.
 */
char *vc_sas_token(const char *namespace_host, const char *entity_path,
                   const char *key_name, const char *key,
                   unsigned ttl_seconds);

#ifdef __cplusplus
}
#endif

#endif
