#include "vc/vc_settings.h"
#include "vc/vc_ini.h"
#include "vc/vc_fs.h"

namespace vc
{
    namespace
    {
        std::string get_or(const Ini& ini, std::string_view section, std::string_view key,
                           std::string_view def)
        {
            return std::string(ini.get(section, key).value_or(def));
        }

        const char* level_name(log::Level l)
        {
            switch (l)
            {
            case log::Level::Trace:
                return "LOG_TRACE";
            case log::Level::Debug:
                return "LOG_DEBUG";
            case log::Level::Info:
                return "LOG_INFO";
            case log::Level::Succ:
                return "LOG_INFO";
            case log::Level::Warn:
                return "LOG_WARN";
            case log::Level::Error:
                return "LOG_ERROR";
            }
            return "LOG_INFO";
        }
    } // namespace

    std::string Settings::default_path()
    {
        std::optional<std::string> dir = fs::exe_dir();
        if (!dir) return "Settings.ini";
        return fs::join(*dir, "Settings.ini");
    }

    Result<Settings> Settings::load(const std::string& path)
    {
        Settings s;
        s.relay.token_ttl_seconds = 3600;

        Result<Ini> ini = Ini::load(path);
        if (!ini) return std::unexpected(Error::NotFound);

        /* [Connection] */
        s.relay.namespace_host = get_or(*ini, "Connection", "Namespace", "");
        s.relay.hybrid_connection = get_or(*ini, "Connection", "HybridConnection", "");
        s.relay.key_name = get_or(*ini, "Connection", "AccessKeyName", "");
        s.relay.key = get_or(*ini, "Connection", "AccessKey", "");

        /* [Logging] */
        s.logging.level = log::level_from_str(get_or(*ini, "Logging", "LogLevel", "LOG_INFO"));
        s.logging.max_rotate_files = ini->get_int("Logging", "MaxRotateFiles", 10);
        s.logging.max_file_size_mb = ini->get_int("Logging", "MaxFileSizeInMB", 10);
        s.logging.show_event_type = ini->get_bool("Logging", "ShowEventType", true);
        s.logging.time_precision = ini->get_bool("Logging", "TimePrecission", true);
        s.logging.enabled = ini->get_bool("Logging", "Enabled", true);

        /* [Adapters] */
        s.adapters_dir = get_or(*ini, "Adapters", "Directory", ".");

        return s;
    }

    Status Settings::save(const std::string& path) const
    {
        Ini ini;

        ini.set("Logging", "LogLevel", level_name(logging.level));
        ini.set_int("Logging", "MaxRotateFiles", logging.max_rotate_files);
        ini.set_int("Logging", "MaxFileSizeInMB", logging.max_file_size_mb);
        ini.set_int("Logging", "ShowEventType", logging.show_event_type ? 1 : 0);
        ini.set_int("Logging", "TimePrecission", logging.time_precision ? 1 : 0);
        ini.set_int("Logging", "Enabled", logging.enabled ? 1 : 0);

        ini.set("Connection", "AccessKey", relay.key);
        ini.set("Connection", "AccessKeyName", relay.key_name);
        ini.set("Connection", "Namespace", relay.namespace_host);
        ini.set("Connection", "HybridConnection", relay.hybrid_connection);

        ini.set("Adapters", "Directory", adapters_dir);

        return ini.save(path);
    }
} // namespace vc
