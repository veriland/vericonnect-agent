#include "vc/vc_settings.h"
#include "vc/vc_ini.h"
#include "vc/vc_fs.h"
#include "vc/vc_str.h"
#include <stdio.h>

char *vc_settings_default_path(void)
{
    char *dir = vc_fs_exe_dir();
    if (!dir) return vc_strdup("Settings.ini");
    char *path = vc_fs_join(dir, "Settings.ini");
    vc_free(dir);
    return path;
}

static void defaults(vc_settings *out)
{
    memset(out, 0, sizeof *out);
    vc_log_config_defaults(&out->logging);
    out->relay.token_ttl_seconds = 3600;
    snprintf(out->adapters_dir, sizeof out->adapters_dir, ".");
}

int vc_settings_load(const char *path, vc_settings *out)
{
    defaults(out);

    vc_ini *ini = vc_ini_load(path);
    if (!ini) return VC_E_NOT_FOUND;

    /* [Connection] */
    snprintf(out->relay.namespace_host, sizeof out->relay.namespace_host,
             "%s", vc_ini_get(ini, "Connection", "Namespace", ""));
    snprintf(out->relay.hybrid_connection, sizeof out->relay.hybrid_connection,
             "%s", vc_ini_get(ini, "Connection", "HybridConnection", ""));
    snprintf(out->relay.key_name, sizeof out->relay.key_name,
             "%s", vc_ini_get(ini, "Connection", "AccessKeyName", ""));
    snprintf(out->relay.key, sizeof out->relay.key,
             "%s", vc_ini_get(ini, "Connection", "AccessKey", ""));

    /* [Logging] */
    out->logging.level = vc_log_level_from_str(
        vc_ini_get(ini, "Logging", "LogLevel", "LOG_INFO"));
    out->logging.max_rotate_files = vc_ini_get_int(ini, "Logging", "MaxRotateFiles", 10);
    out->logging.max_file_size_mb = vc_ini_get_int(ini, "Logging", "MaxFileSizeInMB", 10);
    out->logging.show_event_type  = vc_ini_get_bool(ini, "Logging", "ShowEventType", true);
    out->logging.time_precision   = vc_ini_get_bool(ini, "Logging", "TimePrecission", true);
    out->logging.enabled          = vc_ini_get_bool(ini, "Logging", "Enabled", true);

    /* [Adapters] */
    snprintf(out->adapters_dir, sizeof out->adapters_dir,
             "%s", vc_ini_get(ini, "Adapters", "Directory", "."));

    vc_ini_free(ini);
    return VC_OK;
}

int vc_settings_save(const char *path, const vc_settings *s)
{
    vc_ini *ini = vc_ini_new();
    if (!ini) return VC_E_NOMEM;

    const char *lvl = "LOG_INFO";
    switch (s->logging.level) {
    case VC_LOG_TRACE: lvl = "LOG_TRACE"; break;
    case VC_LOG_DEBUG: lvl = "LOG_DEBUG"; break;
    case VC_LOG_INFO:  lvl = "LOG_INFO";  break;
    case VC_LOG_SUCC:  lvl = "LOG_INFO";  break;
    case VC_LOG_WARN:  lvl = "LOG_WARN";  break;
    case VC_LOG_ERROR: lvl = "LOG_ERROR"; break;
    }
    vc_ini_set(ini, "Logging", "LogLevel", lvl);
    vc_ini_set_int(ini, "Logging", "MaxRotateFiles", s->logging.max_rotate_files);
    vc_ini_set_int(ini, "Logging", "MaxFileSizeInMB", s->logging.max_file_size_mb);
    vc_ini_set_int(ini, "Logging", "ShowEventType", s->logging.show_event_type ? 1 : 0);
    vc_ini_set_int(ini, "Logging", "TimePrecission", s->logging.time_precision ? 1 : 0);
    vc_ini_set_int(ini, "Logging", "Enabled", s->logging.enabled ? 1 : 0);

    vc_ini_set(ini, "Connection", "AccessKey", s->relay.key);
    vc_ini_set(ini, "Connection", "AccessKeyName", s->relay.key_name);
    vc_ini_set(ini, "Connection", "Namespace", s->relay.namespace_host);
    vc_ini_set(ini, "Connection", "HybridConnection", s->relay.hybrid_connection);

    vc_ini_set(ini, "Adapters", "Directory", s->adapters_dir);

    int rc = vc_ini_save(ini, path);
    vc_ini_free(ini);
    return rc;
}
