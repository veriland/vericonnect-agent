/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

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

namespace fs_cmd
{

    std::string list_folder(const vc::Json& req);
    std::string create_folder(const vc::Json& req);
    std::string create_file(const vc::Json& req);
    std::string read_file(const vc::Json& req);
    std::string delete_file(const vc::Json& req);
    std::string move_file(const vc::Json& req);

} // namespace fs_cmd

#endif
