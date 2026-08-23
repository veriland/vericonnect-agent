/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_transport.h - the byte-stream a protocol runs over.
 *
 * The WebSocket, HTTP and relay layers are written against this concept
 * rather than against vc::Tls directly, so a test can drive them from a
 * scripted in-memory transport with no network. See DESIGN.md §4: the set of
 * implementations is known at build time, so this is compile-time
 * polymorphism and costs nothing at runtime - there is no vtable in the
 * framing path.
 *
 * vc::Tls and vc::Socket both satisfy it.
 */
#ifndef VC_TRANSPORT_H
#define VC_TRANSPORT_H

#include "vc_common.h"

#ifdef __cplusplus

#include <concepts>
#include <cstdint>
#include <span>

namespace vc
{
    template <class T>
    concept Transport = requires(T& t, std::span<const std::uint8_t> out,
                                 std::span<std::uint8_t> in, int timeout_ms) {
        /* Send every byte, or fail. */
        { t.send(out) } -> std::same_as<Status>;
        /* Bytes read (>0), 0 on orderly close, Error::Timeout if none arrived. */
        { t.recv(in, timeout_ms) } -> std::same_as<Result<std::size_t>>;
        { t.close() };
        { t.valid() } -> std::convertible_to<bool>;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
