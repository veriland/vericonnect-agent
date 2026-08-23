/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * FileSystem adapter commands, including ReadFile's "move the file into a COPY
 * subfolder after reading" semantics.
 */
#include "fs_commands.h"
#include "vc/vc_fs.h"
#include "vc/vc_base64.h"

#include <format>
#include <optional>
#include <span>
#include <string>

namespace fs_cmd
{

    namespace
    {

        using vc::Json;

        const Json* params_of(const Json& req)
        {
            return req.find_ci("Parameters");
        }

        std::string result_json(int code, std::string_view desc,
                                std::optional<Json> data = std::nullopt)
        {
            Json o = Json::object();
            o.set("StatusCode", Json::number(code));
            o.set("StatusDescription", Json::string(std::string(desc)));
            if (data) o.set("Data", std::move(*data));
            return o.dump();
        }

        /* Resolve TargetFolder + FileName; nullopt if either missing. */
        std::optional<std::string> target_path(const Json* p)
        {
            if (!p) return std::nullopt;
            std::string_view folder = p->get_str("TargetFolder", "");
            std::string_view name = p->get_str("FileName", "");
            if (folder.empty() || name.empty()) return std::nullopt;
            return vc::fs::join(folder, name);
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

        std::span<const std::uint8_t> as_bytes(std::string_view s)
        {
            return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
                                                 s.size());
        }

    } // namespace

    std::string list_folder(const Json& req)
    {
        const Json* p = params_of(req);
        std::string folder(p ? p->get_str("TargetFolder", "") : "");
        if (folder.empty())
            return result_json(400, "Missing parameters, please check your parameters");

        if (!vc::fs::dir_exists(folder))
            return result_json(404, std::format("Folder {} not found", folder));

        vc::Result<std::vector<std::string>> names = vc::fs::list_files(folder);
        if (!names || names->empty())
            return result_json(404, std::format("No files found in the folder {}", folder));

        Json files = Json::array();
        for (const std::string& n : *names)
            files.add(Json::string(n));

        Json data = Json::object();
        data.set("files", std::move(files));
        data.set("FileCount", Json::number(static_cast<double>(names->size())));

        return result_json(
            200, std::format("There are {} files under the {} folder", names->size(), folder),
            std::move(data));
    }

    std::string create_folder(const Json& req)
    {
        const Json* p = params_of(req);
        std::string folder(p ? p->get_str("TargetFolder", "") : "");
        if (folder.empty())
            return result_json(400, "Missing parameters, please check your parameters");

        if (vc::fs::dir_exists(folder))
            return result_json(200, std::format("Folder {} already exists", folder));

        /* create intermediate levels */
        for (std::size_t i = 1; i < folder.size(); i++)
        {
            if (folder[i] == '\\' || folder[i] == '/')
            {
                std::string prefix = folder.substr(0, i);
                /* skip drive roots like "C:" and UNC prefixes */
                if (prefix.size() > 2 || (prefix[1] != ':' && prefix[0] != '\\'))
                    (void)vc::fs::mkdir(prefix);
            }
        }
        if (!vc::fs::mkdir(folder))
            return result_json(400, std::format("Error creating folder: {}", folder));
        return result_json(200, std::format("Folder {} created successfully", folder));
    }

    std::string create_file(const Json& req)
    {
        const Json* p = params_of(req);
        std::optional<std::string> path = target_path(p);
        if (!path) return result_json(400, "Missing parameters, please check your parameters");

        bool overwrite = p->get_bool("OverwriteIfExists", false);
        if (vc::fs::file_exists(*path) && !overwrite)
            return result_json(409, "File already exist and overwrite if exists flag is disabled");

        std::string_view content = p->get_str("FileContent", "");
        std::string_view ctype = p->get_str("FileContentType", "");

        if (iequals(ctype, "base64"))
        {
            std::optional<vc::Bytes> bytes = vc::base64_decode(content);
            if (!bytes) return result_json(400, "FileContent is not valid base64");
            return vc::fs::write_all(*path, *bytes)
                       ? result_json(200,
                                     std::format("Data written successfully to file: {}", *path))
                       : result_json(400, std::format("Error writing to file: {}", *path));
        }

        /* The JSON string is already UTF-8; write the raw bytes without a BOM. */
        return vc::fs::write_all(*path, as_bytes(content))
                   ? result_json(200, std::format("Data written successfully to file: {}", *path))
                   : result_json(400, std::format("Error writing to file: {}", *path));
    }

