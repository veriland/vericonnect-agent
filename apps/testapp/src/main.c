/*
 * vc-test - VeriConnect verbose test tool.
 *
 * Modes:
 *   vc-test listen [--settings <path>]
 *       Connect to Azure Relay immediately (console, very verbose) and
 *       serve commands with the loaded adapters - the same code path
 *       as the Windows service, without installing anything.
 *
 *   vc-test send --command <name> --folder <path> [options]
 *   vc-test send --json <raw-json-or-@file>
 *       Act as the *sender*: POST a command JSON through the relay's
 *       HTTPS endpoint (https://{ns}/{hc}) and print the round trip,
 *       so a listener on the other side (this tool or the service)
 *       can be tested end to end.
 *
 * Common options:
 *   --settings <path>   Settings.ini to use (default: next to the exe)
 */
#include <stdio.h>
#include <signal.h>

#include "vc/vc_agent.h"
#include "vc/vc_settings.h"
#include "vc/vc_sas.h"
#include "vc/vc_http.h"
#include "vc/vc_json.h"
#include "vc/vc_str.h"
#include "vc/vc_sock.h"
#include "vc/vc_fs.h"
#include "vc/vc_url.h"

static volatile bool g_stop = false;

#if defined(_WIN32)
#include <windows.h>
static BOOL WINAPI ctrl_handler(DWORD type)
{
    (void)type;
    g_stop = true;
    return TRUE;
}
static void install_ctrl_handler(void) { SetConsoleCtrlHandler(ctrl_handler, TRUE); }
#else
static void sig_handler(int sig) { (void)sig; g_stop = true; }
static void install_ctrl_handler(void)
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
}
#endif

