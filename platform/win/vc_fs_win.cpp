/* Windows-specific filesystem bits. The portable operations live in
 * core/src/vc_fs.cpp; only executable-path discovery is platform specific. */
#include "vc/vc_fs.h"

#include <string>
#include <windows.h>

namespace vc::fs
{
    namespace
    {
        std::string wide_to_utf8(const wchar_t* w)
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (len <= 0) return {};
            std::string s(static_cast<std::size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
            return s;
        }
    } // namespace

    std::optional<std::string> exe_dir()
    {
        wchar_t path[MAX_PATH * 4];
        DWORD n =
            GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof path / sizeof path[0]));
        if (n == 0) return std::nullopt;
        /* strip the filename */
        for (DWORD i = n; i > 0; i--)
        {
            if (path[i - 1] == L'\\' || path[i - 1] == L'/')
            {
                path[i - 1] = 0;
                break;
            }
        }
        return wide_to_utf8(path);
    }
} // namespace vc::fs
