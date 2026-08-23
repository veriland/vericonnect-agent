/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/* POSIX TCP client socket (Linux/macOS). */
#include "vc/vc_sock.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace vc
{
    Socket::~Socket()
    {
        close();
    }

    Socket::Socket(Socket&& other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

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
            ::close(static_cast<int>(fd_));
            fd_ = -1;
        }
    }

    void Socket::global_init() noexcept {}

    void Socket::global_cleanup() noexcept {}

    Result<Socket> Socket::connect(const std::string& host, int port, int timeout_ms)
    {
        char portstr[16];
        std::snprintf(portstr, sizeof portstr, "%d", port);

        struct addrinfo hints{};
        struct addrinfo* res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0 || !res)
            return std::unexpected(Error::NotFound);

        int fd = -1;
        for (struct addrinfo* ai = res; ai; ai = ai->ai_next)
        {
            fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) continue;
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            int cr = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
            if (cr == 0)
            {
                fcntl(fd, F_SETFL, fl);
                break;
            }
            if (errno == EINPROGRESS)
            {
                fd_set w;
                FD_ZERO(&w);
                FD_SET(fd, &w);
                struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
                if (select(fd + 1, nullptr, &w, nullptr, &tv) > 0)
                {
                    int err = 0;
                    socklen_t l = sizeof err;
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
                    if (err == 0)
                    {
                        fcntl(fd, F_SETFL, fl);
                        break;
                    }
                }
            }
            ::close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        if (fd < 0) return std::unexpected(Error::Io);

        int ka = 1;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof ka);
        return Socket(static_cast<std::intptr_t>(fd));
    }

    Status Socket::send(std::span<const std::uint8_t> data)
    {
        const std::uint8_t* p = data.data();
        std::size_t sent = 0;
        while (sent < data.size())
        {
            ssize_t n = ::send(static_cast<int>(fd_), p + sent, data.size() - sent, 0);
            if (n <= 0)
            {
                if (errno == EINTR) continue;
                return std::unexpected(Error::Io);
            }
            sent += static_cast<std::size_t>(n);
        }
        return {};
    }

    Result<std::size_t> Socket::recv(std::span<std::uint8_t> buf, int timeout_ms)
    {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(static_cast<int>(fd_), &r);
        struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        int sel =
            select(static_cast<int>(fd_) + 1, &r, nullptr, nullptr, timeout_ms < 0 ? nullptr : &tv);
        if (sel == 0) return std::unexpected(Error::Timeout);
        if (sel < 0) return std::unexpected(Error::Io);
        ssize_t n = ::recv(static_cast<int>(fd_), buf.data(), buf.size(), 0);
        if (n == 0) return std::size_t{0};
        if (n < 0) return std::unexpected(Error::Io);
        return static_cast<std::size_t>(n);
    }
} // namespace vc
