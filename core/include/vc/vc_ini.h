/* vc_ini.h - INI file reader/writer (Settings.ini). */
#ifndef VC_INI_H
#define VC_INI_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_ini vc_ini;

vc_ini *vc_ini_new(void);
vc_ini *vc_ini_load(const char *path);   /* NULL if file unreadable */
int     vc_ini_save(const vc_ini *ini, const char *path);
void    vc_ini_free(vc_ini *ini);

const char *vc_ini_get(const vc_ini *ini, const char *section,
                       const char *key, const char *def);
int         vc_ini_get_int(const vc_ini *ini, const char *section,
                           const char *key, int def);
bool        vc_ini_get_bool(const vc_ini *ini, const char *section,
                            const char *key, bool def);
int         vc_ini_set(vc_ini *ini, const char *section,
                       const char *key, const char *value);
int         vc_ini_set_int(vc_ini *ini, const char *section,
                           const char *key, int value);

#ifdef __cplusplus
}
#endif

#endif
