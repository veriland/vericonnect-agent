#include "vc/vc_os.h"

#include <ctime>
#include <unistd.h>
#include <fcntl.h>

namespace vc::os
{
    void sleep_ms(unsigned ms)
    {
        struct timespec ts = {ms / 1000, static_cast<long>(ms % 1000) * 1000000L};
        nanosleep(&ts, nullptr);
    }

    std::uint64_t monotonic_ms() noexcept
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000 +
            static_cast<std::uint64_t>(ts.tv_nsec) / 1000000;
    }

    Status random_bytes(std::span<std::uint8_t> buf)
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) return std::unexpected(Error::Fail);
        std::size_t off = 0;
        while (off < buf.size())
        {
            ssize_t n = read(fd, buf.data() + off, buf.size() - off);
            if (n <= 0)
            {
                close(fd);
                return std::unexpected(Error::Fail);
            }
            off += static_cast<std::size_t>(n);
        }
        close(fd);
        return {};
    }
} // namespace vc::os

