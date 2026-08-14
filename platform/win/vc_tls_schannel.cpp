/*
 * TLS client stream using Windows SChannel (SSPI).
 * No external dependencies; validates the server certificate against the
 * Windows trust store using the SNI hostname.
 */
#include "vc/vc_tls.h"
#include "vc/vc_os.h"

#include <algorithm>
#include <cstring>
#include <vector>

#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>

#pragma comment(lib, "secur32.lib")

namespace vc
{
    namespace
    {
        void consume(Bytes& b, std::size_t n)
        {
            b.erase(b.begin(), b.begin() + std::min(n, b.size()));
        }
    } // namespace

    struct Tls::Impl
    {
        Socket sock;
        CredHandle cred{};
        CtxtHandle ctx{};
        bool have_cred = false;
        bool have_ctx = false;
        SecPkgContext_StreamSizes sizes{};
        Bytes plain; /* decrypted plaintext not yet consumed by caller */
        Bytes enc;   /* raw ciphertext received but not yet decrypted  */
        bool closed = false;

        explicit Impl(Socket s) noexcept : sock(std::move(s)) {}

        ~Impl()
        {
            if (have_ctx) DeleteSecurityContext(&ctx);
            if (have_cred) FreeCredentialsHandle(&cred);
        }

        /* Read some ciphertext into enc. Returns bytes read (>0), 0 on close, or
         * an error. */
        Result<std::size_t> recv_some(int timeout_ms)
        {
            std::uint8_t tmp[8192];
            auto n = sock.recv(std::span<std::uint8_t>(tmp, sizeof tmp), timeout_ms);
            if (n && *n > 0) enc.insert(enc.end(), tmp, tmp + *n);
            return n;
        }

        Status handshake(const std::string& hostname, int timeout_ms);
    };

    Status Tls::Impl::handshake(const std::string& hostname, int timeout_ms)
    {
        DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                      ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                      ISC_REQ_USE_SUPPLIED_CREDS;
        DWORD out_flags = 0;
        bool first = true;

        std::string target(hostname); /* mutable SNI target for InitializeSecurityContextA */
        std::uint64_t deadline = os::monotonic_ms() + static_cast<std::uint64_t>(timeout_ms);

        for (;;)
        {
            if (!first)
            {
                while (enc.empty())
                {
                    std::uint64_t now = os::monotonic_ms();
                    if (now >= deadline) return std::unexpected(Error::Timeout);
                    auto r = recv_some(static_cast<int>(deadline - now));
                    if (!r)
                    {
                        if (r.error() == Error::Timeout) continue;
                        return std::unexpected(Error::Tls);
                    }
                    if (*r == 0) return std::unexpected(Error::Tls);
                }
            }

            SecBuffer in_buf[2];
            in_buf[0].BufferType = SECBUFFER_TOKEN;
            in_buf[0].pvBuffer = enc.data();
            in_buf[0].cbBuffer = static_cast<unsigned long>(enc.size());
            in_buf[1].BufferType = SECBUFFER_EMPTY;
            in_buf[1].pvBuffer = nullptr;
            in_buf[1].cbBuffer = 0;
            SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in_buf};

            SecBuffer out_buf[1];
            out_buf[0].BufferType = SECBUFFER_TOKEN;
            out_buf[0].pvBuffer = nullptr;
            out_buf[0].cbBuffer = 0;
            SecBufferDesc out_desc{SECBUFFER_VERSION, 1, out_buf};

            SECURITY_STATUS ss = InitializeSecurityContextA(
                &cred, first ? nullptr : &ctx, first ? target.data() : nullptr, flags, 0, 0,
                first ? nullptr : &in_desc, 0, first ? &ctx : nullptr, &out_desc, &out_flags,
                nullptr);
            first = false;
            if (!have_ctx && (ss == SEC_E_OK || ss == SEC_I_CONTINUE_NEEDED ||
                              ss == SEC_E_INCOMPLETE_MESSAGE || FAILED(ss)))
                have_ctx = true;

            if (out_buf[0].cbBuffer > 0 && out_buf[0].pvBuffer)
            {
                auto sr = sock.send(std::span<const std::uint8_t>(
                    static_cast<const std::uint8_t*>(out_buf[0].pvBuffer), out_buf[0].cbBuffer));
                FreeContextBuffer(out_buf[0].pvBuffer);
                if (!sr) return std::unexpected(Error::Io);
            }

            if (ss == SEC_E_INCOMPLETE_MESSAGE)
            {
                std::uint64_t now = os::monotonic_ms();
                if (now >= deadline) return std::unexpected(Error::Timeout);
                auto r = recv_some(static_cast<int>(deadline - now));
                if (!r && r.error() != Error::Timeout) return std::unexpected(Error::Tls);
                continue;
            }

            if (in_buf[1].BufferType == SECBUFFER_EXTRA)
                consume(enc, enc.size() - in_buf[1].cbBuffer);
            else
                enc.clear();

            if (ss == SEC_I_CONTINUE_NEEDED) continue;
            if (ss == SEC_E_OK)
            {
                QueryContextAttributes(&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
                return {};
            }
            return std::unexpected(Error::Tls);
        }
    }

