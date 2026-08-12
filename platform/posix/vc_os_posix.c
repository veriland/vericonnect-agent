#include "vc/vc_os.h"
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

void vc_os_sleep_ms(unsigned ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

uint64_t vc_os_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int vc_os_random(void *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return VC_E_FAIL;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, (char *)buf + off, len - off);
        if (n <= 0) { close(fd); return VC_E_FAIL; }
        off += (size_t)n;
    }
    close(fd);
    return VC_OK;
}
