/*
 * FileSystem adapter commands, including ReadFile's "move the file
 * into a COPY subfolder after reading" semantics.
 */
#include "fs_commands.h"
#include "vc/vc_fs.h"
#include "vc/vc_str.h"
#include "vc/vc_base64.h"
#include <stdio.h>

/* ---------------------------------------------------------------- */
/* helpers                                                           */
/* ---------------------------------------------------------------- */

static const vc_json *params_of(const vc_json *req)
{
    return vc_json_obj_get_ci(req, "Parameters");
}

static char *result_json(int code, const char *desc, vc_json *data /*adopted*/)
{
    vc_json *o = vc_json_new_object();
    vc_json_obj_set_num(o, "StatusCode", code);
    vc_json_obj_set_str(o, "StatusDescription", desc);
    if (data)
        vc_json_obj_set(o, "Data", data);
    char *s = vc_json_write(o);
    vc_json_free(o);
    return s ? s : vc_strdup("{\"StatusCode\":500}");
}

static char *simple_result(int code, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    return result_json(code, msg, NULL);
}

/* Resolves TargetFolder + FileName; returns NULL if either missing. */
static char *target_path(const vc_json *p)
{
    const char *folder = vc_json_get_str(p, "TargetFolder", NULL);
    const char *name   = vc_json_get_str(p, "FileName", NULL);
    if (!folder || !name || !*name) return NULL;
    return vc_fs_join(folder, name);
}

/* ---------------------------------------------------------------- */
/* ListFolder                                                        */
/* ---------------------------------------------------------------- */

char *fs_cmd_list_folder(const vc_json *req)
{
    const vc_json *p = params_of(req);
    const char *folder = vc_json_get_str(p, "TargetFolder", NULL);
    if (!folder || !*folder)
        return simple_result(400, "Missing parameters, please check your parameters");

    if (!vc_fs_dir_exists(folder))
        return simple_result(404, "Folder %s not found", folder);

    char **names = NULL;
    size_t count = 0;
    if (vc_fs_list_files(folder, &names, &count) != VC_OK)
        return simple_result(404, "No files found in the folder %s", folder);

    if (count == 0) {
        vc_fs_list_free(names, count);
        return simple_result(404, "No files found in the folder %s", folder);
    }

    vc_json *files = vc_json_new_array();
    for (size_t i = 0; i < count; i++)
        vc_json_arr_add(files, vc_json_new_str(names[i]));
    vc_fs_list_free(names, count);

    vc_json *data = vc_json_new_object();
    vc_json_obj_set(data, "files", files);
    vc_json_obj_set_num(data, "FileCount", (double)count);

    char msg[1024];
    snprintf(msg, sizeof msg, "There are %zu files under the %s folder",
             count, folder);
    return result_json(200, msg, data);
}

/* ---------------------------------------------------------------- */
/* CreateFolder                                                      */
/* ---------------------------------------------------------------- */

char *fs_cmd_create_folder(const vc_json *req)
{
    const vc_json *p = params_of(req);
    const char *folder = vc_json_get_str(p, "TargetFolder", NULL);
    if (!folder || !*folder)
        return simple_result(400, "Missing parameters, please check your parameters");

    if (vc_fs_dir_exists(folder))
        return simple_result(200, "Folder %s already exists", folder);

    /* create intermediate levels */
    char *work = vc_strdup(folder);
    if (!work) return simple_result(500, "Out of memory");
    for (char *q = work; *q; q++) {
        if ((*q == '\\' || *q == '/') && q != work) {
            char saved = *q;
            *q = 0;
            /* skip drive roots like "C:" and UNC prefixes */
            if (strlen(work) > 2 || (work[1] != ':' && work[0] != '\\'))
                vc_fs_mkdir(work);
            *q = saved;
        }
    }
    int rc = vc_fs_mkdir(work);
    vc_free(work);

    if (rc != VC_OK)
        return simple_result(400, "Error creating folder: %s", folder);
    return simple_result(200, "Folder %s created successfully", folder);
}

/* ---------------------------------------------------------------- */
/* CreateFile                                                        */
/* ---------------------------------------------------------------- */

char *fs_cmd_create_file(const vc_json *req)
{
    const vc_json *p = params_of(req);
    char *path = target_path(p);
    if (!path)
        return simple_result(400, "Missing parameters, please check your parameters");

    bool overwrite = vc_json_get_bool(p, "OverwriteIfExists", false);
    if (vc_fs_file_exists(path) && !overwrite) {
        char *r = simple_result(409,
            "File already exist and overwrite if exists flag is disabled");
        vc_free(path);
        return r;
    }

    const char *content  = vc_json_get_str(p, "FileContent", "");
    const char *encoding = vc_json_get_str(p, "Encoding", "utf-8");

    /* base64 content type lets callers push binary payloads */
    const char *ctype = vc_json_get_str(p, "FileContentType", NULL);
    char *result;
    if (ctype && !vc_stricmp(ctype, "base64")) {
        size_t blen = 0;
        uint8_t *bytes = vc_base64_decode(content, &blen);
        if (!bytes) {
            result = simple_result(400, "FileContent is not valid base64");
        } else {
            int rc = vc_fs_write_all(path, bytes, blen);
            vc_free(bytes);
            result = (rc == VC_OK)
                ? simple_result(200, "Data written successfully to file: %s", path)
                : simple_result(400, "Error writing to file: %s", path);
        }
    } else {
        /* The JSON string is already UTF-8; write the raw UTF-8 bytes
         * without a BOM (BACS files rely on BOM-less output). */
        (void)encoding;
        int rc = vc_fs_write_all(path, content, strlen(content));
        result = (rc == VC_OK)
            ? simple_result(200, "Data written successfully to file: %s", path)
            : simple_result(400, "Error writing to file: %s", path);
    }
    vc_free(path);
    return result;
}

