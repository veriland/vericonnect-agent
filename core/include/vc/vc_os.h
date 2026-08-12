/* vc_os.h - misc OS helpers implemented per platform. */
#ifndef VC_OS_H
#define VC_OS_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

void     vc_os_sleep_ms(unsigned ms);
uint64_t vc_os_monotonic_ms(void);
/* Cryptographically random bytes (BCryptGenRandom / /dev/urandom). */
int      vc_os_random(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
