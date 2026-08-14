/* Windows TCP client socket (WinSock2). */
#include "vc/vc_sock.h"

#include <cstdio>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace vc
{
    namespace
    {
        LONG g_ws_init = 0;
    }

    Socket::~Socket() { close(); }

    Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    Socket& Socket::operator=(Socket&& other) noexcept
    {
        if (this != &other)
        {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    void Socket::close() noexcept
    {
        if (fd_ >= 0)
        {
            closesocket(static_cast<SOCKET>(fd_));
            fd_ = -1;
        }
    }

    void Socket::global_init() noexcept
    {
        if (InterlockedCompareExchange(&g_ws_init, 1, 0) == 0)
        {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) g_ws_init = 0;
        }
    }

    void Socket::global_cleanup() noexcept
    {
        if (InterlockedCompareExchange(&g_ws_init, 0, 1) == 1) WSACleanup();
    }

    Result<Socket> Socket::connect(const std::string& host, int port, int timeout_ms)
    {
        global_init();

        char portstr[16];
        std::snprintf(portstr, sizeof portstr, "%d", port);

        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0 || !res)
            return std::unexpected(Error::NotFound);

        SOCKET sock = INVALID_SOCKET;
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next)
        {
            sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (sock == INVALID_SOCKET) continue;

            u_long nb = 1;
            ioctlsocket(sock, FIONBIO, &nb);
            int cr = ::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
            if (cr == 0)
            {
                nb = 0;
                ioctlsocket(sock, FIONBIO, &nb);
                break;
            }
            if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(sock, &wfds);
                struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
                if (select(0, nullptr, &wfds, nullptr, &tv) > 0)
                {
                    int err = 0;
                    int len = sizeof err;
                    getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
                    if (err == 0)
                    {
                        nb = 0;
                        ioctlsocket(sock, FIONBIO, &nb);
                        break;
                    }
                }
            }
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        freeaddrinfo(res);
        if (sock == INVALID_SOCKET) return std::unexpected(Error::Io);

        BOOL ka = TRUE;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&ka), sizeof ka);
        return Socket(static_cast<std::intptr_t>(sock));
    }

    Result<std::size_t> Socket::send(std::span<const std::uint8_t> data)
    {
        const char* p = reinterpret_cast<const char*>(data.data());
        std::size_t sent = 0;
        while (sent < data.size())
        {
            int n = ::send(static_cast<SOCKET>(fd_), p + sent,
                           static_cast<int>(data.size() - sent), 0);
            if (n == SOCKET_ERROR) return std::unexpected(Error::Io);
            sent += static_cast<std::size_t>(n);
        }
        return sent;
    }

    Result<std::size_t> Socket::recv(std::span<std::uint8_t> buf, int timeout_ms)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(static_cast<SOCKET>(fd_), &rfds);
        struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        int sel = select(0, &rfds, nullptr, nullptr, timeout_ms < 0 ? nullptr : &tv);
        if (sel == 0) return std::unexpected(Error::Timeout);
        if (sel == SOCKET_ERROR) return std::unexpected(Error::Io);

        int n = ::recv(static_cast<SOCKET>(fd_), reinterpret_cast<char*>(buf.data()),
                       static_cast<int>(buf.size()), 0);
        if (n == 0) return std::size_t{0};
        if (n == SOCKET_ERROR) return std::unexpected(Error::Io);
        return static_cast<std::size_t>(n);
    }
} // namespace vc
