/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_common.h"

#include <charconv>
#include "vc/vc_transport.h"
#include "vc/vc_tls.h"
#include "vc/vc_sock.h"

#include <cstdlib>

namespace vc
{
    std::optional<std::uint64_t> parse_uint(std::string_view text, std::uint64_t max,
                                            int base) noexcept
    {
        if (text.empty()) return std::nullopt;
        if (text.front() == '-' || text.front() == '+') return std::nullopt;
        std::uint64_t v = 0;
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const auto r = std::from_chars(first, last, v, base);
        if (r.ec != std::errc{} || r.ptr != last) return std::nullopt;
        if (v > max) return std::nullopt;
        return v;
    }
} // namespace vc

namespace vc
{
    std::string_view error_str(Error e) noexcept
    {
        switch (e)
        {
        case Error::Fail:
            return "failure";
        case Error::NoMem:
            return "out of memory";
        case Error::InvalidArg:
            return "invalid argument";
        case Error::NotFound:
            return "not found";
        case Error::Io:
            return "I/O error";
        case Error::Timeout:
            return "timeout";
        case Error::Closed:
            return "closed";
        case Error::Protocol:
            return "protocol error";
        case Error::Tls:
            return "TLS error";
        case Error::Exists:
            return "already exists";
        case Error::Unsupported:
            return "unsupported";
        }
        return "unknown error";
    }
} // namespace vc

/* Adapter-ABI allocator boundary (see vc_common.h). */
extern "C"
{
    void* vc_alloc(size_t n)
    {
        return std::malloc(n ? n : 1);
    }
    void vc_free(void* p)
    {
        std::free(p);
    }
} // extern "C"

namespace vc
{
    /* Asserted here so a signature drift in either platform layer fails the
     * build with a clear message, not deep inside the protocol templates. */
    static_assert(Transport<Tls>, "vc::Tls must satisfy vc::Transport");
    static_assert(Transport<Socket>, "vc::Socket must satisfy vc::Transport");
} // namespace vc
