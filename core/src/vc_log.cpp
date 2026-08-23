/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_log.h"
#include "vc/vc_os.h"

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>

namespace vc::log
{
    namespace
    {
        /*
         * One process-wide sink, so the state is shared and must be guarded:
         * the run loop is not the only possible caller (a service control
         * handler or a signal path can reach it too).
         */
        std::mutex g_mu;
        Config g_cfg;
        std::FILE* g_file = nullptr;
        bool g_init = false;
        std::uint64_t g_written = 0; /* bytes in the current file */
        bool g_file_broken = false;  /* reopen failed; reported once */

        /* Query-string and JSON keys whose values must never reach the log. */
        constexpr std::string_view kSecretKeys[] = {"sb-hc-token", "sig", "password", "accesskey",
                                                    "sharedaccesssignature"};

        const char* level_tag(Level l)
        {
            switch (l)
            {
            case Level::Trace:
                return "TRACE";
            case Level::Debug:
                return "DEBUG";
            case Level::Info:
                return "INFO ";
            case Level::Succ:
                return "SUCC ";
            case Level::Warn:
                return "WARN ";
            case Level::Error:
                return "ERROR";
            }
            return "?????";
        }

        bool iequals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        std::string timestamp()
        {
            const os::LocalTime t = os::local_time();
            char buf[40];
            if (g_cfg.time_precision)
                std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d", t.year,
                              t.month, t.day, t.hour, t.minute, t.second, t.millisecond);
            else
                std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month,
                              t.day, t.hour, t.minute, t.second);
            return buf;
        }

        void rotate_if_needed()
        {
            if (!g_file || g_cfg.max_file_size_mb <= 0) return;
            /* 64-bit: max_file_size_mb * 1024 * 1024 overflows a 32-bit long
             * on Windows for any setting above 2047. */
            const std::uint64_t limit =
                static_cast<std::uint64_t>(g_cfg.max_file_size_mb) * 1024u * 1024u;
            if (g_written < limit) return;

            std::fclose(g_file);
            g_file = nullptr;

            const std::string& base = g_cfg.file_path;
            const int n = g_cfg.max_rotate_files > 0 ? g_cfg.max_rotate_files : 5;
            std::remove((base + "." + std::to_string(n)).c_str());
            for (int i = n - 1; i >= 1; i--)
                std::rename((base + "." + std::to_string(i)).c_str(),
                            (base + "." + std::to_string(i + 1)).c_str());
            std::rename(base.c_str(), (base + ".1").c_str());

            g_file = std::fopen(base.c_str(), "ab");
            g_written = 0;
            if (!g_file && !g_file_broken)
            {
                /* Losing the file silently is worse than the rotation failing:
                 * say so once on stderr and carry on with console output. */
                g_file_broken = true;
                std::fprintf(stderr,
                             "vc::log: cannot reopen %s after rotation; "
                             "file logging disabled\n",
                             base.c_str());
            }
        }

        /* Escape control characters. A payload containing a newline could
         * otherwise forge a timestamped log line. */
        void append_escaped(std::string& out, std::string_view in)
        {
            for (char c : in)
            {
                const auto u = static_cast<unsigned char>(c);
                if (u == '\n')
                    out += "\\n";
                else if (u == '\r')
                    out += "\\r";
                else if (u == '\t')
                    out += "\\t";
                else if (u < 0x20 || u == 0x7F)
                {
                    char esc[5];
                    std::snprintf(esc, sizeof esc, "\\x%02X", u);
                    out += esc;
                }
                else
                    out += c;
            }
        }

        bool starts_with_ci(std::string_view s, std::string_view prefix) noexcept
        {
            if (s.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); i++)
                if (std::tolower(static_cast<unsigned char>(s[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i])))
                    return false;
            return true;
        }

        /*
         * Mask the value following any secret key, in either "key=value" (URL
         * query) or "key":"value" (JSON) form. Applied at the sink so a
         * careless call site cannot leak a credential.
         */
        std::string mask_secrets(std::string_view in)
        {
            std::string out;
            out.reserve(in.size());
            std::size_t i = 0;
            while (i < in.size())
            {
                std::string_view rest = in.substr(i);
                std::size_t key_len = 0;
                for (std::string_view k : kSecretKeys)
                    if (starts_with_ci(rest, k))
                    {
                        key_len = k.size();
                        break;
                    }
                if (key_len == 0)
                {
                    out += in[i++];
                    continue;
                }

                /* Only a key if a separator follows, allowing for a closing
                 * quote and whitespace in the JSON form. */
                std::size_t j = i + key_len;
                if (j < in.size() && in[j] == '"') j++;
                while (j < in.size() && (in[j] == ' ' || in[j] == '\t'))
                    j++;
                if (j >= in.size() || (in[j] != '=' && in[j] != ':'))
                {
                    out += in[i++];
                    continue;
                }
                out.append(in.substr(i, j + 1 - i));
                j++;

                while (j < in.size() && (in[j] == ' ' || in[j] == '\t'))
                    j++;
                const bool quoted = j < in.size() && in[j] == '"';
                if (quoted) j++;
                const std::size_t start = j;
                while (j < in.size() &&
                       (quoted ? in[j] != '"'
                               : (in[j] != '&' && in[j] != ',' && in[j] != '}' && in[j] != ' ')))
                    j++;
                if (j > start)
                {
                    if (quoted) out += '"';
                    out += "***";
                    if (quoted && j < in.size()) out += '"';
                }
                i = (quoted && j < in.size()) ? j + 1 : j;
            }
            return out;
        }

        /* Sink-side guarantees: mask, escape, then bound the length. */
        std::string sanitise(std::string_view msg)
        {
            std::string escaped;
            escaped.reserve(msg.size());
            append_escaped(escaped, msg);
            std::string out = mask_secrets(escaped);
            if (out.size() > kMaxMessageBytes)
            {
                const std::size_t dropped = out.size() - kMaxMessageBytes;
                out.resize(kMaxMessageBytes);
                out += " ... [truncated " + std::to_string(dropped) + " bytes]";
            }
            return out;
        }

    } // namespace

    Status init(const Config& cfg)
    {
        std::lock_guard lock(g_mu);
        if (g_file)
        {
            std::fclose(g_file);
            g_file = nullptr;
        }
        g_cfg = cfg;
        g_written = 0;
        g_file_broken = false;
        if (g_cfg.enabled && !g_cfg.file_path.empty())
        {
            g_file = std::fopen(g_cfg.file_path.c_str(), "ab");
            if (!g_file)
            {
                g_init = true; /* console still works; report the file failure */
                return std::unexpected(Error::Io);
            }
            /* Appending to an existing file: start rotation accounting from
             * its current size, not from zero. */
            if (std::fseek(g_file, 0, SEEK_END) == 0)
            {
                const long end = std::ftell(g_file);
                if (end > 0) g_written = static_cast<std::uint64_t>(end);
            }
        }
        g_init = true;
        return {};
    }

    void shutdown()
    {
        std::lock_guard lock(g_mu);
        if (g_file)
        {
            std::fclose(g_file);
            g_file = nullptr;
        }
        g_init = false;
        g_written = 0;
    }

    Level level_from_str(std::string_view s)
    {
        if (iequals(s, "LOG_TRACE") || iequals(s, "TRACE")) return Level::Trace;
        if (iequals(s, "LOG_DEBUG") || iequals(s, "DEBUG")) return Level::Debug;
        if (iequals(s, "LOG_INFO") || iequals(s, "INFO")) return Level::Info;
        if (iequals(s, "LOG_WARN") || iequals(s, "WARN")) return Level::Warn;
        if (iequals(s, "LOG_ERROR") || iequals(s, "ERROR")) return Level::Error;
        return Level::Info;
    }

    bool enabled(Level lvl) noexcept
    {
        std::lock_guard lock(g_mu);
        if (!g_init) return true; /* pre-init messages go to stderr */
        if (!g_cfg.enabled) return false;
        const Level eff = (lvl == Level::Succ) ? Level::Info : lvl;
        return eff >= g_cfg.level;
    }

    void write(Level lvl, std::string_view msg)
    {
        const std::string text = sanitise(msg);

        std::lock_guard lock(g_mu);
        if (!g_init)
        {
            /* Not initialised: still echo to stderr so nothing is lost. */
            std::fprintf(stderr, "%s\n", text.c_str());
            return;
        }
        if (!g_cfg.enabled) return;
        const Level eff = (lvl == Level::Succ) ? Level::Info : lvl;
        if (eff < g_cfg.level) return;

        const std::string ts = timestamp();

        if (g_cfg.console)
        {
            /* Diagnostics on stderr, so a caller can separate them from
             * ordinary output. */
            std::FILE* out = (eff >= Level::Warn) ? stderr : stdout;
            if (g_cfg.show_event_type)
                std::fprintf(out, "%s [%s] %s\n", ts.c_str(), level_tag(lvl), text.c_str());
            else
                std::fprintf(out, "%s %s\n", ts.c_str(), text.c_str());
            std::fflush(out);
        }
        if (g_file)
        {
            const int n =
                g_cfg.show_event_type
                    ? std::fprintf(g_file, "%s [%s] %s\n", ts.c_str(), level_tag(lvl), text.c_str())
                    : std::fprintf(g_file, "%s %s\n", ts.c_str(), text.c_str());
            if (n > 0) g_written += static_cast<std::uint64_t>(n);
            std::fflush(g_file);
            rotate_if_needed();
        }
    }
} // namespace vc::log
