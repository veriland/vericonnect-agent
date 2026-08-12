/*
 * vc_fs.h - portable filesystem operations (UTF-8 paths).
 * Implemented per platform.
 */
#ifndef VC_FS_H
#define VC_FS_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  define VC_PATH_SEP '\\'
#else
#  define VC_PATH_SEP '/'
#endif

bool vc_fs_file_exists(const char *path);
bool vc_fs_dir_exists(const char *path);
int  vc_fs_mkdir(const char *path);                 /* single level; OK if exists */
int  vc_fs_remove_file(const char *path);
int  vc_fs_move(const char *from, const char *to);  /* fails if to exists */

/* Read entire file. *out is vc_alloc'd (NUL terminated for convenience). */
int vc_fs_read_all(const char *path, uint8_t **out, size_t *out_len);

/* Write entire file (creates/truncates). */
int vc_fs_write_all(const char *path, const void *data, size_t len);

/* List names of regular files (not directories) in dir.
 * Returns vc_alloc'd array of vc_alloc'd strings; caller frees with
 * vc_fs_list_free. count set to number of entries. */
int  vc_fs_list_files(const char *dir, char ***names, size_t *count);
void vc_fs_list_free(char **names, size_t count);

/* Join two path segments into a malloc'd string (vc_free). */
char *vc_fs_join(const char *a, const char *b);

/* Directory containing the current executable (vc_free). */
char *vc_fs_exe_dir(void);

#ifdef __cplusplus
}
#endif

#endif
