/*
 * vc_log.h - logger with console + rotating file output.
 * Configured by the [Logging] section of Settings.ini.
 */
#ifndef VC_LOG_H
#define VC_LOG_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vc_log_level {
    VC_LOG_TRACE = 0,
    VC_LOG_DEBUG,
    VC_LOG_INFO,
    VC_LOG_SUCC,   /* success events, logged at INFO priority */
    VC_LOG_WARN,
    VC_LOG_ERROR
} vc_log_level;

typedef struct vc_log_config {
    vc_log_level level;
    bool         enabled;
    bool         console;           /* echo to stdout                 */
    bool         show_event_type;   /* [INFO] tags                    */
    bool         time_precision;    /* milliseconds in timestamps     */
    char         file_path[512];    /* empty = no file logging        */
    int          max_file_size_mb;  /* rotate threshold               */
    int          max_rotate_files;
} vc_log_config;

void vc_log_config_defaults(vc_log_config *cfg);
int  vc_log_init(const vc_log_config *cfg);
void vc_log_shutdown(void);

vc_log_level vc_log_level_from_str(const char *s); /* "LOG_DEBUG" etc. */

void vc_log(vc_log_level lvl, const char *fmt, ...);

#define VC_TRACE(...) vc_log(VC_LOG_TRACE, __VA_ARGS__)
#define VC_DEBUG(...) vc_log(VC_LOG_DEBUG, __VA_ARGS__)
#define VC_INFO(...)  vc_log(VC_LOG_INFO,  __VA_ARGS__)
#define VC_SUCC(...)  vc_log(VC_LOG_SUCC,  __VA_ARGS__)
#define VC_WARN(...)  vc_log(VC_LOG_WARN,  __VA_ARGS__)
#define VC_ERROR(...) vc_log(VC_LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
