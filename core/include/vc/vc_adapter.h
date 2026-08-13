/*
 * vc_adapter.h - adapter ABI + host-side loader.
 *
 * ABI (C, UTF-8, cdecl) exported by every adapter shared library:
 *
 *   char*       RunAdapterCommand(const char* request_json);
 *   void        FreeAdapterString(char* p);
 *   const char* GetAdapterInfo(void);   // static string, not freed
 *
 * RunAdapterCommand receives the full command JSON and returns a
 * malloc'd JSON result ({"StatusCode":...,"StatusDescription":...}).
 * The host frees it with FreeAdapterString. All strings crossing the
 * boundary are UTF-8.
 */
#ifndef VC_ADAPTER_H
#define VC_ADAPTER_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  define VC_ADAPTER_EXPORT_LINKAGE __declspec(dllexport)
#else
#  define VC_ADAPTER_EXPORT_LINKAGE __attribute__((visibility("default")))
#endif

/* Adapter entry points must keep C linkage so the host resolves them by
 * their plain names (RunAdapterCommand, ...). When an adapter is compiled
 * as C++, name mangling would otherwise hide them from vc_dynlib_sym. */
#ifdef __cplusplus
#  define VC_ADAPTER_EXPORT extern "C" VC_ADAPTER_EXPORT_LINKAGE
#else
#  define VC_ADAPTER_EXPORT VC_ADAPTER_EXPORT_LINKAGE
#endif

typedef char*       (*vc_adapter_run_fn)(const char *request_json);
typedef void        (*vc_adapter_free_fn)(char *p);
typedef const char* (*vc_adapter_info_fn)(void);

typedef struct vc_adapter {
    char               id[64];        /* from GetAdapterInfo "id" */
    char               path[512];
    void              *handle;        /* dynlib handle            */
    vc_adapter_run_fn  run;
    vc_adapter_free_fn free_str;
    vc_adapter_info_fn info;
    struct vc_adapter *next;
} vc_adapter;

typedef struct vc_adapter_registry {
    vc_adapter *first;
} vc_adapter_registry;

/* Loads every vc-adapter-*.dll / .so / .dylib found in dir. */
int  vc_adapter_registry_load(vc_adapter_registry *reg, const char *dir);
void vc_adapter_registry_unload(vc_adapter_registry *reg);

/* Find by adapter id (case-insensitive); NULL if not found. */
vc_adapter *vc_adapter_find(vc_adapter_registry *reg, const char *id);

/*
 * Dispatch a command JSON: routes on the "Adapter" field (falls back
 * to the first loaded adapter). Returns malloc'd (vc_alloc) JSON
 * result the caller frees with vc_free; never returns NULL.
 */
char *vc_adapter_dispatch(vc_adapter_registry *reg, const char *request_json);

/* dynlib helpers implemented per platform */
void *vc_dynlib_open(const char *path);
void *vc_dynlib_sym(void *handle, const char *name);
void  vc_dynlib_close(void *handle);

#ifdef __cplusplus
}
#endif

#endif
