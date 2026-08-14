/* Minimal HTTPS/1.1 client used by the test app sender. */
#include "vc/vc_http.h"
#include "vc/vc_tls.h"
#include "vc/vc_os.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace vc::http
{
    namespace
    {
        bool istarts_with(std::string_view s, std::string_view prefix) noexcept
        {
            if (s.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); i++)
                if (std::tolower(static_cast<unsigned char>(s[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i])))
                    return false;
            return true;
        }

        /* Return the value of header `name` in a raw header block, or nullopt. */
        std::optional<std::string_view> header_value(std::string_view headers, std::string_view name)
        {
            std::size_t pos = 0;
            while (pos < headers.size())
            {
                std::size_t eol = headers.find("\r\n", pos);
                std::string_view line = headers.substr(pos, eol == std::string_view::npos
                                                                ? std::string_view::npos
                                                                : eol - pos);
                if (istarts_with(line, name) && line.size() > name.size() &&
                    line[name.size()] == ':')
                {
                    std::string_view v = line.substr(name.size() + 1);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
                        v.remove_prefix(1);
                    return v;
                }
                if (eol == std::string_view::npos) break;
                pos = eol + 2;
            }
            return std::nullopt;
        }

        bool header_contains(std::string_view headers, std::string_view name, std::string_view needle)
        {
            std::optional<std::string_view> v = header_value(headers, name);
            if (!v) return false;
            for (std::size_t i = 0; i + needle.size() <= v->size(); i++)
                if (istarts_with(v->substr(i), needle)) return true;
            return false;
        }

        Bytes decode_chunked(std::string_view body)
        {
            Bytes out;
            const char* p = body.data();
            const char* end = body.data() + body.size();
            while (p < end)
            {
                char* after = nullptr;
                long chunk = std::strtol(p, &after, 16);
                if (!after || chunk < 0) break;
                /* advance past the CRLF following the size line */
                const char* crlf = nullptr;
                for (const char* q = after; q + 1 < end; q++)
                    if (q[0] == '\r' && q[1] == '\n')
                    {
                        crlf = q;
                        break;
                    }
                if (!crlf) break;
                p = crlf + 2;
                if (chunk == 0) break;
                if (p + chunk > end) chunk = static_cast<long>(end - p);
                out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(p),
                           reinterpret_cast<const std::uint8_t*>(p + chunk));
                p += chunk;
                if (p + 2 <= end && p[0] == '\r' && p[1] == '\n') p += 2;
            }
            return out;
        }
    } // namespace

    Result<Response> request(const Request& req)
    {
        Result<Socket> sock = Socket::connect(std::string(req.host), req.port, req.timeout_ms);
        if (!sock) return std::unexpected(Error::Io);
        Result<Tls> tls = Tls::connect(std::move(*sock), std::string(req.host), req.timeout_ms);
        if (!tls) return std::unexpected(Error::Tls);

        std::string request_head;
        request_head.append(req.method).append(" ").append(req.path_and_query)
                    .append(" HTTP/1.1\r\nHost: ").append(req.host).append("\r\n");
        if (!req.body.empty())
        {
            request_head += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
            request_head += "Content-Type: ";
            request_head += req.content_type.empty() ? "application/json" : req.content_type;
            request_head += "\r\n";
        }
        request_head.append(req.extra_headers);
        request_head += "Connection: close\r\n\r\n";

        if (!tls->send(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(request_head.data()), request_head.size())))
            return std::unexpected(Error::Io);
        if (!req.body.empty() && !tls->send(req.body))
            return std::unexpected(Error::Io);

        /* Read until close (Connection: close) or Content-Length satisfied. */
        std::string in;
        std::uint64_t deadline = os::monotonic_ms() + static_cast<std::uint64_t>(req.timeout_ms);
        std::uint8_t tmp[8192];
        for (;;)
        {
            std::uint64_t now = os::monotonic_ms();
            if (now >= deadline) break;
            auto n = tls->recv(std::span<std::uint8_t>(tmp, sizeof tmp),
                               static_cast<int>(deadline - now));
            if (!n || *n == 0) break;
            in.append(reinterpret_cast<const char*>(tmp), *n);

            std::size_t he = in.find("\r\n\r\n");
            if (he != std::string::npos)
            {
                std::string_view head(in.data(), he);
                std::optional<std::string_view> cl = header_value(head, "Content-Length");
                bool chunked = header_contains(head, "Transfer-Encoding", "chunked");
                if (cl && !chunked)
                {
                    long len = std::atol(std::string(*cl).c_str());
                    if (in.size() >= he + 4 + static_cast<std::size_t>(len)) break;
                }
            }
        }

        std::size_t he = in.find("\r\n\r\n");
        if (he == std::string::npos) return std::unexpected(Error::Protocol);
        std::size_t body_off = he + 4;

        if (!istarts_with(in, "HTTP/")) return std::unexpected(Error::Protocol);
        std::size_t sp = in.find(' ');
        if (sp == std::string::npos) return std::unexpected(Error::Protocol);

        Response resp;
        resp.status = std::atoi(in.c_str() + sp + 1);
        std::size_t sp2 = in.find(' ', sp + 1);
        std::size_t eol = in.find("\r\n");
        if (sp2 != std::string::npos && eol != std::string::npos && sp2 < eol)
            resp.status_text = in.substr(sp2 + 1, eol - sp2 - 1);

        resp.headers = in.substr(0, he);

        if (header_contains(resp.headers, "Transfer-Encoding", "chunked"))
        {
            resp.body = decode_chunked(std::string_view(in).substr(body_off));
        }
        else
        {
            resp.body.assign(reinterpret_cast<const std::uint8_t*>(in.data() + body_off),
                             reinterpret_cast<const std::uint8_t*>(in.data() + in.size()));
        }
        return resp;
    }
} // namespace vc::http

