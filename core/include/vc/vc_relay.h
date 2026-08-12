/*
 * vc_relay.h - Azure Relay Hybrid Connections listener.
 *
 * Implements the Hybrid Connections WebSocket protocol:
 *   - control channel  wss://{ns}/$hc/{path}?sb-hc-action=listen&sb-hc-token=...
 *   - 'request' messages (HTTP over relay), body over control channel
 *   - responses over the control channel (small) or a rendezvous
 *     connection (large bodies)
 *   - proactive SAS token renewal ('renewToken')
 *   - automatic reconnect with backoff
 */
#ifndef VC_RELAY_H
#define VC_RELAY_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_relay_listener vc_relay_listener;

typedef struct vc_relay_config {
    char namespace_host[256];   /* contoso.servicebus.windows.net */
    char hybrid_connection[128];
    char key_name[128];
    char key[256];
    unsigned token_ttl_seconds; /* 0 = default 3600 */
} vc_relay_config;

/* Response the handler wants to send back. */
typedef struct vc_relay_response {
    int      status_code;
    char     status_desc[256];
    uint8_t *body;        /* vc_alloc'd; listener takes ownership */
    size_t   body_len;
    char     content_type[128]; /* default application/json */
} vc_relay_response;

typedef struct vc_relay_request {
    const char *id;
    const char *method;
    const char *target;          /* requestTarget */
    const char *headers_json;    /* serialized requestHeaders or NULL */
    const uint8_t *body;
    size_t      body_len;
} vc_relay_request;

typedef struct vc_relay_callbacks {
    void *user;
    /* Fill 'resp'. Return VC_OK if resp is valid. */
    int  (*on_request)(void *user, const vc_relay_request *req,
                       vc_relay_response *resp);
    /* Informational events (connected, disconnected, renew, errors). */
    void (*on_event)(void *user, const char *event, int code,
                     const char *description);
} vc_relay_callbacks;

/*
 * Runs the listener loop until *stop becomes true.
 * Blocking call; reconnects on failures. Returns VC_OK on requested
 * stop, error code if it could never establish a connection and gave
 * up (it does not give up by default).
 */
int vc_relay_listen(const vc_relay_config *cfg,
                    const vc_relay_callbacks *cb,
                    volatile bool *stop);

#ifdef __cplusplus
}
#endif

#endif
