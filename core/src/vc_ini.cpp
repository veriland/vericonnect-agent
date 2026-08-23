/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_ini.h"
#include "vc/vc_fs.h"

#include <cctype>
#include <cstdlib>

namespace vc
{
    namespace
    {
        bool iequals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        std::string_view trim(std::string_view s) noexcept
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
                s.remove_suffix(1);
            return s;
        }
    } // namespace

    Ini::Entry* Ini::find(std::string_view section, std::string_view key)
    {
        for (Entry& e : entries_)
            if (iequals(e.section, section) && iequals(e.key, key)) return &e;
        return nullptr;
    }

    const Ini::Entry* Ini::find(std::string_view section, std::string_view key) const
    {
        for (const Entry& e : entries_)
            if (iequals(e.section, section) && iequals(e.key, key)) return &e;
        return nullptr;
    }

    Result<Ini> Ini::load(const std::string& path)
    {
        Result<Bytes> data = fs::read_all(path);
        if (!data) return std::unexpected(data.error());

        Ini ini;
        std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
        std::string section;

        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t nl = text.find('\n', pos);
            std::string_view raw =
                (nl == std::string_view::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
            pos = (nl == std::string_view::npos) ? text.size() : nl + 1;

            std::string_view s = trim(raw);
            if (s.empty() || s.front() == ';' || s.front() == '#') continue;

            if (s.front() == '[')
            {
                std::size_t close = s.find(']');
                if (close != std::string_view::npos)
                    section = std::string(trim(s.substr(1, close - 1)));
            }
            else
            {
                std::size_t eq = s.find('=');
                if (eq != std::string_view::npos)
                {
                    std::string_view key = trim(s.substr(0, eq));
                    std::string_view val = trim(s.substr(eq + 1));
                    if (!key.empty()) ini.set(section, key, val);
                }
            }
        }
        return ini;
    }

    Status Ini::save(const std::string& path) const
    {
        std::string out;
        for (std::size_t i = 0; i < entries_.size(); i++)
        {
            /* Emit each section once, at its first-seen position. */
            bool seen = false;
            for (std::size_t j = 0; j < i; j++)
                if (iequals(entries_[j].section, entries_[i].section))
                {
                    seen = true;
                    break;
                }
            if (seen) continue;

            out += '[';
            out += entries_[i].section;
            out += "]\n";
            for (const Entry& e : entries_)
                if (iequals(e.section, entries_[i].section))
                {
                    out += e.key;
                    out += '=';
                    out += e.value;
                    out += '\n';
                }
            out += '\n';
        }
        return fs::write_all(
            path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(out.data()),
                                                out.size()));
    }

    std::optional<std::string_view> Ini::get(std::string_view section, std::string_view key) const
    {
        const Entry* e = find(section, key);
        if (!e) return std::nullopt;
        return std::string_view(e->value);
    }

    int Ini::get_int(std::string_view section, std::string_view key, int def) const
    {
        const Entry* e = find(section, key);
        if (!e || e->value.empty()) return def;
        /* A malformed value yields the caller's default rather than 0, which
         * would silently look like a deliberate setting. */
        const auto v = parse_uint(e->value, 0x7FFFFFFF);
        return v ? static_cast<int>(*v) : def;
    }

    bool Ini::get_bool(std::string_view section, std::string_view key, bool def) const
    {
        const Entry* e = find(section, key);
        if (!e || e->value.empty()) return def;
        return iequals(e->value, "1") || iequals(e->value, "true") || iequals(e->value, "yes") ||
               iequals(e->value, "on");
    }

    void Ini::set(std::string_view section, std::string_view key, std::string_view value)
    {
        if (Entry* e = find(section, key))
        {
            e->value.assign(value);
            return;
        }
        entries_.push_back(Entry{std::string(section), std::string(key), std::string(value)});
    }

    void Ini::set_int(std::string_view section, std::string_view key, int value)
    {
        set(section, key, std::to_string(value));
    }
} // namespace vc
