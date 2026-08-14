/*
 * vc_impersonate.h - run a single adapter command under a caller-supplied
 * user identity ("UserCredentials" in the command JSON).
 *
 * The agent normally executes commands as its own service account. When a
 * command carries UserCredentials, the host impersonates that user for the
 * duration of the adapter call and reverts immediately afterwards.
 *
 * Impersonation is applied to the CURRENT THREAD only, so it is safe in the
 * agent's single-threaded, synchronous dispatch path.
 *
 * Platform support:
 *   Windows  - LogonUser + ImpersonateLoggedOnUser (real).
 *   POSIX    - unsupported (Error::Unsupported, no silent fallback).
 */
#ifndef VC_IMPERSONATE_H
#define VC_IMPERSONATE_H

#include "vc_common.h"

#ifdef __cplusplus

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace vc
{
    struct ImpersonationError
    {
        Error code;
        std::string message; /* log-safe: never contains the password */
    };

    /* RAII thread impersonation: constructed via begin(), reverts on destruction. */
    class Impersonation
    {
    public:
        Impersonation() noexcept;
        ~Impersonation();
        Impersonation(Impersonation&& other) noexcept;
        Impersonation& operator=(Impersonation&& other) noexcept;
        Impersonation(const Impersonation&) = delete;
        Impersonation& operator=(const Impersonation&) = delete;

        /*
         * Begin impersonating `user` on the calling thread.
         *   user     : account name (UTF-8), required.
         *   domain   : domain or machine name; empty or "." = local.
         *   password : plaintext password (UTF-8); may be empty.
         */
        static std::expected<Impersonation, ImpersonationError> begin(std::string_view user,
                                                                      std::string_view domain,
                                                                      std::string_view password);

    private:
        struct Impl;
        explicit Impersonation(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
