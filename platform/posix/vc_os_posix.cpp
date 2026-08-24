/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_os.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

namespace vc::os
{
    void sleep_ms(unsigned ms)
    {
        struct timespec ts = {ms / 1000, static_cast<long>(ms % 1000) * 1000000L};
        nanosleep(&ts, nullptr);
    }

    std::uint64_t monotonic_ms() noexcept
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000 +
               static_cast<std::uint64_t>(ts.tv_nsec) / 1000000;
    }

    Status random_bytes(std::span<std::uint8_t> buf)
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) return std::unexpected(last_error(Error::Fail));
        std::size_t off = 0;
        while (off < buf.size())
        {
            ssize_t n = read(fd, buf.data() + off, buf.size() - off);
            if (n <= 0)
            {
                /* Read before close(), which would overwrite errno. */
                const Error e = last_error(Error::Fail);
                close(fd);
                return std::unexpected(e);
            }
            off += static_cast<std::size_t>(n);
        }
        close(fd);
        return {};
    }

    LocalTime local_time() noexcept
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        std::time_t t = tv.tv_sec;
        std::tm tmv{};
        localtime_r(&t, &tmv);
        return LocalTime{tmv.tm_year + 1900,
                         tmv.tm_mon + 1,
                         tmv.tm_mday,
                         tmv.tm_hour,
                         tmv.tm_min,
                         tmv.tm_sec,
                         static_cast<int>(tv.tv_usec / 1000)};
    }

    std::uint32_t last_error_code() noexcept
    {
        return static_cast<std::uint32_t>(errno);
    }

    std::string error_text(std::uint32_t code)
    {
        if (code == 0) return {};
        const int e = static_cast<int>(code);
        char buf[256];
        /* strerror_r's two incompatible signatures: use the portable subset by
         * checking the return type at compile time. */
        const char* msg = nullptr;
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
        msg = strerror_r(e, buf, sizeof buf);
#else
        msg = (strerror_r(e, buf, sizeof buf) == 0) ? buf : "unknown";
#endif
        return std::to_string(e) + ": " + (msg ? msg : "unknown");
    }

    std::string last_error_text()
    {
        return error_text(last_error_code());
    }

    const char* shared_library_extension() noexcept
    {
#if defined(__APPLE__)
        return ".dylib";
#else
        return ".so";
#endif
    }
} // namespace vc::os
