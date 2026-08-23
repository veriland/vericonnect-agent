/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_sas.h"
#include "vc/vc_sha256.h"
#include "vc/vc_base64.h"
#include "vc/vc_url.h"

#include <cctype>
#include <ctime>

namespace vc
{
    std::string sas_token(std::string_view namespace_host, std::string_view entity_path,
                          std::string_view key_name, std::string_view key, unsigned ttl_seconds)
    {
        if (namespace_host.empty() || entity_path.empty() || key_name.empty() || key.empty())
            return {};
        if (ttl_seconds == 0) ttl_seconds = 3600;

        /* resource URI, lowercased: http://{ns}/{path} */
        std::string uri = "http://";
        uri.append(namespace_host);
        uri += '/';
        uri.append(entity_path);
        for (char& c : uri)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        std::string enc_uri = url_encode(uri);

        auto expiry = static_cast<unsigned long long>(std::time(nullptr)) + ttl_seconds;

        std::string sts = enc_uri + "\n" + std::to_string(expiry);

        Sha256Digest digest = hmac_sha256(key, sts);
        std::string sig_enc = url_encode(base64_encode(std::span<const std::uint8_t>(digest)));

        return "SharedAccessSignature sr=" + enc_uri + "&sig=" + sig_enc +
               "&se=" + std::to_string(expiry) + "&skn=" + std::string(key_name);
    }
} // namespace vc
