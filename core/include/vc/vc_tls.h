/*
 * vc_tls.h - TLS client stream abstraction.
 *
 * Windows: SChannel (platform/win/vc_tls_schannel.cpp) - no extra deps.
 * POSIX:   OpenSSL   (platform/posix/vc_tls_openssl.cpp).
 *
 * The platform-specific state is hidden behind a PIMPL so this header stays
 * free of OpenSSL / SChannel types.
 */
#ifndef VC_TLS_H
#define VC_TLS_H

#include "vc_common.h"
#include "vc_sock.h"

#ifdef __cplusplus

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace vc
{
    class Tls
    {
    public:
        Tls() noexcept;
        ~Tls();
        Tls(Tls&& other) noexcept;
        Tls& operator=(Tls&& other) noexcept;
        Tls(const Tls&) = delete;
        Tls& operator=(const Tls&) = delete;

        /* Perform the TLS handshake over an already-connected socket; hostname is
     * used for SNI and certificate validation. The Tls takes ownership of the
     * socket (it is closed when the Tls is destroyed, or by connect() on
     * failure). */
        static Result<Tls> connect(Socket sock, const std::string& hostname, int timeout_ms);

        Status send(std::span<const std::uint8_t> data);
        /* Returns decrypted bytes read (>0), 0 on orderly close, or an error. */
        Result<std::size_t> recv(std::span<std::uint8_t> buf, int timeout_ms);

        void close();
        bool valid() const noexcept { return impl_ != nullptr; }

    private:
        struct Impl;
        explicit Tls(std::unique_ptr<Impl> impl) noexcept;
        std::unique_ptr<Impl> impl_;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
