/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_base64.h"

namespace vc
{
    namespace
    {
        constexpr char kTab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        int b64_val(char c) noexcept
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        }
    } // namespace

    std::string base64_encode(std::span<const std::uint8_t> data)
    {
        const std::uint8_t* p = data.data();
        std::size_t len = data.size();
        std::string out;
        out.reserve(((len + 2) / 3) * 4);

        std::size_t i = 0;
        while (i + 3 <= len)
        {
            std::uint32_t v = static_cast<std::uint32_t>(p[i]) << 16 |
                              static_cast<std::uint32_t>(p[i + 1]) << 8 | p[i + 2];
            out += kTab[(v >> 18) & 63];
            out += kTab[(v >> 12) & 63];
            out += kTab[(v >> 6) & 63];
            out += kTab[v & 63];
            i += 3;
        }
        std::size_t rem = len - i;
        if (rem == 1)
        {
            std::uint32_t v = static_cast<std::uint32_t>(p[i]) << 16;
            out += kTab[(v >> 18) & 63];
            out += kTab[(v >> 12) & 63];
            out += '=';
            out += '=';
        }
        else if (rem == 2)
        {
            std::uint32_t v =
                static_cast<std::uint32_t>(p[i]) << 16 | static_cast<std::uint32_t>(p[i + 1]) << 8;
            out += kTab[(v >> 18) & 63];
            out += kTab[(v >> 12) & 63];
            out += kTab[(v >> 6) & 63];
            out += '=';
        }
        return out;
    }

    std::string base64_encode(std::string_view s)
    {
        return base64_encode(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
    }

    std::optional<Bytes> base64_decode(std::string_view text)
    {
        Bytes out;
        out.reserve(text.size() / 4 * 3 + 3);

        std::uint32_t acc = 0;
        int bits = 0;
        bool done = false;

        for (char c : text)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            if (c == '=')
            {
                done = true;
                continue;
            }
            if (done) return std::nullopt; /* data after padding */
            int v = b64_val(c);
            if (v < 0) return std::nullopt;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<std::uint8_t>(acc >> bits));
            }
        }
        /* 6 leftover bits cannot be part of any whole byte, so the input was
         * truncated. Returning an empty success would hide that. */
        if (bits == 6) return std::nullopt;
        return out;
    }
} // namespace vc
