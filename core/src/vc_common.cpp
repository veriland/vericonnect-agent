#include "vc/vc_common.h"

#include <cstdlib>
#include <cstring>

namespace vc
{
    std::string_view error_str(Error e) noexcept
    {
        switch (e)
        {
        case Error::Fail: return "failure";
        case Error::NoMem: return "out of memory";
        case Error::InvalidArg: return "invalid argument";
        case Error::NotFound: return "not found";
        case Error::Io: return "I/O error";
        case Error::Timeout: return "timeout";
        case Error::Closed: return "closed";
        case Error::Protocol: return "protocol error";
        case Error::Tls: return "TLS error";
        case Error::Exists: return "already exists";
        case Error::Unsupported: return "unsupported";
        }
        return "unknown error";
    }
} // namespace vc

/* Adapter-ABI allocator boundary (see vc_common.h). */
extern "C" {
void* vc_alloc(size_t n) { return std::malloc(n ? n : 1); }
void vc_free(void* p) { std::free(p); }
} // extern "C"
