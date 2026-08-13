/*
 * The reusable agent core.
 *
 * Loads settings + adapters, then runs the Azure Relay listener,
 * translating each relay request into an adapter dispatch. The
 * OS-specific hosts (Windows service, console app, future Linux daemon)
 * just call vc_agent_run.
 */
#include "vc/vc_agent.h"
#include "vc/vc_settings.h"
#include "vc/vc_adapter.h"
#include "vc/vc_relay.h"
#include "vc/vc_log.h"
#include "vc/vc_fs.h"
#include "vc/vc_str.h"
#include "vc/vc_json.h"
#include <stdio.h>

typedef struct agent_state {
    vc_adapter_registry registry;
} agent_state;

/* Relay -> adapter bridge. */
static int on_request(void *user, const vc_relay_request *req,
                      vc_relay_response *resp)
{
    agent_state *st = static_cast<agent_state*>(user);

    VC_INFO("REQUEST [%s] %s %s (%zu byte body)",
            req->id, req->method, req->target, req->body_len);

    /* The command JSON is the request body (UTF-8). */
    const char *cmd_json = (const char *)(req->body ? req->body : (const uint8_t *)"");
    char *result = vc_adapter_dispatch(&st->registry, cmd_json);

    VC_SUCC("ADAPTER RESULT [%s]: %s", req->id, result ? result : "(null)");

    /* Map the adapter's StatusCode/StatusDescription onto the HTTP
     * response the relay sends back. The full JSON is the body. */
    int status = 200;
    const char *desc = "OK";
    vc_json *root = result ? vc_json_parse(result) : NULL;
    if (root) {
        status = (int)vc_json_get_num(root, "StatusCode", 200);
        desc = vc_json_get_str(root, "StatusDescription", "OK");
        snprintf(resp->status_desc, sizeof resp->status_desc, "%s", desc);
    } else {
        snprintf(resp->status_desc, sizeof resp->status_desc, "OK");
    }
    resp->status_code = status;
    snprintf(resp->content_type, sizeof resp->content_type, "application/json");

    if (result) {
        resp->body_len = strlen(result);
        resp->body = (uint8_t *)result;  /* relay frees via vc_free */
    } else {
        resp->body = (uint8_t *)vc_strdup("{\"StatusCode\":500}");
        resp->body_len = resp->body ? strlen((char *)resp->body) : 0;
        resp->status_code = 500;
    }
    vc_json_free(root);
    return VC_OK;
}

static void on_event(void *user, const char *event, int code,
                     const char *description)
{
    (void)user;
    if (!vc_stricmp(event, "CONNECT_FAILED") ||
        !vc_stricmp(event, "RESPONSE_ERROR") ||
        !vc_stricmp(event, "RENDEZVOUS_FAILED"))
        VC_ERROR("RELAY %s [%d]: %s", event, code, description);
    else if (!vc_stricmp(event, "DISCONNECTED"))
        VC_WARN("RELAY %s [%d]: %s", event, code, description);
    else
        VC_INFO("RELAY %s [%d]: %s", event, code, description);
}

int vc_agent_run(const vc_agent_options *opts, volatile bool *stop)
{
    char *path = NULL;
    const char *settings_path = opts && opts->settings_path
        ? opts->settings_path
        : (path = vc_settings_default_path());

    vc_settings settings;
    int srv = vc_settings_load(settings_path, &settings);

    /* logging first so everything after is captured */
    if (opts && opts->verbose) {
        settings.logging.console = true;
        settings.logging.level = VC_LOG_TRACE;
    }
    /* place the log file next to the executable */
    {
        char *dir = vc_fs_exe_dir();
        if (dir) {
            char *lp = vc_fs_join(dir, "VeriConnect.log");
            if (lp) {
                snprintf(settings.logging.file_path,
                         sizeof settings.logging.file_path, "%s", lp);
                vc_free(lp);
            }
            vc_free(dir);
        }
    }
    vc_log_init(&settings.logging);

    VC_INFO("VeriConnect agent starting");
    if (srv == VC_E_NOT_FOUND)
        VC_WARN("Settings file not found (%s); using defaults", settings_path);
    else
        VC_INFO("Settings loaded from %s", settings_path);
    VC_INFO("Connection: ns=%s hc=%s keyName=%s",
            settings.relay.namespace_host,
            settings.relay.hybrid_connection,
            settings.relay.key_name);

    /* resolve adapters directory relative to the exe if not absolute */
    agent_state st;
    memset(&st, 0, sizeof st);
    char *adapters_dir = NULL;
    {
        const char *d = settings.adapters_dir[0] ? settings.adapters_dir : ".";
        bool absolute =
            (d[0] == '/' || d[0] == '\\' ||
             (d[0] && d[1] == ':'));
        if (absolute) {
            adapters_dir = vc_strdup(d);
        } else {
            char *exe = vc_fs_exe_dir();
            adapters_dir = exe ? vc_fs_join(exe, d) : vc_strdup(d);
            vc_free(exe);
        }
    }
    VC_INFO("Loading adapters from %s", adapters_dir);
    if (vc_adapter_registry_load(&st.registry, adapters_dir) != VC_OK)
        VC_WARN("No adapters loaded from %s", adapters_dir);
    vc_free(adapters_dir);

    vc_relay_callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.user = &st;
    cb.on_request = on_request;
    cb.on_event = on_event;

    int rc = VC_OK;
    if (!settings.relay.namespace_host[0] || !settings.relay.hybrid_connection[0]) {
        VC_ERROR("Connection settings incomplete; cannot start listener");
        rc = VC_E_INVALID_ARG;
    } else {
        rc = vc_relay_listen(&settings.relay, &cb, stop);
    }

    VC_INFO("VeriConnect agent stopped");
    vc_adapter_registry_unload(&st.registry);
    vc_log_shutdown();
    vc_free(path);
    return rc;
}
