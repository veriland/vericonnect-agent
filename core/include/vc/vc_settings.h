/*
 * vc_settings.h - agent settings (Settings.ini).
 */
#ifndef VC_SETTINGS_H
#define VC_SETTINGS_H

#include "vc_common.h"
#include "vc_log.h"
#include "vc_relay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_settings {
    vc_relay_config relay;          /* [Connection] */
    vc_log_config   logging;        /* [Logging]    */
    char            adapters_dir[512]; /* [Adapters] Directory */
} vc_settings;

/* Loads settings; missing file yields defaults + VC_E_NOT_FOUND. */
int vc_settings_load(const char *path, vc_settings *out);
int vc_settings_save(const char *path, const vc_settings *s);

/* Default path: <exe dir>/Settings.ini (vc_free). */
char *vc_settings_default_path(void);

#ifdef __cplusplus
}
#endif

#endif
