/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_ws.h - RFC 6455 WebSocket client over any vc::Transport.
 * Messages are reassembled from fragments; ping/pong is handled internally
 * by recv(). WebSocket is the production instantiation over vc::Tls.
 *
 * Construction is split: upgrade() runs the HTTP/1.1 upgrade over an
 * already-connected transport, ws_connect() dials and wraps in TLS first.
 * Keeping the dial in one place is what makes the framing testable.
 */
#ifndef VC_WS_H
#define VC_WS_H

#include "vc_common.h"
#include "vc_tls.h"
#include "vc_transport.h"

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace vc
{
    template <Transport T> class WebSocketT
    {
    public:
        enum class MsgType
        {
            Text = 1,
            Binary = 2,
            Close = 8
        };

        struct Message
        {
            MsgType type;
            Bytes payload;
        };

        WebSocketT() noexcept = default;
        WebSocketT(WebSocketT&&) noexcept = default;
        WebSocketT& operator=(WebSocketT&&) noexcept = default;
        WebSocketT(const WebSocketT&) = delete;
        WebSocketT& operator=(const WebSocketT&) = delete;

        /* HTTP/1.1 upgrade over an already-connected transport, which it takes
         * ownership of. host fills the Host header; path_and_query e.g.
         * "/$hc/name?sb-hc-action=listen"; extra_headers is optional
         * "Header: value\r\n" lines. */
        [[nodiscard]] static Result<WebSocketT> upgrade(T transport, const std::string& host,
                                                        const std::string& path_and_query,
                                                        std::string_view extra_headers,
                                                        int timeout_ms);

        /* Send one complete message (auto-fragments large payloads). */
        [[nodiscard]] Status send(MsgType type, std::span<const std::uint8_t> data);
        [[nodiscard]] Status send_ping();
        [[nodiscard]] Status send_close(std::uint16_t code);

        /*
         * Receive the next data message. Control frames (ping/pong) are handled
         * transparently. A peer close yields a Message with type == Close (and the
         * connection is marked closed); Error::Timeout if none arrived in time.
         */
        [[nodiscard]] Result<Message> recv(int timeout_ms);

        void close();
        bool valid() const noexcept
        {
            return transport_.valid();
        }

    private:
        explicit WebSocketT(T transport) noexcept : transport_(std::move(transport)) {}

        [[nodiscard]] Result<std::size_t> read_more(int timeout_ms);
        [[nodiscard]] Status send_frame(std::uint8_t opcode, bool fin,
                                        std::span<const std::uint8_t> data);

        T transport_;
        Bytes in_; /* raw undecoded incoming bytes */
        bool closed_ = false;
    };

    /* The production WebSocket: RFC 6455 over TLS. */
    using WebSocket = WebSocketT<Tls>;

    /* Dial host:port, wrap in TLS, upgrade. The only entry point here that
     * opens a connection. */
    [[nodiscard]] Result<WebSocket> ws_connect(const std::string& host, int port,
                                               const std::string& path_and_query,
                                               std::string_view extra_headers, int timeout_ms);
} // namespace vc

#endif /* __cplusplus */

#endif
