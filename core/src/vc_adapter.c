#include "vc/vc_adapter.h"
#include "vc/vc_fs.h"
#include "vc/vc_json.h"
#include "vc/vc_str.h"
#include "vc/vc_log.h"
#include <stdio.h>

#if defined(_WIN32)
#  define VC_ADAPTER_PREFIX "vc-adapter-"
#  define VC_ADAPTER_EXT    ".dll"
#elif defined(__APPLE__)
#  define VC_ADAPTER_PREFIX "libvc-adapter-"
#  define VC_ADAPTER_EXT    ".dylib"
#else
#  define VC_ADAPTER_PREFIX "libvc-adapter-"
#  define VC_ADAPTER_EXT    ".so"
#endif

static bool ends_with_ci(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && vc_stricmp(s + ls - lf, suffix) == 0;
}

static void extract_id(vc_adapter *ad)
{
    ad->id[0] = 0;
    if (!ad->info) return;
    const char *info = ad->info();
    if (!info) return;
    vc_json *root = vc_json_parse(info);
    if (root) {
        const char *id = vc_json_get_str(root, "id", NULL);
        if (id) snprintf(ad->id, sizeof ad->id, "%s", id);
        vc_json_free(root);
    }
}

static int load_one(vc_adapter_registry *reg, const char *path)
{
    void *handle = vc_dynlib_open(path);
    if (!handle) return VC_E_IO;

    vc_adapter_run_fn  run = (vc_adapter_run_fn)vc_dynlib_sym(handle, "RunAdapterCommand");
    vc_adapter_free_fn fr  = (vc_adapter_free_fn)vc_dynlib_sym(handle, "FreeAdapterString");
    vc_adapter_info_fn nf  = (vc_adapter_info_fn)vc_dynlib_sym(handle, "GetAdapterInfo");
    if (!run || !fr) {
        vc_dynlib_close(handle);
        return VC_E_PROTOCOL;
    }

    vc_adapter *ad = vc_alloc(sizeof *ad);
    if (!ad) { vc_dynlib_close(handle); return VC_E_NOMEM; }
    memset(ad, 0, sizeof *ad);
    ad->handle = handle;
    ad->run = run;
    ad->free_str = fr;
    ad->info = nf;
    snprintf(ad->path, sizeof ad->path, "%s", path);
    extract_id(ad);
    if (!ad->id[0])
        snprintf(ad->id, sizeof ad->id, "adapter%p", (void *)ad);

    ad->next = reg->first;
    reg->first = ad;
    VC_INFO("Loaded adapter '%s' from %s", ad->id, path);
    return VC_OK;
}

int vc_adapter_registry_load(vc_adapter_registry *reg, const char *dir)
{
    reg->first = NULL;
    char **names = NULL;
    size_t count = 0;
    if (vc_fs_list_files(dir, &names, &count) != VC_OK)
        return VC_E_NOT_FOUND;

    int loaded = 0;
    for (size_t i = 0; i < count; i++) {
        const char *n = names[i];
        if (!ends_with_ci(n, VC_ADAPTER_EXT)) continue;
        /* accept any shared library exposing the adapter exports,
         * regardless of its name prefix */
        char *full = vc_fs_join(dir, n);
        if (full) {
            if (load_one(reg, full) == VC_OK) loaded++;
            vc_free(full);
        }
    }
    vc_fs_list_free(names, count);
    return loaded > 0 ? VC_OK : VC_E_NOT_FOUND;
}

void vc_adapter_registry_unload(vc_adapter_registry *reg)
{
    vc_adapter *ad = reg->first;
    while (ad) {
        vc_adapter *next = ad->next;
        if (ad->handle) vc_dynlib_close(ad->handle);
        vc_free(ad);
        ad = next;
    }
    reg->first = NULL;
}

vc_adapter *vc_adapter_find(vc_adapter_registry *reg, const char *id)
{
    if (!id) return NULL;
    for (vc_adapter *ad = reg->first; ad; ad = ad->next)
        if (!vc_stricmp(ad->id, id)) return ad;
    return NULL;
}

static char *json_error(int code, const char *desc)
{
    vc_json *root = vc_json_new_object();
    vc_json_obj_set_num(root, "StatusCode", code);
    vc_json_obj_set_str(root, "StatusDescription", desc);
    char *s = vc_json_write(root);
    vc_json_free(root);
    return s ? s : vc_strdup("{\"StatusCode\":500}");
}

char *vc_adapter_dispatch(vc_adapter_registry *reg, const char *request_json)
{
    if (!request_json)
        return json_error(400, "Empty request");

    /* Peek at the "Adapter" field to route. */
    vc_adapter *ad = NULL;
    vc_json *root = vc_json_parse(request_json);
    if (root) {
        const char *adapter_id = vc_json_get_str(root, "Adapter", NULL);
        if (adapter_id) ad = vc_adapter_find(reg, adapter_id);
        vc_json_free(root);
    }
    if (!ad) ad = reg->first;   /* fall back to first loaded adapter */
    if (!ad)
        return json_error(404, "No adapter available");

    char *raw = ad->run(request_json);
    if (!raw)
        return json_error(500, "Adapter returned no result");

    /* Copy into a vc_alloc buffer so the host frees it uniformly, then
     * release the adapter-owned buffer with its own allocator. */
    char *copy = vc_strdup(raw);
    ad->free_str(raw);
    return copy ? copy : json_error(500, "Out of memory");
}