/* ---------------------------------------------------------------- */
/* ReadFile                                                          */
/* ---------------------------------------------------------------- */

char *fs_cmd_read_file(const vc_json *req)
{
    const vc_json *p = params_of(req);
    const char *folder = vc_json_get_str(p, "TargetFolder", NULL);
    char *path = target_path(p);
    if (!path)
        return simple_result(400, "Invalid parameters.");

    if (!vc_fs_file_exists(path)) {
        char *r = simple_result(404, "File %s not found.", path);
        vc_free(path);
        return r;
    }

    uint8_t *bytes = NULL;
    size_t len = 0;
    if (vc_fs_read_all(path, &bytes, &len) != VC_OK) {
        char *r = simple_result(500, "Error reading file: %s", path);
        vc_free(path);
        return r;
    }

    char *b64 = vc_base64_encode(bytes, len);
    vc_free(bytes);
    if (!b64) {
        vc_free(path);
        return simple_result(500, "Out of memory encoding file content");
    }

    /* Post-read archive: move the file into <TargetFolder>/COPY so a
     * polled folder drains. */
    bool overwrite = vc_json_get_bool(p, "OverwriteIfExists", false);
    const char *name = vc_json_get_str(p, "FileName", "");
    char *copy_dir = vc_fs_join(folder, "COPY");
    char *dest = copy_dir ? vc_fs_join(copy_dir, name) : NULL;

    int status = 200;
    char desc[1024];
    snprintf(desc, sizeof desc, "File %s read successfully", path);

    if (copy_dir && dest) {
        if (!vc_fs_dir_exists(copy_dir))
            vc_fs_mkdir(copy_dir);
        if (vc_fs_file_exists(dest)) {
            if (overwrite) {
                vc_fs_remove_file(dest);
                vc_fs_move(path, dest);
            } else {
                status = 409;
                snprintf(desc, sizeof desc,
                         "File %s already exists and overwrite is not allowed.",
                         dest);
            }
        } else {
            vc_fs_move(path, dest);
        }
    }
    vc_free(copy_dir);
    vc_free(dest);
    vc_free(path);

    /* Data carries the base64 payload. */
    vc_json *o = vc_json_new_object();
    vc_json_obj_set_num(o, "StatusCode", status);
    vc_json_obj_set_str(o, "StatusDescription", desc);
    vc_json_obj_set_str(o, "Data", b64);
    vc_json_obj_set_num(o, "FileSize", (double)len);
    char *s = vc_json_write(o);
    vc_json_free(o);
    vc_free(b64);
    return s ? s : vc_strdup("{\"StatusCode\":500}");
}

/* ---------------------------------------------------------------- */
/* DeleteFile                                                        */
/* ---------------------------------------------------------------- */

char *fs_cmd_delete_file(const vc_json *req)
{
    const vc_json *p = params_of(req);
    char *path = target_path(p);
    if (!path)
        return simple_result(400, "Missing parameters, please check your parameters");

    char *result;
    if (!vc_fs_file_exists(path)) {
        result = simple_result(404, "File %s not found.", path);
    } else if (vc_fs_remove_file(path) == VC_OK) {
        result = simple_result(200, "File %s deleted successfully", path);
    } else {
        result = simple_result(400, "Error deleting file: %s", path);
    }
    vc_free(path);
    return result;
}

/* ---------------------------------------------------------------- */
/* MoveFile                                                          */
/* ---------------------------------------------------------------- */

char *fs_cmd_move_file(const vc_json *req)
{
    const vc_json *p = params_of(req);
    char *src = target_path(p);
    const char *dest_folder = vc_json_get_str(p, "DestinationFolder", NULL);
    const char *dest_name   = vc_json_get_str(p, "DestinationFileName",
                              vc_json_get_str(p, "FileName", NULL));
    if (!src || !dest_folder || !dest_name) {
        vc_free(src);
        return simple_result(400,
            "Missing parameters: TargetFolder, FileName and DestinationFolder are required");
    }

    char *dest = vc_fs_join(dest_folder, dest_name);
    if (!dest) { vc_free(src); return simple_result(500, "Out of memory"); }

    char *result;
    bool overwrite = vc_json_get_bool(p, "OverwriteIfExists", false);
    if (!vc_fs_file_exists(src)) {
        result = simple_result(404, "File %s not found.", src);
    } else if (vc_fs_file_exists(dest) && !overwrite) {
        result = simple_result(409,
            "File %s already exists and overwrite is not allowed.", dest);
    } else {
        if (!vc_fs_dir_exists(dest_folder))
            vc_fs_mkdir(dest_folder);
        if (vc_fs_file_exists(dest))
            vc_fs_remove_file(dest);
        result = (vc_fs_move(src, dest) == VC_OK)
            ? simple_result(200, "File moved from %s to %s", src, dest)
            : simple_result(400, "Error moving file %s to %s", src, dest);
    }
    vc_free(src);
    vc_free(dest);
    return result;
}
