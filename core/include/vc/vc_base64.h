/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/* vc_base64.h - RFC 4648 base64 encode/decode. */
#ifndef VC_BASE64_H
#define VC_BASE64_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vc
{
    /* Encode bytes to a base64 string. */
    std::string base64_encode(std::span<const std::uint8_t> data);
    std::string base64_encode(std::string_view s);

    /* Decode a base64 string. Whitespace is ignored; returns nullopt on invalid
     * input. */
    [[nodiscard]] std::optional<Bytes> base64_decode(std::string_view text);
} // namespace vc

#endif /* __cplusplus */

#endif
