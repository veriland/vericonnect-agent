/*
 * vc_ws.h - RFC 6455 WebSocket client over vc::Tls.
 * Messages are reassembled from fragments; ping/pong is handled internally
 * by recv().
 */
#ifndef VC_WS_H
#define VC_WS_H

#include "vc_common.h"
#include "vc_tls.h"

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace vc
{
    class WebSocket
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

        WebSocket() noexcept = default;
        WebSocket(WebSocket&&) noexcept = default;
        WebSocket& operator=(WebSocket&&) noexcept = default;
        WebSocket(const WebSocket&) = delete;
        WebSocket& operator=(const WebSocket&) = delete;

        /* Connect + upgrade. path_and_query e.g. "/$hc/name?sb-hc-action=listen".
         * extra_headers: optional "Header: value\r\n" lines. */
        [[nodiscard]] static Result<WebSocket> connect(const std::string& host, int port,
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
            return tls_.valid();
        }

    private:
        explicit WebSocket(Tls tls) noexcept : tls_(std::move(tls)) {}

        [[nodiscard]] Result<std::size_t> read_more(int timeout_ms);
        [[nodiscard]] Status send_frame(std::uint8_t opcode, bool fin,
                                        std::span<const std::uint8_t> data);

        Tls tls_;
        Bytes in_; /* raw undecoded incoming bytes */
        bool closed_ = false;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
