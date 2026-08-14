/*
 * vc_log.h - logger with console + rotating file output.
 * Configured by the [Logging] section of Settings.ini.
 */
#ifndef VC_LOG_H
#define VC_LOG_H

#include "vc_common.h"

#ifdef __cplusplus

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace vc::log
{
    enum class Level
    {
        Trace = 0,
        Debug,
        Info,
        Succ /* logged at Info */,
        Warn,
        Error
    };

    struct Config
    {
        Level level = Level::Info;
        bool enabled = true;
        bool console = true;         /* echo to stdout             */
        bool show_event_type = true; /* [INFO] tags                */
        bool time_precision = true;  /* milliseconds in timestamps */
        std::string file_path;       /* empty = no file logging    */
        int max_file_size_mb = 10;
        int max_rotate_files = 10;
    };

    Status init(const Config& cfg);
    void shutdown();

    Level level_from_str(std::string_view s); /* "LOG_DEBUG" etc. */

    /* Emit a preformatted message. */
    void write(Level lvl, std::string_view msg);

    /* Emit a std::format-style message. */
    template <class... Args>
    void message(Level lvl, std::format_string<Args...> fmt, Args&&... args)
    {
        write(lvl, std::vformat(fmt.get(), std::make_format_args(args...)));
    }
} // namespace vc::log

#endif /* __cplusplus */

#endif