static void usage(void)
{
    printf(
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

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 0; i < argc - 1; i++)
        if (!vc_stricmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

static bool arg_flag(int argc, char **argv, const char *name)
{
    for (int i = 0; i < argc; i++)
        if (!vc_stricmp(argv[i], name)) return true;
    return false;
}

/* ---------------------------------------------------------------- */
/* listen mode                                                        */
/* ---------------------------------------------------------------- */

static int do_listen(int argc, char **argv)
{
    install_ctrl_handler();
    printf("== VeriConnect test listener ==\n");
    printf("Connecting to Azure Relay... press Ctrl+C to stop.\n\n");

    vc_agent_options opts;
    memset(&opts, 0, sizeof opts);
    opts.settings_path = arg_value(argc, argv, "--settings");
    opts.verbose = true;
    return vc_agent_run(&opts, &g_stop) == 0 ? 0 : 1;
}

/* ---------------------------------------------------------------- */
/* send mode                                                          */
/* ---------------------------------------------------------------- */

static char *build_command_json(int argc, char **argv)
{
    const char *raw = arg_value(argc, argv, "--json");
    if (raw) {
        if (raw[0] == '@') {
            uint8_t *data = NULL;
            size_t len = 0;
            if (vc_fs_read_all(raw + 1, &data, &len) != VC_OK) {
                fprintf(stderr, "Cannot read %s\n", raw + 1);
                return NULL;
            }
            return (char *)data;
        }
        return vc_strdup(raw);
    }

    const char *cmd = arg_value(argc, argv, "--command");
    if (!cmd) {
        fprintf(stderr, "--command or --json is required for send mode\n");
        return NULL;
    }

    vc_json *root = vc_json_new_object();
    vc_json_obj_set_str(root, "Adapter", "FileSystem");
    vc_json_obj_set_str(root, "Command", cmd);

    vc_json *params = vc_json_new_object();
    const char *folder  = arg_value(argc, argv, "--folder");
    const char *file    = arg_value(argc, argv, "--file");
    const char *content = arg_value(argc, argv, "--content");
    const char *dfolder = arg_value(argc, argv, "--dest-folder");
    const char *dfile   = arg_value(argc, argv, "--dest-file");
    if (folder)  vc_json_obj_set_str(params, "TargetFolder", folder);
    if (file)    vc_json_obj_set_str(params, "FileName", file);
    if (content) vc_json_obj_set_str(params, "FileContent", content);
    if (dfolder) vc_json_obj_set_str(params, "DestinationFolder", dfolder);
    if (dfile)   vc_json_obj_set_str(params, "DestinationFileName", dfile);
    vc_json_obj_set_bool(params, "OverwriteIfExists",
                         arg_flag(argc, argv, "--overwrite"));
    vc_json_obj_set_str(params, "Encoding", "utf-8");
    vc_json_obj_set(root, "Parameters", params);

    char *json = vc_json_write(root);
    vc_json_free(root);
    return json;
}

static void print_body_pretty(const uint8_t *body, size_t len)
{
    if (!body || !len) {
        printf("  (empty body)\n");
        return;
    }
    printf("%.*s\n", (int)len, (const char *)body);

    /* if it is an adapter result, decode the useful bits */
    vc_json *root = vc_json_parse_len((const char *)body, len);
    if (root) {
        double code = vc_json_get_num(root, "StatusCode", -1);
        const char *desc = vc_json_get_str(root, "StatusDescription", NULL);
        if (code >= 0)
            printf("\n  -> StatusCode:        %d\n", (int)code);
        if (desc)
            printf("  -> StatusDescription: %s\n", desc);
        vc_json_free(root);
    }
}

static int do_send(int argc, char **argv)
{
    const char *settings_arg = arg_value(argc, argv, "--settings");
    char *default_path = NULL;
    const char *settings_path = settings_arg
        ? settings_arg
        : (default_path = vc_settings_default_path());

    vc_settings settings;
    if (vc_settings_load(settings_path, &settings) != VC_OK) {
        fprintf(stderr, "Cannot load settings from %s\n", settings_path);
        vc_free(default_path);
        return 1;
    }
    vc_free(default_path);

    if (!settings.relay.namespace_host[0] || !settings.relay.hybrid_connection[0]) {
        fprintf(stderr, "Settings are missing Namespace/HybridConnection\n");
        return 1;
    }

    char *json = build_command_json(argc, argv);
    if (!json) return 1;

    printf("== VeriConnect test sender ==\n");
    printf("Namespace:        %s\n", settings.relay.namespace_host);
    printf("HybridConnection: %s\n", settings.relay.hybrid_connection);
    printf("\n>> REQUEST BODY\n%s\n", json);

    char *token = vc_sas_token(settings.relay.namespace_host,
                               settings.relay.hybrid_connection,
                               settings.relay.key_name,
                               settings.relay.key, 3600);
    if (!token) {
        fprintf(stderr, "Failed to build SAS token (check AccessKey settings)\n");
        vc_free(json);
        return 1;
    }
    printf("\n>> SAS token generated (%zu chars)\n", strlen(token));

    vc_buf headers;
    vc_buf_init(&headers);
    vc_buf_appendf(&headers, "ServiceBusAuthorization: %s\r\n", token);
    vc_free(token);

    vc_buf path;
    vc_buf_init(&path);
    vc_buf_appendf(&path, "/%s", settings.relay.hybrid_connection);

    printf(">> POST https://%s%s\n", settings.relay.namespace_host, path.data);
    printf(">> sending...\n\n");

    vc_sock_global_init();
    vc_http_response resp;
    int rc = vc_http_request("POST", settings.relay.namespace_host, 443,
                             path.data, headers.data,
                             json, strlen(json), "application/json",
                             60000, &resp);
    vc_buf_free(&headers);
    vc_buf_free(&path);
    vc_free(json);

    if (rc != VC_OK) {
        fprintf(stderr,
            "HTTP request failed (%d). Is the network up and the relay reachable?\n",
            rc);
        return 1;
    }

    printf("<< HTTP %d %s\n", resp.status, resp.status_text ? resp.status_text : "");
    printf("<< HEADERS\n%s\n", resp.headers ? resp.headers : "");
    printf("\n<< BODY\n");
    print_body_pretty(resp.body, resp.body_len);

    int status = resp.status;
    vc_http_response_free(&resp);
    return (status >= 200 && status < 300) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 1; }
    if (!vc_stricmp(argv[1], "listen")) return do_listen(argc, argv);
    if (!vc_stricmp(argv[1], "send"))   return do_send(argc, argv);
    usage();
    return 1;
}
