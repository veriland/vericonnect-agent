/*
 * vc_ws.h - RFC 6455 WebSocket client over vc_tls.
 * Messages are reassembled from fragments; ping/pong is handled
 * internally by vc_ws_recv_msg.
 */
#ifndef VC_WS_H
#define VC_WS_H

#include "vc_common.h"
#include "vc_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_ws vc_ws;

typedef enum vc_ws_msg_type {
    VC_WS_TEXT   = 1,
    VC_WS_BINARY = 2,
    VC_WS_CLOSE  = 8
} vc_ws_msg_type;

/*
 * Connect + upgrade. path_and_query e.g. "/$hc/name?sb-hc-action=listen&...".
 * extra_headers: optional "Header: value\r\n" lines, may be NULL.
 */
vc_ws *vc_ws_connect(const char *host, int port,
                     const char *path_and_query,
                     const char *extra_headers,
                     int timeout_ms);

/* Send one complete message (auto-fragments large payloads). */
int vc_ws_send(vc_ws *ws, vc_ws_msg_type type, const void *data, size_t len);
int vc_ws_send_ping(vc_ws *ws);
int vc_ws_send_close(vc_ws *ws, uint16_t code);

/*
 * Receive the next data message. Returns VC_OK and sets *type,
 * *payload (vc_free), *len. Control frames (ping/pong) are handled
 * transparently. Returns VC_E_TIMEOUT if no complete message within
 * timeout_ms, VC_E_CLOSED when the peer closed, VC_E_* on error.
 * On VC_E_CLOSED *type is VC_WS_CLOSE and payload may hold the reason.
 */
int vc_ws_recv_msg(vc_ws *ws, vc_ws_msg_type *type,
                   uint8_t **payload, size_t *len, int timeout_ms);

void vc_ws_close(vc_ws *ws);

#ifdef __cplusplus
}
#endif

#endif
