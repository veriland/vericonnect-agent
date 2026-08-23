/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/* vc_url.h - URL percent-encoding and URL parsing. */
#ifndef VC_URL_H
#define VC_URL_H

#include "vc_common.h"

#ifdef __cplusplus

#include <string>
#include <string_view>

namespace vc
{
    /* Percent-encodes everything except unreserved characters
     * (A-Z a-z 0-9 - _ . ~). */
    std::string url_encode(std::string_view s);

    struct Url
    {
        std::string scheme; /* "wss", "https", ... */
        std::string host;
        int port = 0;      /* resolved default (443 for wss/https) if absent */
        std::string path;  /* includes leading '/', without query            */
        std::string query; /* without '?', empty if absent                   */
    };

    /* Parse an absolute URL. */
    [[nodiscard]] Result<Url> url_parse(std::string_view url);
} // namespace vc

#endif /* __cplusplus */

#endif
