/*
 * Azure Relay Hybrid Connections listener.
 *
 * Protocol (see Azure Relay "Hybrid Connections protocol guide"):
 *  - Control channel: WebSocket to
 *      wss://{ns}/$hc/{path}?sb-hc-action=listen&sb-hc-token={SAS}
 *  - The service sends JSON text messages:
 *      {"request": {...}}   HTTP request; body follows as binary msg
 *      {"accept":  {...}}   WebSocket accept offer (not supported here)
 *  - The listener answers HTTP requests with
 *      {"response": {...}}  followed by the body as a binary message
 *    over the control channel (small bodies) or over a rendezvous
 *    WebSocket dialed to the request's "address" (large bodies or
 *    rendezvous-only requests).
 *  - Tokens are renewed proactively with {"renewToken":{"token":..}}.
 */
#include "vc/vc_relay.h"
#include "vc/vc_ws.h"
#include "vc/vc_sas.h"
#include "vc/vc_json.h"
#include "vc/vc_url.h"
#include "vc/vc_str.h"
#include "vc/vc_os.h"
#include <stdio.h>

#define VC_RELAY_CTRL_BODY_MAX   (60 * 1024)  /* response via control ch */
#define VC_RELAY_RECV_TICK_MS    1000
#define VC_RELAY_PING_INTERVAL   30000
#define VC_RELAY_CONNECT_TIMEOUT 20000
#define VC_RELAY_BODY_TIMEOUT    30000

typedef struct relay_ctx {
    const vc_relay_config    *cfg;
    const vc_relay_callbacks *cb;
    vc_ws   *ctrl;
    unsigned ttl;
    uint64_t token_renew_at;   /* monotonic ms */
    uint64_t next_ping_at;
} relay_ctx;

static void emit(relay_ctx *rc, const char *event, int code, const char *desc)
{
    if (rc->cb && rc->cb->on_event)
        rc->cb->on_event(rc->cb->user, event, code, desc ? desc : "");
}

/* ---------------------------------------------------------------- */
/* Connection                                                        */
/* ---------------------------------------------------------------- */

static char *make_token(relay_ctx *rc)
{
    return vc_sas_token(rc->cfg->namespace_host, rc->cfg->hybrid_connection,
                        rc->cfg->key_name, rc->cfg->key, rc->ttl);
}

static int ctrl_connect(relay_ctx *rc)
{
    char *token = make_token(rc);
    if (!token) return VC_E_FAIL;
    char *tok_enc = vc_url_encode(token);
    vc_free(token);
    if (!tok_enc) return VC_E_NOMEM;

    vc_buf path;
    vc_buf_init(&path);
    vc_buf_appendf(&path, "/$hc/%s?sb-hc-action=listen&sb-hc-token=%s",
                   rc->cfg->hybrid_connection, tok_enc);
    vc_free(tok_enc);

    rc->ctrl = vc_ws_connect(rc->cfg->namespace_host, 443, path.data,
                             NULL, VC_RELAY_CONNECT_TIMEOUT);
    vc_buf_free(&path);
    if (!rc->ctrl) return VC_E_IO;

    uint64_t now = vc_os_monotonic_ms();
    rc->token_renew_at = now + (uint64_t)rc->ttl * 1000 * 3 / 4;
    rc->next_ping_at   = now + VC_RELAY_PING_INTERVAL;
    return VC_OK;
}

static void renew_token_if_due(relay_ctx *rc)
{
    if (vc_os_monotonic_ms() < rc->token_renew_at) return;
    char *token = make_token(rc);
    if (!token) return;

    vc_json *root = vc_json_new_object();
    vc_json *rt = vc_json_new_object();
    vc_json_obj_set_str(rt, "token", token);
    vc_json_obj_set(root, "renewToken", rt);
    char *msg = vc_json_write(root);
    vc_json_free(root);
    vc_free(token);
    if (!msg) return;

    if (vc_ws_send(rc->ctrl, VC_WS_TEXT, msg, strlen(msg)) == VC_OK) {
        rc->token_renew_at = vc_os_monotonic_ms() + (uint64_t)rc->ttl * 1000 * 3 / 4;
        emit(rc, "TOKEN_RENEWED", 0, "SAS token renewed");
    }
    vc_free(msg);
}

/* ---------------------------------------------------------------- */
/* Requests                                                          */
/* ---------------------------------------------------------------- */

static vc_json *build_response_msg(const char *request_id,
                                   const vc_relay_response *resp,
                                   bool has_body)
{
    vc_json *root = vc_json_new_object();
    vc_json *r = vc_json_new_object();
    vc_json_obj_set_str(r, "requestId", request_id);
    vc_json_obj_set_num(r, "statusCode", resp->status_code);
    if (resp->status_desc[0])
        vc_json_obj_set_str(r, "statusDescription", resp->status_desc);
    vc_json *hdrs = vc_json_new_object();
    vc_json_obj_set_str(hdrs, "Content-Type",
        resp->content_type[0] ? resp->content_type : "application/json");
    vc_json_obj_set(r, "responseHeaders", hdrs);
    vc_json_obj_set_bool(r, "body", has_body);
    vc_json_obj_set(root, "response", r);
    return root;
}

