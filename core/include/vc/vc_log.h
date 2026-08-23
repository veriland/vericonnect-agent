/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_log.h - logger with console + rotating file output.
 * Configured by the [Logging] section of Settings.ini.
 */
#ifndef VC_LOG_H
#define VC_LOG_H

#include "vc_common.h"

#ifdef __cplusplus

#include <cstddef>
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

    [[nodiscard]] Status init(const Config& cfg);
    void shutdown();

    [[nodiscard]] Level level_from_str(std::string_view s); /* "LOG_DEBUG" etc. */

    /*
     * Longest message written; anything longer is truncated with a marker. A
     * log line must not be able to grow without bound, whatever a caller
     * hands it.
     */
    constexpr std::size_t kMaxMessageBytes = 8192;

    /* Would a message at this level be emitted? Cheap - call it before doing
     * work that exists only to be logged. */
    [[nodiscard]] bool enabled(Level lvl) noexcept;

    /*
     * Emit a preformatted message. Control characters are escaped so a payload
     * cannot forge a log line, values of known secret keys are masked, and the
     * result is truncated to kMaxMessageBytes.
     */
    void write(Level lvl, std::string_view msg);

    /* Emit a std::format-style message. Formatting is skipped when the level is
     * suppressed. */
    template <class... Args>
    void message(Level lvl, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!enabled(lvl)) return;
        write(lvl, std::vformat(fmt.get(), std::make_format_args(args...)));
    }
} // namespace vc::log

#endif /* __cplusplus */

#endif
