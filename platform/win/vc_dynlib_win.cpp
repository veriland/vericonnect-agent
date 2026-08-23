/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_adapter.h"
#include "vc/vc_log.h"
#include "vc/vc_os.h"

#include <string>
#include <windows.h>

namespace vc
{
    namespace
    {
        std::wstring utf8_to_wide(const std::string& s)
        {
            if (s.empty()) return {};
            int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            if (wlen <= 0) return {};
            std::wstring w(static_cast<std::size_t>(wlen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), wlen);
            return w;
        }
    } // namespace

    DynLib::~DynLib()
    {
        if (handle_) FreeLibrary(static_cast<HMODULE>(handle_));
    }

    DynLib::DynLib(DynLib&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    DynLib& DynLib::operator=(DynLib&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_) FreeLibrary(static_cast<HMODULE>(handle_));
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    std::optional<DynLib> DynLib::open(const std::string& path)
    {
        HMODULE h = LoadLibraryW(utf8_to_wide(path).c_str());
        if (!h)
        {
            log::message(log::Level::Warn, "LoadLibrary({}) failed: {}", path,
                         os::last_error_text());
            return std::nullopt;
        }
        return DynLib(h);
    }

    void* DynLib::symbol(const char* name) const
    {
        return handle_
                   ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name))
                   : nullptr;
    }
} // namespace vc
