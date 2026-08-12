#include "vc/vc_ini.h"
#include "vc/vc_str.h"
#include "vc/vc_fs.h"
#include <stdio.h>
#include <ctype.h>

typedef struct ini_entry {
    char *section;
    char *key;
    char *value;
    struct ini_entry *next;
} ini_entry;

struct vc_ini {
    ini_entry *first;
};

vc_ini *vc_ini_new(void)
{
    vc_ini *ini = vc_alloc(sizeof *ini);
    if (ini) ini->first = NULL;
    return ini;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = 0;
    return s;
}

vc_ini *vc_ini_load(const char *path)
{
    uint8_t *data = NULL;
    size_t len = 0;
    if (vc_fs_read_all(path, &data, &len) != VC_OK)
        return NULL;

    vc_ini *ini = vc_ini_new();
    if (!ini) { vc_free(data); return NULL; }

    char section[128] = "";
    char *text = (char *)data;
    char *line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *s = trim(line);
        if (*s && *s != ';' && *s != '#') {
            if (*s == '[') {
                char *close = strchr(s, ']');
                if (close) {
                    *close = 0;
                    snprintf(section, sizeof section, "%s", trim(s + 1));
                }
            } else {
                char *eq = strchr(s, '=');
                if (eq) {
                    *eq = 0;
                    char *key = trim(s);
                    char *val = trim(eq + 1);
                    if (*key)
                        vc_ini_set(ini, section, key, val);
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    vc_free(data);
    return ini;
}

int vc_ini_save(const vc_ini *ini, const char *path)
{
    vc_buf b;
    vc_buf_init(&b);

    /* group by section, preserving first-seen section order */
    for (ini_entry *sec = ini->first; sec; sec = sec->next) {
        bool seen = false;
        for (ini_entry *prev = ini->first; prev != sec; prev = prev->next)
            if (!vc_stricmp(prev->section, sec->section)) { seen = true; break; }
        if (seen) continue;
        vc_buf_appendf(&b, "[%s]\n", sec->section);
        for (ini_entry *e = ini->first; e; e = e->next)
            if (!vc_stricmp(e->section, sec->section))
                vc_buf_appendf(&b, "%s=%s\n", e->key, e->value);
        vc_buf_append_char(&b, '\n');
    }
    int rc = vc_fs_write_all(path, b.data ? b.data : "", b.len);
    vc_buf_free(&b);
    return rc;
}

void vc_ini_free(vc_ini *ini)
{
    if (!ini) return;
    ini_entry *e = ini->first;
    while (e) {
        ini_entry *next = e->next;
        vc_free(e->section);
        vc_free(e->key);
        vc_free(e->value);
        vc_free(e);
        e = next;
    }
    vc_free(ini);
}

static ini_entry *find(const vc_ini *ini, const char *section, const char *key)
{
    for (ini_entry *e = ini->first; e; e = e->next)
        if (!vc_stricmp(e->section, section) && !vc_stricmp(e->key, key))
            return e;
    return NULL;
}

const char *vc_ini_get(const vc_ini *ini, const char *section,
                       const char *key, const char *def)
{
    if (!ini) return def;
    ini_entry *e = find(ini, section, key);
    return e ? e->value : def;
}

int vc_ini_get_int(const vc_ini *ini, const char *section,
                   const char *key, int def)
{
    const char *v = vc_ini_get(ini, section, key, NULL);
    if (!v || !*v) return def;
    return atoi(v);
}

bool vc_ini_get_bool(const vc_ini *ini, const char *section,
                     const char *key, bool def)
{
    const char *v = vc_ini_get(ini, section, key, NULL);
    if (!v || !*v) return def;
    return !vc_stricmp(v, "1") || !vc_stricmp(v, "true") ||
           !vc_stricmp(v, "yes") || !vc_stricmp(v, "on");
}

int vc_ini_set(vc_ini *ini, const char *section, const char *key,
               const char *value)
{
    ini_entry *e = find(ini, section, key);
    if (e) {
        char *nv = vc_strdup(value ? value : "");
        if (!nv) return VC_E_NOMEM;
        vc_free(e->value);
        e->value = nv;
        return VC_OK;
    }
    e = vc_alloc(sizeof *e);
    if (!e) return VC_E_NOMEM;
    e->section = vc_strdup(section ? section : "");
    e->key     = vc_strdup(key);
    e->value   = vc_strdup(value ? value : "");
    e->next    = NULL;
    if (!e->section || !e->key || !e->value) {
        vc_free(e->section); vc_free(e->key); vc_free(e->value); vc_free(e);
        return VC_E_NOMEM;
    }
    if (!ini->first) {
        ini->first = e;
    } else {
        ini_entry *it = ini->first;
        while (it->next) it = it->next;
        it->next = e;
    }
    return VC_OK;
}

int vc_ini_set_int(vc_ini *ini, const char *section, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", value);
    return vc_ini_set(ini, section, key, buf);
}
