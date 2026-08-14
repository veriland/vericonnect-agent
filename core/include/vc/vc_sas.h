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

#include <string>
#include <string_view>

namespace vc
{
    /*
     * namespace_host : e.g. "contoso.servicebus.windows.net"
     * entity_path    : e.g. "myhybridconnection"
     * ttl_seconds    : lifetime from now (0 -> 3600).
     * Returns the token, or an empty string on invalid input.
     */
    std::string sas_token(std::string_view namespace_host, std::string_view entity_path,
                          std::string_view key_name, std::string_view key, unsigned ttl_seconds);
} // namespace vc

#endif /* __cplusplus */

#endif
