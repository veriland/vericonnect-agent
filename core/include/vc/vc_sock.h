/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_sock.h - minimal TCP client socket abstraction.
 * Implemented per platform (platform/win, platform/posix).
 */
#ifndef VC_SOCK_H
#define VC_SOCK_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstdint>
#include <span>
#include <string>

namespace vc
{
    /* A connected TCP client socket. Move-only; the descriptor is closed on
     * destruction. */
    class Socket
    {
    public:
        Socket() noexcept = default;
        ~Socket();
        Socket(Socket&& other) noexcept;
        Socket& operator=(Socket&& other) noexcept;
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        static void global_init() noexcept; /* WSAStartup on Windows */
        static void global_cleanup() noexcept;

        /* Connect to host:port (TCP). timeout_ms bounds the connect. */
        [[nodiscard]] static Result<Socket> connect(const std::string& host, int port,
                                                    int timeout_ms);

        /* Send all bytes; returns the count sent or an error. */
        [[nodiscard]] Result<std::size_t> send(std::span<const std::uint8_t> data);

        /* Returns bytes read (>0), 0 on orderly close, or an error
         * (Error::Timeout if nothing arrived within timeout_ms). */
        [[nodiscard]] Result<std::size_t> recv(std::span<std::uint8_t> buf, int timeout_ms);

        bool valid() const noexcept
        {
            return fd_ >= 0;
        }
        void close() noexcept;

    private:
        explicit Socket(std::intptr_t fd) noexcept : fd_(fd) {}

        std::intptr_t fd_ = -1;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