static int send_response_over(vc_ws *ws, const char *request_id,
                              const vc_relay_response *resp)
{
    bool has_body = resp->body && resp->body_len > 0;
    vc_json *msg = build_response_msg(request_id, resp, has_body);
    char *text = vc_json_write(msg);
    vc_json_free(msg);
    if (!text) return VC_E_NOMEM;

    int rc = vc_ws_send(ws, VC_WS_TEXT, text, strlen(text));
    vc_free(text);
    if (rc != VC_OK) return rc;

    if (has_body)
        rc = vc_ws_send(ws, VC_WS_BINARY, resp->body, resp->body_len);
    return rc;
}

/* Dial the rendezvous address given in a request message. */
static vc_ws *rendezvous_connect(relay_ctx *rc, const char *address)
{
    vc_url u;
    if (vc_url_parse(address, &u) != VC_OK) return NULL;

    vc_buf path;
    vc_buf_init(&path);
    vc_buf_append_str(&path, u.path);
    if (u.query) {
        vc_buf_append_char(&path, '?');
        vc_buf_append_str(&path, u.query);
    }
    /* append a token unless the address already carries one */
    if (!u.query || !strstr(u.query, "sb-hc-token=")) {
        char *token = make_token(rc);
        if (token) {
            char *enc = vc_url_encode(token);
            vc_free(token);
            if (enc) {
                vc_buf_appendf(&path, "%csb-hc-token=%s",
                               u.query ? '&' : '?', enc);
                vc_free(enc);
            }
        }
    }
    vc_ws *ws = vc_ws_connect(u.host, u.port, path.data, NULL,
                              VC_RELAY_CONNECT_TIMEOUT);
    vc_buf_free(&path);
    vc_url_free(&u);
    return ws;
}

static void response_defaults(vc_relay_response *resp)
{
    memset(resp, 0, sizeof *resp);
    resp->status_code = 500;
    snprintf(resp->status_desc, sizeof resp->status_desc,
             "Internal Server Error");
    snprintf(resp->content_type, sizeof resp->content_type,
             "application/json");
}

/* Reads the body (one binary message) if the request says one follows. */
static int read_body(vc_ws *ws, bool expected, uint8_t **body, size_t *len)
{
    *body = NULL;
    *len = 0;
    if (!expected) return VC_OK;
    vc_ws_msg_type type;
    int rc = vc_ws_recv_msg(ws, &type, body, len, VC_RELAY_BODY_TIMEOUT);
    if (rc != VC_OK) return rc;
    if (type != VC_WS_BINARY) {
        vc_free(*body);
        *body = NULL;
        *len = 0;
        return VC_E_PROTOCOL;
    }
    return VC_OK;
}

/* Handles one parsed {"request": ...} node arriving on channel 'ws'
 * (control or rendezvous). */
static void handle_request(relay_ctx *rc, vc_ws *ws, const vc_json *req_node,
                           bool on_control)
{
    const char *id      = vc_json_get_str(req_node, "id", "");
    const char *method  = vc_json_get_str(req_node, "method", NULL);
    const char *target  = vc_json_get_str(req_node, "requestTarget", "/");
    const char *address = vc_json_get_str(req_node, "address", NULL);
    bool has_body       = vc_json_get_bool(req_node, "body", false);

    /* Rendezvous-only offer: no method on the control message; the
     * full request is delivered on the rendezvous connection. */
    if (on_control && !method) {
        if (!address) {
            emit(rc, "REQUEST_ERROR", 0, "request without method or address");
            return;
        }
        emit(rc, "RENDEZVOUS", 0, address);
        vc_ws *rws = rendezvous_connect(rc, address);
        if (!rws) {
            emit(rc, "RENDEZVOUS_FAILED", 0, address);
            return;
        }
        /* Expect the request message on the rendezvous channel */
        vc_ws_msg_type type;
        uint8_t *payload = NULL;
        size_t plen = 0;
        int rcv = vc_ws_recv_msg(rws, &type, &payload, &plen,
                                 VC_RELAY_BODY_TIMEOUT);
        if (rcv == VC_OK && type == VC_WS_TEXT) {
            vc_json *root = vc_json_parse_len((const char *)payload, plen);
            vc_json *inner = root ? vc_json_obj_get_ci(root, "request") : NULL;
            if (inner)
                handle_request(rc, rws, inner, false);
            vc_json_free(root);
        }
        vc_free(payload);
        vc_ws_send_close(rws, 1000);
        vc_ws_close(rws);
        return;
    }

    /* Body (if any) follows as a binary message on the same channel. */
    uint8_t *body = NULL;
    size_t body_len = 0;
    if (read_body(ws, has_body, &body, &body_len) != VC_OK) {
        emit(rc, "REQUEST_ERROR", 0, "failed reading request body");
        return;
    }

    vc_relay_request req;
    memset(&req, 0, sizeof req);
    req.id = id;
    req.method = method ? method : "";
    req.target = target;
    req.body = body;
    req.body_len = body_len;

    char *headers_json = NULL;
    vc_json *hdrs = vc_json_obj_get_ci(req_node, "requestHeaders");
    if (hdrs) headers_json = vc_json_write(hdrs);
    req.headers_json = headers_json;

    vc_relay_response resp;
    response_defaults(&resp);
    if (rc->cb && rc->cb->on_request) {
        if (rc->cb->on_request(rc->cb->user, &req, &resp) != VC_OK)
            response_defaults(&resp);
    } else {
        resp.status_code = 501;
        snprintf(resp.status_desc, sizeof resp.status_desc, "Not Implemented");
    }
    vc_free(headers_json);
    vc_free(body);

    /* Send the response: control channel for small bodies, rendezvous
     * for big ones (the control channel caps messages at 64 KB). */
    int src;
    if (on_control && resp.body_len > VC_RELAY_CTRL_BODY_MAX && address) {
        vc_ws *rws = rendezvous_connect(rc, address);
        if (rws) {
            src = send_response_over(rws, id, &resp);
            vc_ws_send_close(rws, 1000);
            vc_ws_close(rws);
        } else {
            /* fall back to control channel; relay may reject it */
            src = send_response_over(ws, id, &resp);
        }
    } else {
        src = send_response_over(ws, id, &resp);
    }
    if (src != VC_OK)
        emit(rc, "RESPONSE_ERROR", src, "failed to send response");
    else
        emit(rc, "RESPONSE_SENT", resp.status_code, req.target);

    vc_free(resp.body);
}

