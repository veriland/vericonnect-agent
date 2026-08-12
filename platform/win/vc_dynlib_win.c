#include "vc/vc_adapter.h"
#include <windows.h>

/* UTF-8 path -> UTF-16 for LoadLibraryW. */
void *vc_dynlib_open(const char *path)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *wpath = vc_alloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    HMODULE h = LoadLibraryW(wpath);
    vc_free(wpath);
    return h;
}

void *vc_dynlib_sym(void *handle, const char *name)
{
    return (void *)GetProcAddress((HMODULE)handle, name);
}

void vc_dynlib_close(void *handle)
{
    if (handle) FreeLibrary((HMODULE)handle);
}
