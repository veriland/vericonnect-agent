/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_http.h - minimal HTTPS/1.1 client (used by the sender side of the test
 * app to POST commands through the Azure Relay HTTP endpoint).
 */
#ifndef VC_HTTP_H
#define VC_HTTP_H

#include "vc_common.h"
#include "vc_transport.h"

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace vc::http
{
    struct Response
    {
        int status = 0;
        std::string status_text;
        std::string headers; /* raw header block */
        Bytes body;
    };

    struct Request
    {
        std::string_view method;
        std::string_view host;
        int port = 443;
        std::string_view path_and_query;
        std::string_view extra_headers;     /* "H: v\r\n" lines, optional */
        std::span<const std::uint8_t> body; /* may be empty               */
        std::string_view content_type;      /* default application/json   */
        int timeout_ms = 30000;
    };

    /*
     * Run one request/response exchange over an already-connected transport,
     * which is borrowed rather than owned. Split out from request() so the
     * response parser - status line, headers, Content-Length and chunked
     * bodies - can be tested without a network (DESIGN.md §8).
     */
    template <Transport T>
    [[nodiscard]] Result<Response> exchange(T& transport, const Request& req);

    /* Perform a single HTTPS request: dial, wrap in TLS, exchange. The only
     * entry point here that opens a connection. */
    [[nodiscard]] Result<Response> request(const Request& req);
} // namespace vc::http

#endif /* __cplusplus */

#endif