static void handle_control_message(relay_ctx *rc, const uint8_t *payload,
                                   size_t len)
{
    vc_json *root = vc_json_parse_len((const char *)payload, len);
    if (!root) {
        emit(rc, "PROTOCOL", 0, "unparsable control message");
        return;
    }
    vc_json *req = vc_json_obj_get_ci(root, "request");
    if (req) {
        handle_request(rc, rc->ctrl, req, true);
        vc_json_free(root);
        return;
    }
    if (vc_json_obj_get_ci(root, "accept")) {
        emit(rc, "ACCEPT_IGNORED", 0,
             "WebSocket accept offers are not supported by this listener");
        vc_json_free(root);
        return;
    }
    /* token renew confirmations etc. are informational */
    char *text = vc_json_write(root);
    emit(rc, "CONTROL", 0, text ? text : "");
    vc_free(text);
    vc_json_free(root);
}

/* ---------------------------------------------------------------- */
/* Main loop                                                         */
/* ---------------------------------------------------------------- */

int vc_relay_listen(const vc_relay_config *cfg,
                    const vc_relay_callbacks *cb,
                    volatile bool *stop)
{
    if (!cfg || !cfg->namespace_host[0] || !cfg->hybrid_connection[0])
        return VC_E_INVALID_ARG;

    vc_sock_global_init();

    relay_ctx rc;
    memset(&rc, 0, sizeof rc);
    rc.cfg = cfg;
    rc.cb = cb;
    rc.ttl = cfg->token_ttl_seconds ? cfg->token_ttl_seconds : 3600;

    unsigned backoff_ms = 1000;

    while (!*stop) {
        emit(&rc, "CONNECTING", 0, cfg->namespace_host);
        int crc = ctrl_connect(&rc);
        if (crc != VC_OK) {
            emit(&rc, "CONNECT_FAILED", crc, "will retry");
            for (unsigned waited = 0; waited < backoff_ms && !*stop; waited += 100)
                vc_os_sleep_ms(100);
            if (backoff_ms < 60000) backoff_ms *= 2;
            continue;
        }
        backoff_ms = 1000;
        emit(&rc, "CONNECTED", 200, "listening on control channel");

        while (!*stop) {
            renew_token_if_due(&rc);

            uint64_t now = vc_os_monotonic_ms();
            if (now >= rc.next_ping_at) {
                vc_ws_send_ping(rc.ctrl);
                rc.next_ping_at = now + VC_RELAY_PING_INTERVAL;
            }

            vc_ws_msg_type type;
            uint8_t *payload = NULL;
            size_t len = 0;
            int rrc = vc_ws_recv_msg(rc.ctrl, &type, &payload, &len,
                                     VC_RELAY_RECV_TICK_MS);
            if (rrc == VC_E_TIMEOUT)
                continue;
            if (rrc == VC_OK) {
                if (type == VC_WS_TEXT)
                    handle_control_message(&rc, payload, len);
                vc_free(payload);
                continue;
            }
            /* closed or error -> reconnect */
            vc_free(payload);
            emit(&rc, "DISCONNECTED", rrc, "control channel lost");
            break;
        }

        if (rc.ctrl) {
            if (*stop) vc_ws_send_close(rc.ctrl, 1000);
            vc_ws_close(rc.ctrl);
            rc.ctrl = NULL;
        }
    }
    emit(&rc, "STOPPED", 0, "listener stopped");
    return VC_OK;
}
