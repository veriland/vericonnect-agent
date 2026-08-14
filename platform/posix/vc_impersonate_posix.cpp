/*
 * POSIX impersonation backend - intentionally unsupported.
 *
 * Password-based per-command impersonation has no safe, portable POSIX
 * equivalent: there is no LogonUser, credential verification needs PAM, and
 * setuid/setgid are process-wide and require root. Rather than silently run
 * the command as the service account (a security surprise), a request
 * carrying UserCredentials is rejected with a clear error.
 *
 * See vc_impersonate_win.cpp for the Windows implementation.
 */
#include "vc/vc_impersonate.h"

#include <cstring>

namespace vc
{
    struct Impersonation::Impl
    {
    };

    Impersonation::Impersonation() noexcept = default;

    Impersonation::Impersonation(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
    {
    }

    Impersonation::~Impersonation() = default;
    Impersonation::Impersonation(Impersonation&&) noexcept = default;
    Impersonation& Impersonation::operator=(Impersonation&&) noexcept = default;

    std::expected<Impersonation, ImpersonationError>
    Impersonation::begin(std::string_view, std::string_view, std::string_view)
    {
        return std::unexpected(ImpersonationError{
            Error::Unsupported,
            "Impersonation via UserCredentials is not supported on this platform"
        });
    }
} // namespace vc

