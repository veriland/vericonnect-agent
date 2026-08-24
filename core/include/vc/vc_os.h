/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/* vc_os.h - misc OS helpers implemented per platform. */
#ifndef VC_OS_H
#define VC_OS_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string>

namespace vc::os
{
    /* Sleep for at least ms milliseconds. */
    void sleep_ms(unsigned ms);

    /* Monotonic clock in milliseconds (unaffected by wall-clock changes). */
    std::uint64_t monotonic_ms() noexcept;

    /* Fill buf with cryptographically secure random bytes. */
    [[nodiscard]] Status random_bytes(std::span<std::uint8_t> buf);

    /* Broken-down local wall-clock time, to millisecond precision. */
    struct LocalTime
    {
        int year;        /* full year, e.g. 2026 */
        int month;       /* 1-12                 */
        int day;         /* 1-31                 */
        int hour;        /* 0-23                 */
        int minute;      /* 0-59                 */
        int second;      /* 0-60 (leap second)   */
        int millisecond; /* 0-999                */
    };

    /* Current local time. Keeps the localtime_r / localtime_s split in the
     * platform layer. */
    [[nodiscard]] LocalTime local_time() noexcept;

    /* Filename extension for a shared library on this platform, leading dot
     * included: ".dll", ".dylib" or ".so". */
    [[nodiscard]] const char* shared_library_extension() noexcept;

    /*
     * The last OS error on this thread, raw: errno on POSIX, GetLastError on
     * Windows (WSAGetLastError returns the same value). 0 when there is none.
     *
     * Read this at the point of failure. Almost any intervening call - a
     * close(), a free, a log line - is entitled to overwrite it, so a cleanup
     * path between the failure and the read will lose or falsify it.
     */
    [[nodiscard]] std::uint32_t last_error_code() noexcept;

    /* A code from last_error_code() rendered as "code: text". Empty for 0. */
    [[nodiscard]] std::string error_text(std::uint32_t code);

    /*
     * The last OS error on this thread as "code: text". vc::Error's category
     * says only that I/O failed; this says which I/O failure, which is the
     * difference between a diagnosable field report and a guess.
     */
    [[nodiscard]] std::string last_error_text();

    /*
     * `code` tagged with the last OS error, for
     * `return std::unexpected(os::last_error(Error::Io));` at the point a
     * platform call fails. Subject to the same read-it-now caveat as
     * last_error_code().
     */
    [[nodiscard]] inline Error last_error(ErrorCode code) noexcept
    {
        return Error{code, last_error_code()};
    }
} // namespace vc::os

#endif /* __cplusplus */

#endif
