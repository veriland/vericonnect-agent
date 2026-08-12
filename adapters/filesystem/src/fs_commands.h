/*
 * FileSystem adapter command handlers.
 *
 * Each takes the parsed request JSON (vc_json object with "Parameters")
 * and returns a malloc'd JSON result string (vc_free). The result
 * follows the VeriConnect contract: an object with StatusCode,
 * StatusDescription and, where relevant, a Data field.
 */
#ifndef FS_COMMANDS_H
#define FS_COMMANDS_H

#include "vc/vc_json.h"

char *fs_cmd_list_folder(const vc_json *req);
char *fs_cmd_create_folder(const vc_json *req);
char *fs_cmd_create_file(const vc_json *req);
char *fs_cmd_read_file(const vc_json *req);
char *fs_cmd_delete_file(const vc_json *req);
char *fs_cmd_move_file(const vc_json *req);

#endif
