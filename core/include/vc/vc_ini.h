/* vc_ini.h - INI file reader/writer (Settings.ini). */
#ifndef VC_INI_H
#define VC_INI_H

#include "vc_common.h"

#ifdef __cplusplus

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vc
{
    /* Ordered INI store. Keys are grouped by section; insertion order is
     * preserved so save() reproduces a stable layout. Lookups are
     * case-insensitive on both section and key. */
    class Ini
    {
    public:
        [[nodiscard]] static Result<Ini> load(const std::string& path);
        [[nodiscard]] Status save(const std::string& path) const;

        [[nodiscard]] std::optional<std::string_view> get(std::string_view section,
                                                          std::string_view key) const;
        int get_int(std::string_view section, std::string_view key, int def) const;
        bool get_bool(std::string_view section, std::string_view key, bool def) const;

        void set(std::string_view section, std::string_view key, std::string_view value);
        void set_int(std::string_view section, std::string_view key, int value);

    private:
        struct Entry
        {
            std::string section, key, value;
        };

        Entry* find(std::string_view section, std::string_view key);
        const Entry* find(std::string_view section, std::string_view key) const;

        std::vector<Entry> entries_;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
