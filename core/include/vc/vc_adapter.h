/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_adapter.h - adapter ABI + host-side loader.
 *
 * ABI (C, UTF-8, cdecl) exported by every adapter shared library:
 *
 *   char*       RunAdapterCommand(const char* request_json);
 *   void        FreeAdapterString(char* p);
 *   const char* GetAdapterInfo(void);   // static string, not freed
 *
 * RunAdapterCommand receives the full command JSON and returns a malloc'd
 * JSON result ({"StatusCode":...,"StatusDescription":...}). The host frees it
 * with FreeAdapterString. All strings crossing the boundary are UTF-8.
 */
#ifndef VC_ADAPTER_H
#define VC_ADAPTER_H

#include "vc_common.h"

/* ------------------------------------------------------------------------
 * Adapter ABI (permanent C linkage; adapters resolve these by plain name).
 * ---------------------------------------------------------------------- */
#if defined(_WIN32)
#define VC_ADAPTER_EXPORT_LINKAGE __declspec(dllexport)
#else
#define VC_ADAPTER_EXPORT_LINKAGE __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
#define VC_ADAPTER_EXPORT extern "C" VC_ADAPTER_EXPORT_LINKAGE
#else
#define VC_ADAPTER_EXPORT VC_ADAPTER_EXPORT_LINKAGE
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    typedef char* (*vc_adapter_run_fn)(const char* request_json);
    typedef void (*vc_adapter_free_fn)(char* p);
    typedef const char* (*vc_adapter_info_fn)(void);

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------
 * Host-side loader (C++ API).
 * ---------------------------------------------------------------------- */
#ifdef __cplusplus

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vc
{
    /* RAII wrapper around a dynamically loaded shared library. */
    class DynLib
    {
    public:
        DynLib() noexcept = default;
        ~DynLib();
        DynLib(DynLib&& other) noexcept;
        DynLib& operator=(DynLib&& other) noexcept;
        DynLib(const DynLib&) = delete;
        DynLib& operator=(const DynLib&) = delete;

        [[nodiscard]] static std::optional<DynLib> open(const std::string& path);
        void* symbol(const char* name) const;
        explicit operator bool() const noexcept
        {
            return handle_ != nullptr;
        }

    private:
        explicit DynLib(void* handle) noexcept : handle_(handle) {}

        void* handle_ = nullptr;
    };

    /* A single loaded adapter shared library. */
    class Adapter
    {
    public:
        Adapter(Adapter&&) noexcept = default;
        Adapter& operator=(Adapter&&) noexcept = default;

        /* Load from a shared library; nullopt if it lacks the adapter exports. */
        [[nodiscard]] static std::optional<Adapter> load(const std::string& path);

        const std::string& id() const noexcept
        {
            return id_;
        }
        const std::string& path() const noexcept
        {
            return path_;
        }

        /* Invoke the adapter; returns its JSON result (empty if it returned null). */
        std::string run(const std::string& request_json) const;

    private:
        Adapter() = default;

        std::string id_;
        std::string path_;
        DynLib lib_;
        vc_adapter_run_fn run_ = nullptr;
        vc_adapter_free_fn free_ = nullptr;
        vc_adapter_info_fn info_ = nullptr;
    };

    class AdapterRegistry
    {
    public:
        /* Load every shared library exposing the adapter exports in dir. */
        [[nodiscard]] Status load(const std::string& dir);

        /* Find by adapter id (case-insensitive); nullptr if absent. */
        const Adapter* find(std::string_view id) const;

        /* Dispatch a command JSON (routes on "Adapter", else the first adapter).
         * Returns a JSON result; never empty. */
        std::string dispatch(const std::string& request_json);

        bool empty() const noexcept
        {
            return adapters_.empty();
        }

    private:
        std::vector<Adapter> adapters_;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
