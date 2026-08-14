/* vc_sha256.h - SHA-256 and HMAC-SHA256 (portable, no OS deps). */
#ifndef VC_SHA256_H
#define VC_SHA256_H

#include "vc_common.h"

#ifdef __cplusplus

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace vc
{
    inline constexpr std::size_t kSha256DigestLen = 32;

    using Sha256Digest = std::array<std::uint8_t, kSha256DigestLen>;

    /* Incremental SHA-256 hasher. Construct, update() one or more times, finish(). */
    class Sha256
    {
    public:
        Sha256() noexcept { reset(); }

        void reset() noexcept;
        Sha256& update(std::span<const std::uint8_t> data) noexcept;
        Sha256& update(std::string_view s) noexcept;
        Sha256Digest finish() noexcept;

    private:
        void block(const std::uint8_t* p) noexcept;

        std::uint32_t state_[8];
        std::uint64_t bitlen_;
        std::uint8_t buffer_[64];
        std::size_t buflen_;
    };

    /* One-shot helpers. */
    Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept;
    Sha256Digest sha256(std::string_view s) noexcept;

    Sha256Digest hmac_sha256(std::span<const std::uint8_t> key,
                             std::span<const std::uint8_t> msg) noexcept;
    Sha256Digest hmac_sha256(std::string_view key, std::string_view msg) noexcept;
} // namespace vc

#endif /* __cplusplus */

#endif
