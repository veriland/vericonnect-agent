/*
 * FileSystem adapter - shared library entry points.
 *
 * ABI (UTF-8, C):
 *   char*       RunAdapterCommand(const char* request_json);
 *   void        FreeAdapterString(char* p);
 *   const char* GetAdapterInfo(void);
 *
 * Request JSON shape:
 *   {
 *     "Adapter": "FileSystem",
 *     "Command": "ListFolder" | "CreateFolder" | "CreateFile" |
 *                "ReadFile" | "DeleteFile" | "MoveFile",
 *     "Parameters": { "TargetFolder": "...", "FileName": "...",
 *                     "FileContent": "...", "Encoding": "utf-8",
 *                     "OverwriteIfExists": true, ... }
 *   }
 */
#include "vc/vc_adapter.h"
#include "vc/vc_json.h"
#include "vc/vc_str.h"
#include "fs_commands.h"
#include <stdio.h>

static char *make_error(int code, const char *desc)
{
    vc_json *o = vc_json_new_object();
    vc_json_obj_set_num(o, "StatusCode", code);
    vc_json_obj_set_str(o, "StatusDescription", desc);
    char *s = vc_json_write(o);
    vc_json_free(o);
    return s ? s : vc_strdup("{\"StatusCode\":500,\"StatusDescription\":\"error\"}");
}

VC_ADAPTER_EXPORT char *RunAdapterCommand(const char *request_json)
{
    if (!request_json)
        return make_error(400, "Empty request");

    vc_json *root = vc_json_parse(request_json);
    if (!root)
        return make_error(400, "Invalid JSON format.");

    const char *cmd = vc_json_get_str(root, "Command", NULL);
    if (!cmd) {
        vc_json_free(root);
        return make_error(400, "\"Command\" parameter is required.");
    }

    char *result = NULL;
    if (!vc_stricmp(cmd, "ListFolder"))
        result = fs_cmd_list_folder(root);
    else if (!vc_stricmp(cmd, "CreateFolder"))
        result = fs_cmd_create_folder(root);
    else if (!vc_stricmp(cmd, "CreateFile"))
        result = fs_cmd_create_file(root);
    else if (!vc_stricmp(cmd, "ReadFile"))
        result = fs_cmd_read_file(root);
    else if (!vc_stricmp(cmd, "DeleteFile"))
        result = fs_cmd_delete_file(root);
    else if (!vc_stricmp(cmd, "MoveFile") || !vc_stricmp(cmd, "Move"))
        result = fs_cmd_move_file(root);
    else {
        char msg[256];
        snprintf(msg, sizeof msg, "Command \"%s\" not found.", cmd);
        result = make_error(404, msg);
    }

    vc_json_free(root);
    if (!result) result = make_error(500, "Adapter produced no result");
    return result;
}

VC_ADAPTER_EXPORT void FreeAdapterString(char *p)
{
    vc_free(p);
}

VC_ADAPTER_EXPORT const char *GetAdapterInfo(void)
{
    return
        "{"
        "\"id\":\"FileSystem\","
        "\"name\":\"File System Adapter\","
        "\"version\":\"1.0.0\","
        "\"vendor\":\"Veriland Consulting Ltd.\","
        "\"capabilities\":["
            "\"ListFolder\",\"CreateFolder\",\"CreateFile\","
            "\"ReadFile\",\"DeleteFile\",\"MoveFile\"],"
        "\"description\":\"Adapter providing basic file-system operations.\""
        "}";
}
