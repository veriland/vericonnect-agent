/*
 * vc_fs.h - portable filesystem operations (UTF-8 paths).
 */
#ifndef VC_FS_H
#define VC_FS_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vc::fs
{
    bool file_exists(const std::string& path);
    bool dir_exists(const std::string& path);
    Status mkdir(const std::string& path); /* single level; OK if exists */
    Status remove_file(const std::string& path);
    Status move(const std::string& from, const std::string& to); /* fails if to exists */

    /* Read an entire file into a byte buffer. */
    Result<Bytes> read_all(const std::string& path);

    /* Write an entire file (creates/truncates). */
    Status write_all(const std::string& path, std::span<const std::uint8_t> data);

    /* Names of regular files (not directories) in dir. */
    Result<std::vector<std::string>> list_files(const std::string& dir);

    /* Join two path segments. */
    std::string join(std::string_view a, std::string_view b);

    /* Directory containing the current executable. */
    std::optional<std::string> exe_dir();
} // namespace vc::fs

#endif /* __cplusplus */

#endif
