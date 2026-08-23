/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_adapter.h"

#include <dlfcn.h>

namespace vc
{
    DynLib::~DynLib()
    {
        if (handle_) dlclose(handle_);
    }

    DynLib::DynLib(DynLib&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    DynLib& DynLib::operator=(DynLib&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_) dlclose(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    std::optional<DynLib> DynLib::open(const std::string& path)
    {
        void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) return std::nullopt;
        return DynLib(h);
    }

    void* DynLib::symbol(const char* name) const
    {
        return handle_ ? dlsym(handle_, name) : nullptr;
    }
} // namespace vc
