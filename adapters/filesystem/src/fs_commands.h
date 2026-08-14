/*
 * FileSystem adapter command handlers.
 *
 * Each takes the parsed request JSON (object with "Parameters") and returns a
 * JSON result string following the VeriConnect contract: an object with
 * StatusCode, StatusDescription and, where relevant, a Data field.
 */
#ifndef FS_COMMANDS_H
#define FS_COMMANDS_H

#include "vc/vc_json.h"

#include <string>

namespace fs_cmd {

std::string list_folder(const vc::Json &req);
std::string create_folder(const vc::Json &req);
std::string create_file(const vc::Json &req);
std::string read_file(const vc::Json &req);
std::string delete_file(const vc::Json &req);
std::string move_file(const vc::Json &req);

} // namespace fs_cmd

#endif
