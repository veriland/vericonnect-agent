#include "vc/vc_os.h"

#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace vc::os
{
    void sleep_ms(unsigned ms)
    {
        Sleep(ms);
    }

    std::uint64_t monotonic_ms() noexcept
    {
        return static_cast<std::uint64_t>(GetTickCount64());
    }

    Status random_bytes(std::span<std::uint8_t> buf)
    {
        NTSTATUS s = BCryptGenRandom(nullptr, buf.data(), static_cast<ULONG>(buf.size()),
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return s == 0 ? Status{} : std::unexpected(Error::Fail);
    }
} // namespace vc::os
