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
 *     "Parameters": { ... }
 *   }
 */
#include "vc/vc_adapter.h"
#include "vc/vc_json.h"
#include "fs_commands.h"

#include <cstring>
#include <format>
#include <string>

namespace {

using vc::Json;

/* Copy a std::string into a vc_alloc'd C buffer for the ABI boundary. */
char *to_abi(const std::string &s)
{
    char *out = static_cast<char *>(vc_alloc(s.size() + 1));
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

std::string make_error(int code, std::string_view desc)
{
    Json o = Json::object();
    o.set("StatusCode", Json::number(code));
    o.set("StatusDescription", Json::string(std::string(desc)));
    return o.dump();
}

bool iequals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string run(std::string_view request_json)
{
    vc::Result<Json> root = Json::parse(request_json);
    if (!root) return make_error(400, "Invalid JSON format.");

    std::string_view cmd = root->get_str("Command", "");
    if (cmd.empty()) return make_error(400, "\"Command\" parameter is required.");

    if (iequals(cmd, "ListFolder"))   return fs_cmd::list_folder(*root);
    if (iequals(cmd, "CreateFolder")) return fs_cmd::create_folder(*root);
    if (iequals(cmd, "CreateFile"))   return fs_cmd::create_file(*root);
    if (iequals(cmd, "ReadFile"))     return fs_cmd::read_file(*root);
    if (iequals(cmd, "DeleteFile"))   return fs_cmd::delete_file(*root);
    if (iequals(cmd, "MoveFile") || iequals(cmd, "Move")) return fs_cmd::move_file(*root);

    return make_error(404, std::format("Command \"{}\" not found.", cmd));
}

} // namespace

VC_ADAPTER_EXPORT char *RunAdapterCommand(const char *request_json)
{
    if (!request_json) return to_abi(make_error(400, "Empty request"));
    return to_abi(run(request_json));
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
