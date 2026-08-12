/* POSIX filesystem ops (Linux/macOS). Paths are UTF-8 already. */
#include "vc/vc_fs.h"
#include "vc/vc_str.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

bool vc_fs_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool vc_fs_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int vc_fs_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0) return VC_OK;
    if (vc_fs_dir_exists(path)) return VC_OK;
    return VC_E_IO;
}

int vc_fs_remove_file(const char *path)
{
    return unlink(path) == 0 ? VC_OK : VC_E_IO;
}

int vc_fs_move(const char *from, const char *to)
{
    if (vc_fs_file_exists(to) || vc_fs_dir_exists(to)) return VC_E_EXISTS;
    return rename(from, to) == 0 ? VC_OK : VC_E_IO;
}

int vc_fs_read_all(const char *path, uint8_t **out, size_t *out_len)
{
    *out = NULL; *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return VC_E_NOT_FOUND;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return VC_E_IO; }
    uint8_t *buf = vc_alloc((size_t)sz + 1);
    if (!buf) { fclose(f); return VC_E_NOMEM; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    *out = buf;
    *out_len = got;
    return VC_OK;
}

int vc_fs_write_all(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return VC_E_IO;
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    return wrote == len ? VC_OK : VC_E_IO;
}

int vc_fs_list_files(const char *dir, char ***names, size_t *count)
{
    *names = NULL; *count = 0;
    DIR *d = opendir(dir);
    if (!d) return VC_E_NOT_FOUND;
    size_t cap = 16, n = 0;
    char **arr = vc_alloc(cap * sizeof(char *));
    if (!arr) { closedir(d); return VC_E_NOMEM; }
    struct dirent *e;
    while ((e = readdir(d))) {
        char *full = vc_fs_join(dir, e->d_name);
        if (!full) continue;
        bool isfile = vc_fs_file_exists(full);
        vc_free(full);
        if (!isfile) continue;
        if (n == cap) {
            cap *= 2;
            char **na = vc_realloc(arr, cap * sizeof(char *));
            if (!na) break;
            arr = na;
        }
        arr[n++] = vc_strdup(e->d_name);
    }
    closedir(d);
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
    bool has_sep = a[la - 1] == '/';
    vc_buf buf;
    vc_buf_init(&buf);
    vc_buf_append_str(&buf, a);
    if (!has_sep) vc_buf_append_char(&buf, '/');
    while (*b == '/') b++;
    vc_buf_append_str(&buf, b);
    return vc_buf_take(&buf);
}

char *vc_fs_exe_dir(void)
{
    char path[4096];
#if defined(__APPLE__)
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0) return NULL;
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return NULL;
    path[n] = 0;
#endif
    for (size_t i = strlen(path); i > 0; i--)
        if (path[i - 1] == '/') { path[i - 1] = 0; break; }
    return vc_strdup(path);
}
