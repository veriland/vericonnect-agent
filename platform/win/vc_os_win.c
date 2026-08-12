#include "vc/vc_os.h"
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

void vc_os_sleep_ms(unsigned ms) { Sleep(ms); }

uint64_t vc_os_monotonic_ms(void)
{
    return (uint64_t)GetTickCount64();
}

int vc_os_random(void *buf, size_t len)
{
    NTSTATUS s = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return s == 0 ? VC_OK : VC_E_FAIL;
}
