/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_url.h"

#include <cctype>
#include <cstdlib>

namespace vc
{
    namespace
    {
        bool is_unreserved(unsigned char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.' || c == '~';
        }

        bool iequals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }
    } // namespace

    std::string url_encode(std::string_view s)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            if (is_unreserved(c))
            {
                out += static_cast<char>(c);
            }
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 0xF];
            }
        }
        return out;
    }

    Result<Url> url_parse(std::string_view url)
    {
        std::size_t sep = url.find("://");
        if (sep == std::string_view::npos) return std::unexpected(Error::InvalidArg);

        Url out;
        out.scheme = std::string(url.substr(0, sep));
        std::string_view rest = url.substr(sep + 3);

        std::size_t i = 0;
        while (i < rest.size() && rest[i] != ':' && rest[i] != '/' && rest[i] != '?')
            i++;
        if (i == 0) return std::unexpected(Error::InvalidArg);
        out.host = std::string(rest.substr(0, i));

        if (i < rest.size() && rest[i] == ':')
        {
            i++;
            /* The rendezvous address comes from the relay service, so the port
             * is off-machine input: reject anything outside 1-65535. */
            std::size_t pe = i;
            while (pe < rest.size() && rest[pe] != '/' && rest[pe] != '?')
                pe++;
            const auto port = parse_uint(rest.substr(i, pe - i), 65535);
            if (!port || *port == 0) return std::unexpected(Error::Protocol);
            out.port = static_cast<int>(*port);
            while (i < rest.size() && rest[i] != '/' && rest[i] != '?')
                i++;
        }
        if (out.port == 0)
        {
            if (iequals(out.scheme, "https") || iequals(out.scheme, "wss"))
                out.port = 443;
            else if (iequals(out.scheme, "http") || iequals(out.scheme, "ws"))
                out.port = 80;
            else
                out.port = 443;
        }

        if (i < rest.size() && rest[i] == '/')
        {
            std::size_t path_start = i;
            while (i < rest.size() && rest[i] != '?')
                i++;
            out.path = std::string(rest.substr(path_start, i - path_start));
        }
        else
        {
            out.path = "/";
            if (i < rest.size() && rest[i] != '?') return std::unexpected(Error::InvalidArg);
        }

        if (i < rest.size() && rest[i] == '?') out.query = std::string(rest.substr(i + 1));

        return out;
    }
} // namespace vc
