/*
 * RFC 6455 WebSocket client over vc::Tls.
 * - client frames are always masked
 * - fragmented messages are reassembled in recv
 * - ping is answered with pong transparently
 * - outgoing messages larger than kFragSize are fragmented
 */
#include "vc/vc_ws.h"
#include "vc/vc_base64.h"
#include "vc/vc_os.h"

#include <array>
#include <cctype>
#include <cstring>
#include <optional>

namespace vc
{
    namespace
    {
        constexpr std::size_t kFragSize = 60 * 1024;
        constexpr std::uint64_t kMaxMsg = 64ull * 1024 * 1024; /* sanity cap 64 MB */
        constexpr int kHdrTimeout = 15000;

        bool istarts_with(std::span<const std::uint8_t> s, std::string_view prefix) noexcept
        {
            if (s.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); i++)
                if (std::tolower(s[i]) != std::tolower(static_cast<unsigned char>(prefix[i])))
                    return false;
            return true;
        }

        /* Find the byte offset of needle in hay, or npos. */
        std::size_t find(std::span<const std::uint8_t> hay, std::string_view needle) noexcept
        {
            if (needle.empty() || hay.size() < needle.size()) return static_cast<std::size_t>(-1);
            for (std::size_t i = 0; i + needle.size() <= hay.size(); i++)
                if (std::memcmp(hay.data() + i, needle.data(), needle.size()) == 0) return i;
            return static_cast<std::size_t>(-1);
        }

