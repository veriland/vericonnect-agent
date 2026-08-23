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
     * The last OS error on this thread as "code: text" (errno on POSIX,
     * GetLastError on Windows). vc::Error says only that I/O failed; this says
     * which I/O failure, which is the difference between a diagnosable field
     * report and a guess.
     */
    [[nodiscard]] std::string last_error_text();
} // namespace vc::os

#endif /* __cplusplus */

#endif
