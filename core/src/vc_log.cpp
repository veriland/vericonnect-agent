#include "vc/vc_log.h"
#include "vc/vc_str.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <sys/timeb.h>
#else
#  include <sys/time.h>
#endif

static vc_log_config g_cfg;
static FILE *g_file = NULL;
static bool g_init = false;

static const char *level_tag(vc_log_level l)
{
    switch (l) {
    case VC_LOG_TRACE: return "TRACE";
    case VC_LOG_DEBUG: return "DEBUG";
    case VC_LOG_INFO:  return "INFO ";
    case VC_LOG_SUCC:  return "SUCC ";
    case VC_LOG_WARN:  return "WARN ";
    case VC_LOG_ERROR: return "ERROR";
    default:           return "?????";
    }
}

void vc_log_config_defaults(vc_log_config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->level = VC_LOG_INFO;
    cfg->enabled = true;
    cfg->console = true;
    cfg->show_event_type = true;
    cfg->time_precision = true;
    cfg->max_file_size_mb = 10;
    cfg->max_rotate_files = 10;
}

vc_log_level vc_log_level_from_str(const char *s)
{
    if (!s) return VC_LOG_INFO;
    if (!vc_stricmp(s, "LOG_TRACE") || !vc_stricmp(s, "TRACE")) return VC_LOG_TRACE;
    if (!vc_stricmp(s, "LOG_DEBUG") || !vc_stricmp(s, "DEBUG")) return VC_LOG_DEBUG;
    if (!vc_stricmp(s, "LOG_INFO")  || !vc_stricmp(s, "INFO"))  return VC_LOG_INFO;
    if (!vc_stricmp(s, "LOG_WARN")  || !vc_stricmp(s, "WARN"))  return VC_LOG_WARN;
    if (!vc_stricmp(s, "LOG_ERROR") || !vc_stricmp(s, "ERROR")) return VC_LOG_ERROR;
    return VC_LOG_INFO;
}

int vc_log_init(const vc_log_config *cfg)
{
    vc_log_shutdown();
    g_cfg = *cfg;
    if (g_cfg.enabled && g_cfg.file_path[0]) {
        g_file = fopen(g_cfg.file_path, "ab");
        if (!g_file) return VC_E_IO;
    }
    g_init = true;
    return VC_OK;
}

void vc_log_shutdown(void)
{
    if (g_file) { fclose(g_file); g_file = NULL; }
    g_init = false;
}

static void rotate_if_needed(void)
{
    if (!g_file || g_cfg.max_file_size_mb <= 0) return;
    long pos = ftell(g_file);
    if (pos < (long)g_cfg.max_file_size_mb * 1024 * 1024) return;

    fclose(g_file);
    g_file = NULL;

    /* shift file.(n-1) -> file.n ... file -> file.1 */
    char from[600], to[600];
    int n = g_cfg.max_rotate_files > 0 ? g_cfg.max_rotate_files : 5;
    snprintf(to, sizeof to, "%s.%d", g_cfg.file_path, n);
    remove(to);
    for (int i = n - 1; i >= 1; i--) {
        snprintf(from, sizeof from, "%s.%d", g_cfg.file_path, i);
        snprintf(to, sizeof to, "%s.%d", g_cfg.file_path, i + 1);
        rename(from, to);
    }
    snprintf(to, sizeof to, "%s.1", g_cfg.file_path);
    rename(g_cfg.file_path, to);

    g_file = fopen(g_cfg.file_path, "ab");
}

static void timestamp(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
    struct _timeb tb;
    _ftime_s(&tb);
    int ms = tb.millitm;
#else
    localtime_r(&t, &tmv);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int ms = (int)(tv.tv_usec / 1000);
#endif
    if (g_cfg.time_precision)
        snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
    else
        snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

void vc_log(vc_log_level lvl, const char *fmt, ...)
{
    if (!g_init) {
        /* not initialised: still echo to stderr so nothing is lost */
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        va_end(ap);
        return;
    }
    if (!g_cfg.enabled) return;
    /* SUCC logs at INFO priority */
    vc_log_level eff = (lvl == VC_LOG_SUCC) ? VC_LOG_INFO : lvl;
    if (eff < g_cfg.level) return;

    char ts[40];
    timestamp(ts, sizeof ts);

    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    if (g_cfg.console) {
        if (g_cfg.show_event_type)
            printf("%s [%s] %s\n", ts, level_tag(lvl), msg);
        else
            printf("%s %s\n", ts, msg);
        fflush(stdout);
    }
    if (g_file) {
        if (g_cfg.show_event_type)
            fprintf(g_file, "%s [%s] %s\n", ts, level_tag(lvl), msg);
        else
            fprintf(g_file, "%s %s\n", ts, msg);
        fflush(g_file);
        rotate_if_needed();
    }
}