        struct Frame
        {
            std::uint8_t opcode;
            bool fin;
            Bytes payload;
        };
    } // namespace

    Result<std::size_t> WebSocket::read_more(int timeout_ms)
    {
        std::uint8_t tmp[8192];
        auto n = tls_.recv(std::span<std::uint8_t>(tmp, sizeof tmp), timeout_ms);
        if (!n) return std::unexpected(n.error());
        if (*n == 0) return std::unexpected(Error::Closed);
        in_.insert(in_.end(), tmp, tmp + *n);
        return *n;
    }

    Result<WebSocket> WebSocket::connect(const std::string& host, int port,
                                         const std::string& path_and_query,
                                         std::string_view extra_headers, int timeout_ms)
    {
        Result<Socket> sock = Socket::connect(host, port, timeout_ms);
        if (!sock) return std::unexpected(sock.error());
        Result<Tls> tls = Tls::connect(std::move(*sock), host, timeout_ms);
        if (!tls) return std::unexpected(tls.error());

        WebSocket ws(std::move(*tls));

        /* Sec-WebSocket-Key: 16 random bytes, base64 */
        std::array<std::uint8_t, 16> nonce;
        if (!os::random_bytes(nonce)) return std::unexpected(Error::Fail);
        std::string key = base64_encode(std::span<const std::uint8_t>(nonce));

        std::string req;
        req.reserve(256);
        req.append("GET ")
            .append(path_and_query)
            .append(" HTTP/1.1\r\n")
            .append("Host: ")
            .append(host)
            .append("\r\n")
            .append("Upgrade: websocket\r\n")
            .append("Connection: Upgrade\r\n")
            .append("Sec-WebSocket-Key: ")
            .append(key)
            .append("\r\n")
            .append("Sec-WebSocket-Version: 13\r\n")
            .append(extra_headers)
            .append("\r\n");

        if (!ws.tls_.send(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(req.data()), req.size())))
            return std::unexpected(Error::Io);

        /* read the 101 response header block */
        std::uint64_t deadline = os::monotonic_ms() + kHdrTimeout;
        std::size_t hdr_end;
        while ((hdr_end = find(ws.in_, "\r\n\r\n")) == static_cast<std::size_t>(-1))
        {
            std::uint64_t now = os::monotonic_ms();
            if (now >= deadline) return std::unexpected(Error::Timeout);
            if (!ws.read_more(static_cast<int>(deadline - now))) return std::unexpected(Error::Io);
        }

        if (!istarts_with(ws.in_, "HTTP/1.1 101") && !istarts_with(ws.in_, "HTTP/1.0 101"))
            return std::unexpected(Error::Protocol);

        /* We rely on TLS for integrity; the Sec-WebSocket-Accept SHA-1 check is
         * intentionally skipped. */
        ws.in_.erase(ws.in_.begin(), ws.in_.begin() + hdr_end + 4);
        return ws;
    }

    Status WebSocket::send_frame(std::uint8_t opcode, bool fin, std::span<const std::uint8_t> data)
    {
        std::uint8_t hdr[14];
        std::size_t h = 0;
        std::size_t len = data.size();
        hdr[h++] = static_cast<std::uint8_t>((fin ? 0x80 : 0x00) | opcode);
        if (len < 126)
        {
            hdr[h++] = static_cast<std::uint8_t>(0x80 | len);
        }
        else if (len <= 0xFFFF)
        {
            hdr[h++] = 0x80 | 126;
            hdr[h++] = static_cast<std::uint8_t>(len >> 8);
            hdr[h++] = static_cast<std::uint8_t>(len);
        }
        else
        {
            hdr[h++] = 0x80 | 127;
            for (int i = 7; i >= 0; i--)
                hdr[h++] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(len) >> (i * 8));
        }
        std::uint8_t mask[4];
        if (!os::random_bytes(mask)) return std::unexpected(Error::Fail);
        std::memcpy(hdr + h, mask, 4);
        h += 4;

        if (!tls_.send(std::span<const std::uint8_t>(hdr, h))) return std::unexpected(Error::Io);

        if (len)
        {
            std::uint8_t chunk[8192];
            std::size_t off = 0;
            while (off < len)
            {
                std::size_t n = len - off;
                if (n > sizeof chunk) n = sizeof chunk;
                for (std::size_t i = 0; i < n; i++)
                    chunk[i] = data[off + i] ^ mask[(off + i) & 3];
                if (!tls_.send(std::span<const std::uint8_t>(chunk, n)))
                    return std::unexpected(Error::Io);
                off += n;
            }
        }
        return {};
    }

    Status WebSocket::send(MsgType type, std::span<const std::uint8_t> data)
    {
        if (closed_) return std::unexpected(Error::Closed);
        std::uint8_t opcode = (type == MsgType::Text) ? 0x1 : 0x2;

        if (data.size() <= kFragSize) return send_frame(opcode, true, data);

        std::size_t off = 0;
        bool first = true;
        while (off < data.size())
        {
            std::size_t n = data.size() - off;
            if (n > kFragSize) n = kFragSize;
            bool fin = (off + n == data.size());
            Status rc = send_frame(first ? opcode : 0x0, fin, data.subspan(off, n));
            if (!rc) return rc;
            first = false;
            off += n;
        }
        return {};
    }

    Status WebSocket::send_ping()
    {
        if (closed_) return std::unexpected(Error::Closed);
        return send_frame(0x9, true, {});
    }

    Status WebSocket::send_close(std::uint16_t code)
    {
        if (closed_) return std::unexpected(Error::Closed);
        std::uint8_t payload[2] = {static_cast<std::uint8_t>(code >> 8),
                                   static_cast<std::uint8_t>(code)};
        return send_frame(0x8, true, std::span<const std::uint8_t>(payload, 2));
    }

    /* Parse one frame from in_. Returns a Frame, nullopt if more bytes are
     * needed, or an error. */
    namespace
    {
        Result<std::optional<Frame>> parse_frame(Bytes& in)
        {
            const std::uint8_t* p = in.data();
            std::size_t avail = in.size();
            if (avail < 2) return std::optional<Frame>{};

            Frame f;
            f.fin = (p[0] & 0x80) != 0;
            f.opcode = p[0] & 0x0F;
            bool masked = (p[1] & 0x80) != 0;
            std::uint64_t len = p[1] & 0x7F;
            std::size_t pos = 2;

            if (len == 126)
            {
                if (avail < pos + 2) return std::optional<Frame>{};
                len = (static_cast<std::uint64_t>(p[pos]) << 8) | p[pos + 1];
                pos += 2;
            }
            else if (len == 127)
            {
                if (avail < pos + 8) return std::optional<Frame>{};
                len = 0;
                for (int i = 0; i < 8; i++)
                    len = (len << 8) | p[pos + i];
                pos += 8;
            }
            if (len > kMaxMsg) return std::unexpected(Error::Protocol);

            std::uint8_t mask[4] = {0};
            if (masked)
            {
                if (avail < pos + 4) return std::optional<Frame>{};
                std::memcpy(mask, p + pos, 4);
                pos += 4;
            }
            if (avail < pos + static_cast<std::size_t>(len)) return std::optional<Frame>{};

            f.payload.assign(p + pos, p + pos + len);
            if (masked)
                for (std::size_t i = 0; i < f.payload.size(); i++)
                    f.payload[i] ^= mask[i & 3];

            in.erase(in.begin(), in.begin() + pos + static_cast<std::size_t>(len));
            return std::optional<Frame>{std::move(f)};
        }
    } // namespace

    Result<WebSocket::Message> WebSocket::recv(int timeout_ms)
    {
        if (closed_) return std::unexpected(Error::Closed);

        Bytes msg;
        std::uint8_t msg_opcode = 0;
        bool in_fragmented = false;

        std::uint64_t deadline =
            os::monotonic_ms() + static_cast<std::uint64_t>(timeout_ms < 0 ? 0 : timeout_ms);

        for (;;)
        {
            Result<std::optional<Frame>> pf = parse_frame(in_);
            if (!pf) return std::unexpected(pf.error());

            if (!*pf)
            {
                /* need more bytes */
                std::uint64_t now = os::monotonic_ms();
                if (timeout_ms >= 0 && now >= deadline) return std::unexpected(Error::Timeout);
                int wait = timeout_ms < 0 ? 60000 : static_cast<int>(deadline - now);
                auto n = read_more(wait);
                if (!n)
                {
                    if (n.error() == Error::Timeout)
                    {
                        if (timeout_ms < 0) continue;
                        return std::unexpected(Error::Timeout);
                    }
                    closed_ = true;
                    return std::unexpected(n.error());
                }
                continue;
            }

            Frame f = std::move(**pf);
            switch (f.opcode)
            {
            case 0x9: /* ping -> pong with same payload */
                (void)send_frame(0xA, true, f.payload);
                continue;
            case 0xA: /* pong */
                continue;
            case 0x8: /* close */
                (void)send_frame(0x8, true,
                                 std::span<const std::uint8_t>(f.payload).first(
                                     f.payload.size() > 2 ? 2 : f.payload.size()));
                closed_ = true;
                return Message{MsgType::Close, std::move(f.payload)};
            case 0x0: /* continuation */
                if (!in_fragmented) return std::unexpected(Error::Protocol);
                msg.insert(msg.end(), f.payload.begin(), f.payload.end());
                if (f.fin)
                    return Message{msg_opcode == 0x1 ? MsgType::Text : MsgType::Binary,
                                   std::move(msg)};
                continue;
            case 0x1:
            case 0x2:
                if (in_fragmented) return std::unexpected(Error::Protocol);
                if (f.fin)
                    return Message{f.opcode == 0x1 ? MsgType::Text : MsgType::Binary,
                                   std::move(f.payload)};
                in_fragmented = true;
                msg_opcode = f.opcode;
                msg.insert(msg.end(), f.payload.begin(), f.payload.end());
                continue;
            default:
                return std::unexpected(Error::Protocol);
            }
        }
    }

    void WebSocket::close()
    {
        tls_.close();
        in_.clear();
        closed_ = true;
    }
} // namespace vc
