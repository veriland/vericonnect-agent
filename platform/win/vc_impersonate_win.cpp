/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * Windows impersonation backend.
 *
 * Logs the caller-supplied user on (LogonUser) and impersonates them on the
 * current thread (ImpersonateLoggedOnUser) for the duration of one adapter
 * command. Reverted (RevertToSelf) when the Impersonation is destroyed.
 *
 * The host process must hold SE_IMPERSONATE_NAME - LocalSystem,
 * NetworkService and most service accounts do by default.
 */
#include "vc/vc_impersonate.h"

#include <format>
#include <string>

#define SECURITY_WIN32
#include <windows.h>

namespace vc
{
    struct Impersonation::Impl
    {
        bool active = false;
        ~Impl()
        {
            if (active) RevertToSelf();
        }

        Impl() = default;

        /* Reverts the thread token in its destructor; a copy would revert an
         * identity it never established. Only ever held by unique_ptr. */
        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;
    };

    Impersonation::Impersonation() noexcept = default;

    Impersonation::Impersonation(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    Impersonation::~Impersonation() = default;
    Impersonation::Impersonation(Impersonation&&) noexcept = default;
    Impersonation& Impersonation::operator=(Impersonation&&) noexcept = default;

    namespace
    {
        std::wstring utf8_to_wide(std::string_view s)
        {
            if (s.empty()) return {};
            int wlen =
                MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (wlen <= 0) return {};
            std::wstring w(static_cast<std::size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), wlen);
            return w;
        }
    } // namespace

    std::expected<Impersonation, ImpersonationError> Impersonation::begin(std::string_view user,
                                                                          std::string_view domain,
                                                                          std::string_view password)
    {
        if (user.empty())
            return std::unexpected(ImpersonationError{Error::InvalidArg, "Missing username"});

        /* username, domain and password go to LogonUser as-is (domain may be
         * empty), with LOGON32_LOGON_INTERACTIVE / LOGON32_PROVIDER_DEFAULT. */
        std::wstring wuser = utf8_to_wide(user);
        std::wstring wdom = utf8_to_wide(domain);
        std::wstring wpass = utf8_to_wide(password);

        HANDLE token = nullptr;
        BOOL ok = LogonUserW(wuser.c_str(), wdom.c_str(), wpass.c_str(), LOGON32_LOGON_INTERACTIVE,
                             LOGON32_PROVIDER_DEFAULT, &token);
        DWORD last = ok ? 0 : GetLastError();

        /* Scrub the plaintext password copy. */
        if (!wpass.empty()) SecureZeroMemory(wpass.data(), wpass.size() * sizeof(wchar_t));

        if (!ok)
            return std::unexpected(ImpersonationError{
                Error{Error::Fail, last}, std::format("Logon failed (Win32 error {})", last)});

        BOOL imp_ok = ImpersonateLoggedOnUser(token);
        DWORD imp_err = imp_ok ? 0 : GetLastError();
        CloseHandle(token); /* once impersonating, the token is no longer needed */

        if (!imp_ok)
            return std::unexpected(
                ImpersonationError{Error{Error::Fail, imp_err},
                                   std::format("Impersonation failed (Win32 error {})", imp_err)});

        auto impl = std::make_unique<Impl>();
        impl->active = true;
        return Impersonation(std::move(impl));
    }
} // namespace vc