    std::string read_file(const Json& req)
    {
        const Json* p = params_of(req);
        std::string folder(p ? p->get_str("TargetFolder", "") : "");
        std::optional<std::string> path = target_path(p);
        if (!path) return result_json(400, "Invalid parameters.");

        if (!vc::fs::file_exists(*path))
            return result_json(404, std::format("File {} not found.", *path));

        vc::Result<vc::Bytes> bytes = vc::fs::read_all(*path);
        if (!bytes) return result_json(500, std::format("Error reading file: {}", *path));

        std::string b64 = vc::base64_encode(*bytes);

        /* Post-read archive: move the file into <TargetFolder>/COPY so a polled
         * folder drains. */
        bool overwrite = p->get_bool("OverwriteIfExists", false);
        std::string name(p->get_str("FileName", ""));
        std::string copy_dir = vc::fs::join(folder, "COPY");
        std::string dest = vc::fs::join(copy_dir, name);

        int status = 200;
        std::string desc = std::format("File {} read successfully", *path);

        /* A failed mkdir surfaces as a failed move below. */
        if (!vc::fs::dir_exists(copy_dir)) (void)vc::fs::mkdir(copy_dir);

        vc::Status archived; /* default-constructed = success (409 path) */
        if (vc::fs::file_exists(dest))
        {
            if (overwrite)
            {
                (void)vc::fs::remove_file(dest); /* a failure here fails the move */
                archived = vc::fs::move(*path, dest);
            }
            else
            {
                status = 409;
                desc = std::format("File {} already exists and overwrite is not allowed.", dest);
            }
        }
        else
        {
            archived = vc::fs::move(*path, dest);
        }

        /* Data is returned either way, but a failed archive leaves the file in
         * the polled folder to be read again next poll: report it. */
        if (status == 200 && !archived)
        {
            status = 500;
            desc = std::format("File {} was read but could not be archived to {}: {}", *path, dest,
                               vc::error_str(archived.error()));
        }

        Json o = Json::object();
        o.set("StatusCode", Json::number(status));
        o.set("StatusDescription", Json::string(desc));
        o.set("Data", Json::string(b64));
        o.set("FileSize", Json::number(static_cast<double>(bytes->size())));
        return o.dump();
    }

    std::string delete_file(const Json& req)
    {
        const Json* p = params_of(req);
        std::optional<std::string> path = target_path(p);
        if (!path) return result_json(400, "Missing parameters, please check your parameters");

        if (!vc::fs::file_exists(*path))
            return result_json(404, std::format("File {} not found.", *path));
        if (vc::fs::remove_file(*path))
            return result_json(200, std::format("File {} deleted successfully", *path));
        return result_json(400, std::format("Error deleting file: {}", *path));
    }

    std::string move_file(const Json& req)
    {
        const Json* p = params_of(req);
        std::optional<std::string> src = target_path(p);
        std::string_view dest_folder = p ? p->get_str("DestinationFolder", "") : "";
        std::string_view dest_name =
            p ? p->get_str("DestinationFileName", p->get_str("FileName", "")) : "";
        if (!src || dest_folder.empty() || dest_name.empty())
            return result_json(
                400,
                "Missing parameters: TargetFolder, FileName and DestinationFolder are required");

        std::string dest = vc::fs::join(dest_folder, dest_name);
        bool overwrite = p->get_bool("OverwriteIfExists", false);

        if (!vc::fs::file_exists(*src))
            return result_json(404, std::format("File {} not found.", *src));
        if (vc::fs::file_exists(dest) && !overwrite)
            return result_json(
                409, std::format("File {} already exists and overwrite is not allowed.", dest));

        if (!vc::fs::dir_exists(std::string(dest_folder)))
            (void)vc::fs::mkdir(std::string(dest_folder));
        if (vc::fs::file_exists(dest)) (void)vc::fs::remove_file(dest);
        return vc::fs::move(*src, dest)
                   ? result_json(200, std::format("File moved from {} to {}", *src, dest))
                   : result_json(400, std::format("Error moving file {} to {}", *src, dest));
    }

} // namespace fs_cmd
