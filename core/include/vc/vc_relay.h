/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_relay.h - Azure Relay Hybrid Connections listener.
 *
 * Implements the Hybrid Connections WebSocket protocol:
 *   - control channel  wss://{ns}/$hc/{path}?sb-hc-action=listen&sb-hc-token=...
 *   - 'request' messages (HTTP over relay), body over control channel
 *   - responses over the control channel (small) or a rendezvous
 *     connection (large bodies)
 *   - proactive SAS token renewal ('renewToken')
 *   - automatic reconnect with backoff
 */
#ifndef VC_RELAY_H
#define VC_RELAY_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace vc
{
    struct RelayConfig
    {
        std::string namespace_host; /* contoso.servicebus.windows.net */
        std::string hybrid_connection;
        std::string key_name;
        std::string key;
        unsigned token_ttl_seconds = 0; /* 0 = default 3600 */
    };

    struct RelayRequest
    {
        std::string_view id;
        std::string_view method;
        std::string_view target;       /* requestTarget */
        std::string_view headers_json; /* serialized requestHeaders, or empty */
        std::span<const std::uint8_t> body;
    };

    struct RelayResponse
    {
        int status_code = 500;
        std::string status_desc = "Internal Server Error";
        Bytes body;
        std::string content_type; /* default application/json */
    };

    struct RelayCallbacks
    {
        using request_fn = std::function<bool(const RelayRequest& req, RelayResponse& resp)>;
        using event_fn =
            std::function<void(std::string_view event, int code, std::string_view desc)>;

        /* Fill resp; return true if resp is valid. */
        request_fn on_request;
        /* Informational events (connected, disconnected, renew, errors). */
        event_fn on_event;
    };

    /*
     * Run the listener loop until stop_requested() returns true. Blocking;
     * reconnects on failure. Returns success on a requested stop.
     */
    [[nodiscard]] Status relay_listen(const RelayConfig& cfg, const RelayCallbacks& cb,
                                      const std::function<bool()>& stop_requested);

    /*
     * As relay_listen, but with the dialler injected. The listener opens new
     * connections mid-stream for large responses, so the dialler - not just a
     * transport - is what a test has to substitute. See vc_relay_testing.h for
     * ScriptedDialler; production code wants relay_listen above.
     */
    template <class Dialler>
    [[nodiscard]] Status relay_listen_with(const RelayConfig& cfg, const RelayCallbacks& cb,
                                           Dialler dialler,
                                           const std::function<bool()>& stop_requested);
} // namespace vc

#endif /* __cplusplus */

#endif
