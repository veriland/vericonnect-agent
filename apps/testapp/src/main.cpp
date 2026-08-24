/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc-test - VeriConnect verbose test tool.
 *
 * Modes:
 *   vc-test listen [--settings <path>]
 *       Connect to Azure Relay immediately (console, very verbose) and serve
 *       commands with the loaded adapters.
 *
 *   vc-test send --command <name> --folder <path> [options]
 *   vc-test send --json <raw-json-or-@file>
 *       Act as the sender: POST a command JSON through the relay's HTTPS
 *       endpoint and print the round trip.
 */
#include <csignal>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "vc/vc_agent.h"
#include "vc/vc_settings.h"
#include "vc/vc_sas.h"
#include "vc/vc_http.h"
#include "vc/vc_json.h"
#include "vc/vc_sock.h"
#include "vc/vc_fs.h"

#include <atomic>

namespace
{

    /* Set from a console control or signal handler, read by the run loop.
     * atomic rather than volatile. */
    std::atomic<bool> g_stop{false};

#if defined(_WIN32)
#include <windows.h>
    BOOL WINAPI ctrl_handler(DWORD)
    {
        g_stop.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    void install_ctrl_handler()
    {
        SetConsoleCtrlHandler(ctrl_handler, TRUE);
    }
#else
    void sig_handler(int)
    {
        g_stop.store(true, std::memory_order_relaxed);
    }
    void install_ctrl_handler()
    {
        std::signal(SIGINT, sig_handler);
        std::signal(SIGTERM, sig_handler);
    }
#endif

    bool iequals(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); i++)
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }

    void usage()
    {
        std::printf(
            "vc-test - VeriConnect relay test tool\n"
            "\n"
            "  vc-test listen [--settings <path>]\n"
            "      Run the agent listener in the console with verbose output.\n"
            "\n"
            "  vc-test send [--settings <path>] --command <name> --folder <path>\n"
            "               [--file <name>] [--content <text>] [--overwrite]\n"
            "               [--dest-folder <path>] [--dest-file <name>]\n"
            "  vc-test send [--settings <path>] --json <raw-json | @file>\n"
            "      Send a command through the relay HTTPS endpoint.\n"
            "\n"
            "  Commands: ListFolder CreateFolder CreateFile ReadFile DeleteFile MoveFile\n");
    }

    const char* arg_value(int argc, char** argv, std::string_view name)
    {
        for (int i = 0; i < argc - 1; i++)
            if (iequals(argv[i], name)) return argv[i + 1];
        return nullptr;
    }

    bool arg_flag(int argc, char** argv, std::string_view name)
    {
        for (int i = 0; i < argc; i++)
            if (iequals(argv[i], name)) return true;
        return false;
    }

    /* ---------------------------------------------------------------- */
    /* listen mode                                                        */
    /* ---------------------------------------------------------------- */

    int do_listen(int argc, char** argv)
    {
        install_ctrl_handler();
        std::printf("== VeriConnect test listener ==\n");
        std::printf("Connecting to Azure Relay... press Ctrl+C to stop.\n\n");

        vc::agent::Options opts;
        if (const char* sp = arg_value(argc, argv, "--settings")) opts.settings_path = sp;
        opts.verbose = true;
        return vc::agent::run(opts, [] { return g_stop.load(std::memory_order_relaxed); }) ? 0 : 1;
    }

    /* ---------------------------------------------------------------- */
    /* send mode                                                          */
    /* ---------------------------------------------------------------- */

    std::optional<std::string> build_command_json(int argc, char** argv)
    {
        if (const char* raw = arg_value(argc, argv, "--json"))
        {
            if (raw[0] == '@')
            {
                vc::Result<vc::Bytes> data = vc::fs::read_all(raw + 1);
                if (!data)
                {
                    std::fprintf(stderr, "Cannot read %s\n", raw + 1);
                    return std::nullopt;
                }
                return std::string(data->begin(), data->end());
            }
            return std::string(raw);
        }

        const char* cmd = arg_value(argc, argv, "--command");
        if (!cmd)
        {
            std::fprintf(stderr, "--command or --json is required for send mode\n");
            return std::nullopt;
        }

        vc::Json root = vc::Json::object();
        root.set("Adapter", vc::Json::string("FileSystem"));
        root.set("Command", vc::Json::string(cmd));

        vc::Json params = vc::Json::object();
        if (const char* v = arg_value(argc, argv, "--folder"))
            params.set("TargetFolder", vc::Json::string(v));
        if (const char* v = arg_value(argc, argv, "--file"))
            params.set("FileName", vc::Json::string(v));
        if (const char* v = arg_value(argc, argv, "--content"))
            params.set("FileContent", vc::Json::string(v));
        if (const char* v = arg_value(argc, argv, "--dest-folder"))
            params.set("DestinationFolder", vc::Json::string(v));
        if (const char* v = arg_value(argc, argv, "--dest-file"))
            params.set("DestinationFileName", vc::Json::string(v));
        params.set("OverwriteIfExists", vc::Json::boolean(arg_flag(argc, argv, "--overwrite")));
        params.set("Encoding", vc::Json::string("utf-8"));
        root.set("Parameters", std::move(params));

        return root.dump();
    }

    void print_body_pretty(std::span<const std::uint8_t> body)
    {
        if (body.empty())
        {
            std::printf("  (empty body)\n");
            return;
        }
        std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());
        std::printf("%.*s\n", static_cast<int>(text.size()), text.data());

        if (vc::Result<vc::Json> root = vc::Json::parse(text))
        {
            double code = root->get_num("StatusCode", -1);
            std::string_view desc = root->get_str("StatusDescription", "");
            if (code >= 0) std::printf("\n  -> StatusCode:        %d\n", static_cast<int>(code));
            if (!desc.empty())
                std::printf("  -> StatusDescription: %.*s\n", static_cast<int>(desc.size()),
                            desc.data());
        }
    }

    int do_send(int argc, char** argv)
    {
        const char* settings_arg = arg_value(argc, argv, "--settings");
        std::string settings_path = settings_arg ? settings_arg : vc::Settings::default_path();

        vc::Result<vc::Settings> loaded = vc::Settings::load(settings_path);
        if (!loaded)
        {
            std::fprintf(stderr, "Cannot load settings from %s\n", settings_path.c_str());
            return 1;
        }
        const vc::Settings& settings = *loaded;

        if (settings.relay.namespace_host.empty() || settings.relay.hybrid_connection.empty())
        {
            std::fprintf(stderr, "Settings are missing Namespace/HybridConnection\n");
            return 1;
        }

        std::optional<std::string> json = build_command_json(argc, argv);
        if (!json) return 1;

        std::printf("== VeriConnect test sender ==\n");
        std::printf("Namespace:        %s\n", settings.relay.namespace_host.c_str());
        std::printf("HybridConnection: %s\n", settings.relay.hybrid_connection.c_str());
        std::printf("\n>> REQUEST BODY\n%s\n", json->c_str());

        std::string token =
            vc::sas_token(settings.relay.namespace_host, settings.relay.hybrid_connection,
                          settings.relay.key_name, settings.relay.key, 3600);
        if (token.empty())
        {
            std::fprintf(stderr, "Failed to build SAS token (check AccessKey settings)\n");
            return 1;
        }
        std::printf("\n>> SAS token generated (%zu chars)\n", token.size());

        std::string headers = "ServiceBusAuthorization: " + token + "\r\n";
        std::string path = "/" + settings.relay.hybrid_connection;

        std::printf(">> POST https://%s%s\n", settings.relay.namespace_host.c_str(), path.c_str());
        std::printf(">> sending...\n\n");

        vc::Socket::global_init();

        vc::http::Request req;
        req.method = "POST";
        req.host = settings.relay.namespace_host;
        req.port = 443;
        req.path_and_query = path;
        req.extra_headers = headers;
        req.body = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(json->data()), json->size());
        req.content_type = "application/json";
        req.timeout_ms = 60000;

        vc::Result<vc::http::Response> resp = vc::http::request(req);
        if (!resp)
        {
            std::fprintf(stderr,
                         "HTTP request failed (%s). Is the network up and the relay reachable?\n",
                         vc::error_detail(resp.error()).c_str());
            return 1;
        }

        std::printf("<< HTTP %d %s\n", resp->status, resp->status_text.c_str());
        std::printf("<< HEADERS\n%s\n", resp->headers.c_str());
        std::printf("\n<< BODY\n");
        print_body_pretty(resp->body);

        return (resp->status >= 200 && resp->status < 300) ? 0 : 1;
    }

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
        return 1;
    }
    if (iequals(argv[1], "listen")) return do_listen(argc, argv);
    if (iequals(argv[1], "send")) return do_send(argc, argv);
    usage();
    return 1;
}
