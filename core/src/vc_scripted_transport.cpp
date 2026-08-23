/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_scripted_transport.h"

#include <algorithm>
#include <cstring>

namespace vc
{
    void ScriptedWire::push_incoming(std::span<const std::uint8_t> data)
    {
        in_.insert(in_.end(), data.begin(), data.end());
    }

    void ScriptedWire::push_incoming(std::string_view text)
    {
        const auto* p = reinterpret_cast<const std::uint8_t*>(text.data());
        in_.insert(in_.end(), p, p + text.size());
    }

    std::string_view ScriptedWire::outgoing_text() const noexcept
    {
        return std::string_view(reinterpret_cast<const char*>(out_.data()), out_.size());
    }

    Status ScriptedWire::send(std::span<const std::uint8_t> data)
    {
        if (closed_) return std::unexpected(Error::Closed);
        if (fail_send_)
        {
            fail_send_ = false;
            return std::unexpected(Error::Io);
        }
        out_.insert(out_.end(), data.begin(), data.end());
        return {};
    }

    Result<std::size_t> ScriptedWire::recv(std::span<std::uint8_t> buf)
    {
        if (closed_) return std::unexpected(Error::Closed);
        const std::size_t avail = in_.size() - read_pos_;
        if (avail == 0)
        {
            /* Nothing scripted left: either the peer closed cleanly, or the
             * test wants the caller to see a timeout and retry. */
            if (eof_) return std::size_t{0};
            return std::unexpected(Error::Timeout);
        }
        const std::size_t n = std::min(avail, buf.size());
        std::memcpy(buf.data(), in_.data() + read_pos_, n);
        read_pos_ += n;
        return n;
    }
} // namespace vc
