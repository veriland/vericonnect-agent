/*
 * vc_common.h - shared basics for the VeriConnect code base.
 *
 * All strings crossing module boundaries are UTF-8 encoded. Platform layers
 * convert to native encodings (UTF-16 on Windows) internally.
 *
 * This header exposes two surfaces during the C++ migration:
 *   - The modern C++ vocabulary in namespace vc (Error, Result, Bytes, ...).
 *   - A small C-ABI allocator boundary (vc_alloc/vc_free/...) that is kept for
 *     the adapter DLL contract, where a buffer allocated on one side is freed
 *     on the other.
 */
#ifndef VC_COMMON_H
#define VC_COMMON_H

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace vc
{
    /* Generic result codes (negative = error). */
    enum class Error : int
    {
        Fail = -1,
        NoMem = -2,
        InvalidArg = -3,
        NotFound = -4,
        Io = -5,
        Timeout = -6,
        Closed = -7,
        Protocol = -8,
        Tls = -9,
        Exists = -10,
        Unsupported = -11,
    };

    /* A value of type T on success, or an Error. */
    template <class T> using Result = std::expected<T, Error>;

    /* Success or an Error, carrying no value. */
    using Status = std::expected<void, Error>;

    /* Owned byte buffer. */
    using Bytes = std::vector<std::uint8_t>;

    /* Short human-readable name for an error code (for logging/diagnostics). */
    std::string_view error_str(Error e) noexcept;
} // namespace vc

#endif /* __cplusplus */

/* ------------------------------------------------------------------------
 * Allocator boundary for the adapter DLL contract: the host allocates a
 * buffer that a dynamically loaded adapter frees (and vice versa), so both
 * sides must share one allocator. Not for general use - prefer std::
 * containers in new code.
 * ---------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif

    void* vc_alloc(size_t n);
    void vc_free(void* p);

#ifdef __cplusplus
}
#endif

#endif /* VC_COMMON_H */
