/* Portable filesystem operations (std::filesystem). The only platform
 * specific piece is exe_dir(), which lives in platform/<os>/vc_fs_*.cpp.
 *
 * All paths crossing this module are UTF-8. std::filesystem::path built from a
 * plain std::string uses the OS narrow encoding (ACP on Windows), so paths are
 * converted through char8_t to force UTF-8 interpretation on every platform. */
#include "vc/vc_fs.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace stdfs = std::filesystem;

namespace vc::fs
{
    namespace
    {
        stdfs::path to_path(std::string_view utf8)
        {
            return stdfs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()),
                                             utf8.size()));
        }

        std::string from_path(const stdfs::path& p)
        {
            std::u8string s = p.u8string();
            return std::string(reinterpret_cast<const char*>(s.data()), s.size());
        }
    } // namespace

    bool file_exists(const std::string& path)
    {
        std::error_code ec;
        return stdfs::is_regular_file(to_path(path), ec);
    }

    bool dir_exists(const std::string& path)
    {
        std::error_code ec;
        return stdfs::is_directory(to_path(path), ec);
    }

    Status mkdir(const std::string& path)
    {
        std::error_code ec;
        if (stdfs::create_directory(to_path(path), ec)) return {};
        if (dir_exists(path)) return {};
        return std::unexpected(Error::Io);
    }

    Status remove_file(const std::string& path)
    {
        std::error_code ec;
        return stdfs::remove(to_path(path), ec) ? Status{} : std::unexpected(Error::Io);
    }

    Status move(const std::string& from, const std::string& to)
    {
        std::error_code ec;
        if (stdfs::exists(to_path(to), ec)) return std::unexpected(Error::Exists);
        stdfs::rename(to_path(from), to_path(to), ec);
        return ec ? std::unexpected(Error::Io) : Status{};
    }

    Result<Bytes> read_all(const std::string& path)
    {
        std::ifstream f(to_path(path), std::ios::binary);
        if (!f) return std::unexpected(Error::NotFound);
        Bytes out{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        if (f.bad()) return std::unexpected(Error::Io);
        return out;
    }

    Status write_all(const std::string& path, std::span<const std::uint8_t> data)
    {
        std::ofstream f(to_path(path), std::ios::binary | std::ios::trunc);
        if (!f) return std::unexpected(Error::Io);
        if (!data.empty())
            f.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
        return f.good() ? Status{} : std::unexpected(Error::Io);
    }

    Result<std::vector<std::string>> list_files(const std::string& dir)
    {
        std::error_code ec;
        stdfs::directory_iterator it(to_path(dir), ec), end;
        if (ec) return std::unexpected(Error::NotFound);

        std::vector<std::string> names;
        for (; it != end; it.increment(ec))
        {
            if (ec) break;
            std::error_code fec;
            if (stdfs::is_regular_file(it->status(fec)) && !fec)
                names.push_back(from_path(it->path().filename()));
        }
        return names;
    }

    std::string join(std::string_view a, std::string_view b)
    {
        if (a.empty()) return std::string(b);
        if (b.empty()) return std::string(a);
        std::string out(a);
        if (out.back() != '/') out += '/';
        std::size_t i = 0;
        while (i < b.size() && b[i] == '/') i++;
        out.append(b.substr(i));
        return out;
    }
} // namespace vc::fs
