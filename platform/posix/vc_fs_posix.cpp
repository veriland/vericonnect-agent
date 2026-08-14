/* POSIX-specific filesystem bits. The portable operations live in
 * core/src/vc_fs.cpp; only executable-path discovery is platform specific. */
#include "vc/vc_fs.h"

#include <cstring>
#include <string>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace vc::fs
{
    std::optional<std::string> exe_dir()
    {
        char path[4096];
#if defined(__APPLE__)
        std::uint32_t size = sizeof path;
        if (_NSGetExecutablePath(path, &size) != 0) return std::nullopt;
#else
        ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
        if (n <= 0) return std::nullopt;
        path[n] = 0;
#endif
        std::string p(path);
        std::size_t slash = p.find_last_of('/');
        if (slash != std::string::npos) p.resize(slash);
        return p;
    }
} // namespace vc::fs
