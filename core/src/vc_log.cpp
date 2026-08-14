#include "vc/vc_log.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(_WIN32)
#include <windows.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

namespace vc::log
{
    namespace
    {
        Config g_cfg;
        std::FILE* g_file = nullptr;
        bool g_init = false;

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
            std::time_t t = std::time(nullptr);
            std::tm tmv;
#if defined(_WIN32)
            localtime_s(&tmv, &t);
            struct _timeb tb;
            _ftime_s(&tb);
            int ms = tb.millitm;
#else
            localtime_r(&t, &tmv);
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            int ms = static_cast<int>(tv.tv_usec / 1000);
#endif
            char buf[40];
            if (g_cfg.time_precision)
                std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                              tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                              tmv.tm_min, tmv.tm_sec, ms);
            else
                std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900,
                              tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            return buf;
        }

        void rotate_if_needed()
        {
            if (!g_file || g_cfg.max_file_size_mb <= 0) return;
            long pos = std::ftell(g_file);
            if (pos < static_cast<long>(g_cfg.max_file_size_mb) * 1024 * 1024) return;

            std::fclose(g_file);
            g_file = nullptr;

            const std::string& base = g_cfg.file_path;
            int n = g_cfg.max_rotate_files > 0 ? g_cfg.max_rotate_files : 5;
            std::string last = base + "." + std::to_string(n);
            std::remove(last.c_str());
            for (int i = n - 1; i >= 1; i--)
            {
                std::string from = base + "." + std::to_string(i);
                std::string to = base + "." + std::to_string(i + 1);
                std::rename(from.c_str(), to.c_str());
            }
            std::rename(base.c_str(), (base + ".1").c_str());

            g_file = std::fopen(base.c_str(), "ab");
        }
    } // namespace

    Status init(const Config& cfg)
    {
        shutdown();
        g_cfg = cfg;
        if (g_cfg.enabled && !g_cfg.file_path.empty())
        {
            g_file = std::fopen(g_cfg.file_path.c_str(), "ab");
            if (!g_file) return std::unexpected(Error::Io);
        }
        g_init = true;
        return {};
    }

    void shutdown()
    {
        if (g_file)
        {
            std::fclose(g_file);
            g_file = nullptr;
        }
        g_init = false;
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

    void write(Level lvl, std::string_view msg)
    {
        if (!g_init)
        {
            /* not initialised: still echo to stderr so nothing is lost */
            std::fprintf(stderr, "%.*s\n", static_cast<int>(msg.size()), msg.data());
            return;
        }
        if (!g_cfg.enabled) return;
        Level eff = (lvl == Level::Succ) ? Level::Info : lvl;
        if (eff < g_cfg.level) return;

        std::string ts = timestamp();

        if (g_cfg.console)
        {
            if (g_cfg.show_event_type)
                std::printf("%s [%s] %.*s\n", ts.c_str(), level_tag(lvl),
                            static_cast<int>(msg.size()), msg.data());
            else
                std::printf("%s %.*s\n", ts.c_str(), static_cast<int>(msg.size()), msg.data());
            std::fflush(stdout);
        }
        if (g_file)
        {
            if (g_cfg.show_event_type)
                std::fprintf(g_file, "%s [%s] %.*s\n", ts.c_str(), level_tag(lvl),
                             static_cast<int>(msg.size()), msg.data());
            else
                std::fprintf(g_file, "%s %.*s\n", ts.c_str(), static_cast<int>(msg.size()),
                             msg.data());
            std::fflush(g_file);
            rotate_if_needed();
        }
    }
} // namespace vc::log