    Tls::Tls() noexcept = default;

    Tls::Tls(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    Tls::~Tls() = default;
    Tls::Tls(Tls&&) noexcept = default;
    Tls& Tls::operator=(Tls&&) noexcept = default;

    Result<Tls> Tls::connect(Socket sock, const std::string& hostname, int timeout_ms)
    {
        auto impl = std::make_unique<Impl>(std::move(sock));

        SCHANNEL_CRED sc{};
        sc.dwVersion = SCHANNEL_CRED_VERSION;
        sc.dwFlags =
            SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;

        SECURITY_STATUS ss = AcquireCredentialsHandleA(nullptr, const_cast<LPSTR>(UNISP_NAME_A),
                                                       SECPKG_CRED_OUTBOUND, nullptr, &sc, nullptr,
                                                       nullptr, &impl->cred, nullptr);
        if (ss != SEC_E_OK) return std::unexpected(Error::Tls);
        impl->have_cred = true;

        if (!impl->handshake(hostname, timeout_ms)) return std::unexpected(Error::Tls);
        return Tls(std::move(impl));
    }

    Status Tls::send(std::span<const std::uint8_t> data)
    {
        if (!impl_ || impl_->closed) return std::unexpected(Error::Closed);

        std::size_t hdr = impl_->sizes.cbHeader;
        std::size_t trl = impl_->sizes.cbTrailer;
        std::size_t maxmsg = impl_->sizes.cbMaximumMessage;

        std::vector<std::uint8_t> buf(hdr + maxmsg + trl);
        const std::uint8_t* p = data.data();
        std::size_t off = 0;
        while (off < data.size())
        {
            std::size_t chunk = std::min(data.size() - off, maxmsg);
            std::memcpy(buf.data() + hdr, p + off, chunk);

            SecBuffer bufs[4];
            bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
            bufs[0].pvBuffer = buf.data();
            bufs[0].cbBuffer = static_cast<unsigned long>(hdr);
            bufs[1].BufferType = SECBUFFER_DATA;
            bufs[1].pvBuffer = buf.data() + hdr;
            bufs[1].cbBuffer = static_cast<unsigned long>(chunk);
            bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
            bufs[2].pvBuffer = buf.data() + hdr + chunk;
            bufs[2].cbBuffer = static_cast<unsigned long>(trl);
            bufs[3].BufferType = SECBUFFER_EMPTY;
            bufs[3].pvBuffer = nullptr;
            bufs[3].cbBuffer = 0;
            SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};

            if (EncryptMessage(&impl_->ctx, 0, &desc, 0) != SEC_E_OK)
                return std::unexpected(Error::Tls);

            std::size_t total = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
            if (!impl_->sock.send(std::span<const std::uint8_t>(buf.data(), total)))
                return std::unexpected(Error::Io);
            off += chunk;
        }
        return {};
    }

