/* vc_os.h - misc OS helpers implemented per platform. */
#ifndef VC_OS_H
#define VC_OS_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstdint>
#include <span>

namespace vc::os
{
    /* Sleep for at least ms milliseconds. */
    void sleep_ms(unsigned ms);

    /* Monotonic clock in milliseconds (unaffected by wall-clock changes). */
    std::uint64_t monotonic_ms() noexcept;

    /* Fill buf with cryptographically secure random bytes. */
    Status random_bytes(std::span<std::uint8_t> buf);
} // namespace vc::os

#endif /* __cplusplus */

#endif
