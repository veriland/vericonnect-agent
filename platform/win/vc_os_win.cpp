/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_os.h"

#include <windows.h>
#include <bcrypt.h>

#include <string>
#include <string_view>

#pragma comment(lib, "bcrypt.lib")

namespace vc::os
{
    void sleep_ms(unsigned ms)
    {
        Sleep(ms);
    }

    std::uint64_t monotonic_ms() noexcept
    {
        return static_cast<std::uint64_t>(GetTickCount64());
    }

    Status random_bytes(std::span<std::uint8_t> buf)
    {
        NTSTATUS s = BCryptGenRandom(nullptr, buf.data(), static_cast<ULONG>(buf.size()),
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return s == 0 ? Status{} : std::unexpected(Error::Fail);
    }

    LocalTime local_time() noexcept
    {
        /* GetLocalTime carries milliseconds; no localtime_s / _ftime_s. */
        SYSTEMTIME st;
        GetLocalTime(&st);
        return LocalTime{static_cast<int>(st.wYear),        static_cast<int>(st.wMonth),
                         static_cast<int>(st.wDay),         static_cast<int>(st.wHour),
                         static_cast<int>(st.wMinute),      static_cast<int>(st.wSecond),
                         static_cast<int>(st.wMilliseconds)};
    }

    std::string last_error_text()
    {
        const DWORD e = GetLastError();
        if (e == 0) return {};
        char* msg = nullptr;
        const DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS,
                                       nullptr, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                       reinterpret_cast<LPSTR>(&msg), 0, nullptr);
        std::string text = std::to_string(e) + ": ";
        if (n && msg)
        {
            /* FormatMessage appends CRLF. */
            std::string_view sv(msg, n);
            while (!sv.empty() && (sv.back() == '\r' || sv.back() == '\n'))
                sv.remove_suffix(1);
            text.append(sv);
        }
        else
            text += "unknown";
        if (msg) LocalFree(msg);
        return text;
    }

    const char* shared_library_extension() noexcept
    {
        return ".dll";
    }
} // namespace vc::os
