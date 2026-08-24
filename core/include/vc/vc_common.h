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
    /* Generic result categories (negative = error). */
    enum class ErrorCode : int
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

    /*
     * A failure: the category, and the platform's own code for it where a
     * platform call is what failed. The category alone is too coarse to
     * diagnose from - Io is equally true of a refused connection, a peer that
     * hung up and a full disk - and logging the OS code at the point of
     * failure only helps whoever can read that machine's log. Carrying it here
     * puts it where the caller can also act on it.
     *
     * os_code() is errno on POSIX and GetLastError on Windows (WSAGetLastError
     * returns the same value), which is the one code space os::error_text()
     * renders. It is 0 when no platform call was involved - a protocol
     * violation, a cap we enforce ourselves - and, for now, for Error::Tls:
     * SChannel's SECURITY_STATUS and OpenSSL's error queue are separate code
     * spaces and each needs its own renderer to be worth carrying.
     *
     * Implicitly constructible from ErrorCode, so `Error::Io` and
     * `std::unexpected(Error::Io)` read and compile as they always have.
     */
    class Error
    {
    public:
        /* The categories, kept spelled `Error::Io` at every call site. */
        static constexpr ErrorCode Fail = ErrorCode::Fail;
        static constexpr ErrorCode NoMem = ErrorCode::NoMem;
        static constexpr ErrorCode InvalidArg = ErrorCode::InvalidArg;
        static constexpr ErrorCode NotFound = ErrorCode::NotFound;
        static constexpr ErrorCode Io = ErrorCode::Io;
        static constexpr ErrorCode Timeout = ErrorCode::Timeout;
        static constexpr ErrorCode Closed = ErrorCode::Closed;
        static constexpr ErrorCode Protocol = ErrorCode::Protocol;
        static constexpr ErrorCode Tls = ErrorCode::Tls;
        static constexpr ErrorCode Exists = ErrorCode::Exists;
        static constexpr ErrorCode Unsupported = ErrorCode::Unsupported;

        constexpr Error(ErrorCode code) noexcept : code_(code) {}
        constexpr Error(ErrorCode code, std::uint32_t os_code) noexcept
            : code_(code), os_code_(os_code)
        {
        }

        [[nodiscard]] constexpr ErrorCode code() const noexcept
        {
            return code_;
        }

        [[nodiscard]] constexpr std::uint32_t os_code() const noexcept
        {
            return os_code_;
        }

        /*
         * Comparing against a category ignores the OS code: control flow
         * turns on "was this a timeout", never on which timeout. Comparing two
         * Errors is full value equality, because that is what a value type
         * comparing to itself should mean.
         */
        friend constexpr bool operator==(const Error& a, ErrorCode b) noexcept
        {
            return a.code_ == b;
        }

        friend constexpr bool operator==(const Error& a, const Error& b) noexcept
        {
            return a.code_ == b.code_ && a.os_code_ == b.os_code_;
        }

    private:
        ErrorCode code_;
        std::uint32_t os_code_ = 0;
    };

    /* A value of type T on success, or an Error. */
    template <class T> using Result = std::expected<T, Error>;

    /* Success or an Error, carrying no value. */
    using Status = std::expected<void, Error>;

    /* Owned byte buffer. */
    using Bytes = std::vector<std::uint8_t>;

    /* Short human-readable name for an error category (for logging/diagnostics). */
    std::string_view error_str(Error e) noexcept;

    /*
     * error_str plus the platform code and its text when one was captured:
     * "I/O error (61: Connection refused)", or just "I/O error" when it was
     * not. This is the form to log; error_str alone is the form to fit in a
     * fixed field.
     */
    [[nodiscard]] std::string error_detail(Error e);

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
