/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_scripted_transport.h - in-memory vc::Transport for tests.
 *
 * Queue what a peer would send, run the protocol code, inspect what it wrote.
 * The buffers sit in a separate ScriptedWire because protocol objects take
 * their transport by value: once moved in it is out of the test's reach, so
 * the test keeps the wire. See apps/selftest for use.
 *
 * It lives in core/ because vc_ws.cpp instantiates WebSocketT for it, which
 * is what keeps the template definitions out of the public headers.
 */
#ifndef VC_SCRIPTED_TRANSPORT_H
#define VC_SCRIPTED_TRANSPORT_H

#include "vc_common.h"
#include "vc_transport.h"

#ifdef __cplusplus

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace vc
{
    /* The shared byte buffers behind a ScriptedTransport. */
    class ScriptedWire
    {
    public:
        /* ---- scripting side, driven by the test ---- */

        /* Queue bytes for the code under test to read. */
        void push_incoming(std::span<const std::uint8_t> data);
        void push_incoming(std::string_view text);

        /* Everything the code under test has written so far. */
        [[nodiscard]] const Bytes& outgoing() const noexcept
        {
            return out_;
        }
        [[nodiscard]] std::string_view outgoing_text() const noexcept;
        void clear_outgoing() noexcept
        {
            out_.clear();
        }

        /* Once drained, report an orderly close rather than a timeout. */
        void set_eof() noexcept
        {
            eof_ = true;
        }
        /* Make the next send() fail with Error::Io, once. */
        void fail_next_send() noexcept
        {
            fail_send_ = true;
        }
        [[nodiscard]] std::size_t unread() const noexcept
        {
            return in_.size() - read_pos_;
        }
        [[nodiscard]] bool closed() const noexcept
        {
            return closed_;
        }

        /* ---- transport side, driven by the code under test ---- */

        [[nodiscard]] Status send(std::span<const std::uint8_t> data);
        [[nodiscard]] Result<std::size_t> recv(std::span<std::uint8_t> buf);
        void close() noexcept
        {
            closed_ = true;
        }

    private:
        Bytes in_;
        Bytes out_;
        std::size_t read_pos_ = 0;
        bool closed_ = false;
        bool eof_ = false;
        bool fail_send_ = false;
    };

    /* A vc::Transport view onto a ScriptedWire; every copy shares the wire. A
     * default-constructed one holds none and reports valid() == false, which
     * is useful for the "transport is dead" paths. */
    class ScriptedTransport
    {
    public:
        ScriptedTransport() noexcept = default;
        explicit ScriptedTransport(std::shared_ptr<ScriptedWire> wire) noexcept
            : wire_(std::move(wire))
        {
        }

        [[nodiscard]] const std::shared_ptr<ScriptedWire>& wire() const noexcept
        {
            return wire_;
        }

        [[nodiscard]] Status send(std::span<const std::uint8_t> data)
        {
            if (!wire_) return std::unexpected(Error::Closed);
            return wire_->send(data);
        }
        [[nodiscard]] Result<std::size_t> recv(std::span<std::uint8_t> buf, int /*timeout_ms*/)
        {
            if (!wire_) return std::unexpected(Error::Closed);
            return wire_->recv(buf);
        }
        void close()
        {
            if (wire_) wire_->close();
        }
        [[nodiscard]] bool valid() const noexcept
        {
            return wire_ && !wire_->closed();
        }

    private:
        std::shared_ptr<ScriptedWire> wire_;
    };

    static_assert(Transport<ScriptedTransport>, "vc::ScriptedTransport must satisfy vc::Transport");
} // namespace vc

#endif /* __cplusplus */

#endif
