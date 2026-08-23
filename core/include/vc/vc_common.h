/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_common.h - shared basics for the VeriConnect code base.
 *
 * All strings crossing module boundaries are UTF-8 encoded. Platform layers
 * convert to native encodings (UTF-16 on Windows) internally.
 *
 * This header exposes two surfaces during the C++ migration:
 *   - The modern C++ vocabulary in namespace vc (Error, Result, Bytes, ...).
 *   - A small C-ABI allocator boundary (vc_alloc/vc_free/...) that is kept for
 *     the adapter DLL contract, where a buffer allocated on one side is freed
 *     on the other.
 */
#ifndef VC_COMMON_H
#define VC_COMMON_H

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vc
{
    /* Generic result codes (negative = error). */
    enum class Error : int
    {
        Fail = -1,
        NoMem = -2,
        InvalidArg = -3,
        NotFound = -4,
        Io = -5,
        Timeout = -6,
        Closed = -7,
        Protocol = -8,
        Tls = -9,
        Exists = -10,
        Unsupported = -11,
    };

    /* A value of type T on success, or an Error. */
    template <class T> using Result = std::expected<T, Error>;

    /* Success or an Error, carrying no value. */
    using Status = std::expected<void, Error>;

    /* Owned byte buffer. */
    using Bytes = std::vector<std::uint8_t>;

    /* Short human-readable name for an error code (for logging/diagnostics). */
    std::string_view error_str(Error e) noexcept;

    /*
     * Parse a whole unsigned integer, or nothing. Rejects a partial parse, a
     * sign, an empty string and anything above max. Use this for any value
     * that arrives from off the machine: atoi and atol report neither
     * overflow nor "not a number", and their result for "-1" survives a cast
     * to an unsigned type as a very large number.
     */
    [[nodiscard]] std::optional<std::uint64_t> parse_uint(std::string_view text, std::uint64_t max,
                                                          int base = 10) noexcept;
} // namespace vc

#endif /* __cplusplus */

/* ------------------------------------------------------------------------
 * Allocator for the adapter ABI. Memory never crosses the boundary to be
 * freed: an adapter allocates its result here and frees it in its own
 * FreeAdapterString, which the host calls. That is what makes the contract
 * safe, because vc_core is linked statically into both the host and each
 * adapter, so each has its own copy of these functions and its own heap.
 * Never free here what the other side allocated. Not for general use -
 * prefer std:: containers in new code.
 * ---------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif

    void* vc_alloc(size_t n);
    void vc_free(void* p);

#ifdef __cplusplus
}
#endif

#endif /* VC_COMMON_H */
