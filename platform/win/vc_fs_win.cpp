/* Win32 filesystem ops with UTF-8 <-> UTF-16 path conversion. */
#include "vc/vc_fs.h"
#include "vc/vc_str.h"
#include <windows.h>
#include <stdio.h>

static wchar_t *utf8_to_wide(const char *s)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *w = static_cast<wchar_t*>(vc_alloc((size_t)wlen * sizeof(wchar_t)));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wlen);
    return w;
}

static char *wide_to_utf8(const wchar_t *w)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *s = static_cast<char*>(vc_alloc((size_t)len));
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, len, NULL, NULL);
    return s;
}

bool vc_fs_file_exists(const char *path)
{
    wchar_t *w = utf8_to_wide(path);
    if (!w) return false;
    DWORD a = GetFileAttributesW(w);
    vc_free(w);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool vc_fs_dir_exists(const char *path)
{
    wchar_t *w = utf8_to_wide(path);
    if (!w) return false;
    DWORD a = GetFileAttributesW(w);
    vc_free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int vc_fs_mkdir(const char *path)
{
    wchar_t *w = utf8_to_wide(path);
    if (!w) return VC_E_NOMEM;
    BOOL ok = CreateDirectoryW(w, NULL);
    DWORD err = GetLastError();
    vc_free(w);
    if (ok || err == ERROR_ALREADY_EXISTS) return VC_OK;
    return VC_E_IO;
}

int vc_fs_remove_file(const char *path)
{
    wchar_t *w = utf8_to_wide(path);
    if (!w) return VC_E_NOMEM;
    BOOL ok = DeleteFileW(w);
    vc_free(w);
    return ok ? VC_OK : VC_E_IO;
}

int vc_fs_move(const char *from, const char *to)
{
    wchar_t *wf = utf8_to_wide(from);
    wchar_t *wt = utf8_to_wide(to);
    int rc = VC_E_NOMEM;
    if (wf && wt)
        rc = MoveFileExW(wf, wt, 0) ? VC_OK : VC_E_IO;
    vc_free(wf); vc_free(wt);
    return rc;
}

int vc_fs_read_all(const char *path, uint8_t **out, size_t *out_len)
{
    *out = NULL; *out_len = 0;
    wchar_t *w = utf8_to_wide(path);
    if (!w) return VC_E_NOMEM;
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    vc_free(w);
    if (h == INVALID_HANDLE_VALUE) return VC_E_NOT_FOUND;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return VC_E_IO; }
    size_t total = (size_t)sz.QuadPart;
    uint8_t *buf = static_cast<uint8_t*>(vc_alloc(total + 1));
    if (!buf) { CloseHandle(h); return VC_E_NOMEM; }

    size_t off = 0;
    while (off < total) {
        DWORD want = (DWORD)((total - off) > 0x10000000 ? 0x10000000 : (total - off));
        DWORD got = 0;
        if (!ReadFile(h, buf + off, want, &got, NULL) || got == 0) break;
        off += got;
    }
    CloseHandle(h);
    buf[off] = 0;
    *out = buf;
    *out_len = off;
    return VC_OK;
}

int vc_fs_write_all(const char *path, const void *data, size_t len)
{
    wchar_t *w = utf8_to_wide(path);
    if (!w) return VC_E_NOMEM;
    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    vc_free(w);
    if (h == INVALID_HANDLE_VALUE) return VC_E_IO;

    const uint8_t *p = static_cast<const uint8_t*>(data);
    size_t off = 0;
    int rc = VC_OK;
    while (off < len) {
        DWORD want = (DWORD)((len - off) > 0x10000000 ? 0x10000000 : (len - off));
        DWORD wrote = 0;
        if (!WriteFile(h, p + off, want, &wrote, NULL)) { rc = VC_E_IO; break; }
        off += wrote;
    }
    CloseHandle(h);
    return rc;
}

int vc_fs_list_files(const char *dir, char ***names, size_t *count)
{
    *names = NULL; *count = 0;

    char *pattern = vc_fs_join(dir, "*");
    if (!pattern) return VC_E_NOMEM;
    wchar_t *w = utf8_to_wide(pattern);
    vc_free(pattern);
    if (!w) return VC_E_NOMEM;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    vc_free(w);
    if (h == INVALID_HANDLE_VALUE) return VC_E_NOT_FOUND;

    size_t cap = 16, n = 0;
    char **arr = static_cast<char**>(vc_alloc(cap * sizeof(char *)));
    if (!arr) { FindClose(h); return VC_E_NOMEM; }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char *name = wide_to_utf8(fd.cFileName);
        if (!name) continue;
        if (n == cap) {
            cap *= 2;
            char **na = static_cast<char**>(vc_realloc(arr, cap * sizeof(char *)));
            if (!na) { vc_free(name); break; }
            arr = na;
        }
        arr[n++] = name;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    *names = arr;
    *count = n;
    return VC_OK;
}

void vc_fs_list_free(char **names, size_t count)
{
    if (!names) return;
    for (size_t i = 0; i < count; i++) vc_free(names[i]);
    vc_free(names);
}

char *vc_fs_join(const char *a, const char *b)
{
    if (!a || !*a) return vc_strdup(b ? b : "");
    if (!b || !*b) return vc_strdup(a);
    size_t la = strlen(a);
    bool has_sep = a[la - 1] == '\\' || a[la - 1] == '/';
    vc_buf buf;
    vc_buf_init(&buf);
    vc_buf_append_str(&buf, a);
    if (!has_sep) vc_buf_append_char(&buf, '\\');
    /* skip leading separators on b */
    while (*b == '\\' || *b == '/') b++;
    vc_buf_append_str(&buf, b);
    return vc_buf_take(&buf);
}

char *vc_fs_exe_dir(void)
{
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)(sizeof path / sizeof path[0]));
    if (n == 0) return NULL;
    /* strip filename */
    for (DWORD i = n; i > 0; i--) {
        if (path[i - 1] == '\\' || path[i - 1] == '/') {
            path[i - 1] = 0;
            break;
        }
    }
    return wide_to_utf8(path);
}