    Result<std::size_t> Tls::recv(std::span<std::uint8_t> buf, int timeout_ms)
    {
        if (!impl_) return std::unexpected(Error::InvalidArg);

        /* serve buffered plaintext first */
        if (!impl_->plain.empty())
        {
            std::size_t n = std::min(impl_->plain.size(), buf.size());
            std::memcpy(buf.data(), impl_->plain.data(), n);
            consume(impl_->plain, n);
            return n;
        }
        if (impl_->closed) return std::size_t{0};

        std::uint64_t deadline =
            os::monotonic_ms() + static_cast<std::uint64_t>(timeout_ms < 0 ? 0 : timeout_ms);

        for (;;)
        {
            if (!impl_->enc.empty())
            {
                SecBuffer bufs[4];
                bufs[0].BufferType = SECBUFFER_DATA;
                bufs[0].pvBuffer = impl_->enc.data();
                bufs[0].cbBuffer = static_cast<unsigned long>(impl_->enc.size());
                bufs[1].BufferType = SECBUFFER_EMPTY;
                bufs[2].BufferType = SECBUFFER_EMPTY;
                bufs[3].BufferType = SECBUFFER_EMPTY;
                SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};

                SECURITY_STATUS ss = DecryptMessage(&impl_->ctx, &desc, 0, nullptr);
                if (ss == SEC_E_OK)
                {
                    SecBuffer *data_buf = nullptr, *extra_buf = nullptr;
                    for (int i = 0; i < 4; i++)
                    {
                        if (bufs[i].BufferType == SECBUFFER_DATA && !data_buf)
                            data_buf = &bufs[i];
                        else if (bufs[i].BufferType == SECBUFFER_EXTRA && !extra_buf)
                            extra_buf = &bufs[i];
                    }
                    if (data_buf && data_buf->cbBuffer > 0)
                    {
                        auto* d = static_cast<const std::uint8_t*>(data_buf->pvBuffer);
                        impl_->plain.insert(impl_->plain.end(), d, d + data_buf->cbBuffer);
                    }
                    if (extra_buf && extra_buf->cbBuffer > 0)
                        consume(impl_->enc, impl_->enc.size() - extra_buf->cbBuffer);
                    else
                        impl_->enc.clear();

                    if (!impl_->plain.empty())
                    {
                        std::size_t n = std::min(impl_->plain.size(), buf.size());
                        std::memcpy(buf.data(), impl_->plain.data(), n);
                        consume(impl_->plain, n);
                        return n;
                    }
                    continue;
                }
                else if (ss == SEC_I_CONTEXT_EXPIRED)
                {
                    impl_->closed = true;
                    return std::size_t{0};
                }
                else if (ss != SEC_E_INCOMPLETE_MESSAGE)
                {
                    return std::unexpected(Error::Tls);
                }
                /* incomplete: fall through and read more */
            }

            std::uint64_t now = os::monotonic_ms();
            int wait =
                timeout_ms < 0 ? -1 : (now >= deadline ? 0 : static_cast<int>(deadline - now));
            if (timeout_ms >= 0 && wait == 0 && impl_->enc.empty())
                return std::unexpected(Error::Timeout);
            auto r = impl_->recv_some(wait < 0 ? 60000 : wait);
            if (!r)
            {
                if (r.error() == Error::Timeout)
                {
                    if (timeout_ms < 0) continue;
                    return std::unexpected(Error::Timeout);
                }
                return std::unexpected(r.error());
            }
            if (*r == 0)
            {
                impl_->closed = true;
                return std::size_t{0};
            }
        }
    }

    void Tls::close()
    {
        impl_.reset();
    }
} // namespace vc
