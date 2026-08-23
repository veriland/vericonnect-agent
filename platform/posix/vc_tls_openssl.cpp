/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/* TLS client stream over OpenSSL (Linux/macOS). */
#include "vc/vc_tls.h"
#include "vc/vc_os.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace vc
{
    struct Tls::Impl
    {
        Socket sock;
        SSL_CTX* ctx = nullptr;
        SSL* ssl = nullptr;
        BIO* rbio = nullptr; /* network -> SSL */
        BIO* wbio = nullptr; /* SSL -> network */
        bool closed = false;

        explicit Impl(Socket s) noexcept : sock(std::move(s)) {}

        /* Owns raw OpenSSL handles freed in the destructor, so copying
         * or moving it would double-free. It only ever lives in a
         * unique_ptr (DESIGN.md §2). */
        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        ~Impl()
        {
            if (ssl)
            {
                SSL_shutdown(ssl);
                SSL_free(ssl);
            } /* frees the BIOs */
            if (ctx) SSL_CTX_free(ctx);
        }

        /* Drain queued outbound bytes to the socket. */
        Status flush_wbio()
        {
            char tmp[8192];
            int pending;
            while ((pending = BIO_read(wbio, tmp, sizeof tmp)) > 0)
            {
                auto r = sock.send(std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(tmp), static_cast<std::size_t>(pending)));
                if (!r) return std::unexpected(Error::Io);
            }
            return {};
        }

        /* Pull inbound bytes from the socket into the read BIO. Returns bytes
         * read (>0), 0 on close, or an error. */
        Result<std::size_t> pump_in(int timeout_ms)
        {
            char tmp[8192];
            auto r =
                sock.recv(std::span<std::uint8_t>(reinterpret_cast<std::uint8_t*>(tmp), sizeof tmp),
                          timeout_ms);
            if (r && *r > 0)
            {
                BIO_write(rbio, tmp, static_cast<int>(*r));
            }
            return r;
        }
    };

    Tls::Tls() noexcept = default;

    Tls::Tls(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    Tls::~Tls() = default;
    Tls::Tls(Tls&&) noexcept = default;
    Tls& Tls::operator=(Tls&&) noexcept = default;

    Result<Tls> Tls::connect(Socket sock, const std::string& hostname, int timeout_ms)
    {
        auto impl = std::make_unique<Impl>(std::move(sock));

        impl->ctx = SSL_CTX_new(TLS_client_method());
        if (!impl->ctx) return std::unexpected(Error::Tls);
        SSL_CTX_set_verify(impl->ctx, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_default_verify_paths(impl->ctx);
        SSL_CTX_set_min_proto_version(impl->ctx, TLS1_2_VERSION);

        impl->ssl = SSL_new(impl->ctx);
        impl->rbio = BIO_new(BIO_s_mem());
        impl->wbio = BIO_new(BIO_s_mem());
        SSL_set_bio(impl->ssl, impl->rbio, impl->wbio);
        SSL_set_connect_state(impl->ssl);
        SSL_set_tlsext_host_name(impl->ssl, hostname.c_str());
        X509_VERIFY_PARAM_set1_host(SSL_get0_param(impl->ssl), hostname.c_str(), 0);

        std::uint64_t deadline = os::monotonic_ms() + static_cast<std::uint64_t>(timeout_ms);
        for (;;)
        {
            int r = SSL_do_handshake(impl->ssl);
            if (r == 1) break;
            int err = SSL_get_error(impl->ssl, r);
            if (!impl->flush_wbio()) return std::unexpected(Error::Io);
            if (err == SSL_ERROR_WANT_READ)
            {
                std::uint64_t now = os::monotonic_ms();
                if (now >= deadline) return std::unexpected(Error::Timeout);
                auto pr = impl->pump_in(static_cast<int>(deadline - now));
                if (!pr)
                {
                    if (pr.error() == Error::Timeout) return std::unexpected(Error::Timeout);
                    return std::unexpected(Error::Tls);
                }
                if (*pr == 0) return std::unexpected(Error::Tls); /* closed mid-handshake */
            }
            else if (err != SSL_ERROR_WANT_WRITE)
            {
                return std::unexpected(Error::Tls);
            }
        }
        if (!impl->flush_wbio()) return std::unexpected(Error::Io);
        return Tls(std::move(impl));
    }

    Status Tls::send(std::span<const std::uint8_t> data)
    {
        if (!impl_ || impl_->closed) return std::unexpected(Error::Closed);
        std::size_t off = 0;
        const char* p = reinterpret_cast<const char*>(data.data());
        while (off < data.size())
        {
            int n = SSL_write(impl_->ssl, p + off, static_cast<int>(data.size() - off));
            if (n <= 0)
            {
                int err = SSL_get_error(impl_->ssl, n);
                if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
                {
                    if (!impl_->flush_wbio()) return std::unexpected(Error::Io);
                    continue;
                }
                return std::unexpected(Error::Tls);
            }
            off += static_cast<std::size_t>(n);
            if (!impl_->flush_wbio()) return std::unexpected(Error::Io);
        }
        return {};
    }

    Result<std::size_t> Tls::recv(std::span<std::uint8_t> buf, int timeout_ms)
    {
        if (!impl_) return std::unexpected(Error::InvalidArg);
        if (impl_->closed) return std::size_t{0};
        std::uint64_t deadline =
            os::monotonic_ms() + static_cast<std::uint64_t>(timeout_ms < 0 ? 0 : timeout_ms);
        for (;;)
        {
            int n = SSL_read(impl_->ssl, buf.data(), static_cast<int>(buf.size()));
            if (n > 0) return static_cast<std::size_t>(n);
            int err = SSL_get_error(impl_->ssl, n);
            if (err == SSL_ERROR_ZERO_RETURN)
            {
                impl_->closed = true;
                return std::size_t{0};
            }
            if (err == SSL_ERROR_WANT_READ)
            {
                std::uint64_t now = os::monotonic_ms();
                int wait = timeout_ms < 0
                               ? 60000
                               : (now >= deadline ? 0 : static_cast<int>(deadline - now));
                if (timeout_ms >= 0 && wait == 0) return std::unexpected(Error::Timeout);
                auto pr = impl_->pump_in(wait);
                if (!pr)
                {
                    if (pr.error() == Error::Timeout)
                    {
                        if (timeout_ms < 0) continue;
                        return std::unexpected(Error::Timeout);
                    }
                    return std::unexpected(pr.error());
                }
                if (*pr == 0)
                {
                    impl_->closed = true;
                    return std::size_t{0};
                }
            }
            else
            {
                return std::unexpected(Error::Tls);
            }
        }
    }

    void Tls::close()
    {
        impl_.reset();
    }
} // namespace vc
