/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

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
 *    WebSocket dialed to the request's "address" (large bodies).
 *  - Tokens are renewed proactively with {"renewToken":{"token":..}}.
 */
#include "vc/vc_relay.h"
#include "vc/vc_log.h"

#include <ctime>
#include "vc/vc_relay_testing.h"
#include "vc/vc_ws.h"
#include "vc/vc_sas.h"
#include "vc/vc_json.h"
#include "vc/vc_url.h"
#include "vc/vc_os.h"
#include "vc/vc_sock.h"

#include <optional>

namespace vc
{
    namespace
    {
        constexpr std::size_t kCtrlBodyMax = std::size_t{60} * 1024; /* control ch. */
        constexpr int kRecvTickMs = 1000;
        constexpr std::uint64_t kPingInterval = 30000;
        constexpr int kConnectTimeout = 20000;
        constexpr int kBodyTimeout = 30000;

        /*
         * Is the SAS token due for renewal? The monotonic deadline is the
         * normal trigger, but the token's own expiry is wall-clock, so a clock
         * step either way could otherwise leave it expiring before renewal
         * fires or never renewing at all. Either condition triggers.
         * Pure, so it is tested directly.
         */
        bool renewal_due(std::uint64_t now_mono, std::uint64_t mono_due, std::uint64_t now_wall,
                         std::uint64_t expiry_wall, unsigned ttl_seconds) noexcept
        {
            if (now_mono >= mono_due) return true;
            if (expiry_wall == 0) return false;
            const std::uint64_t margin = ttl_seconds / 4;
            return now_wall + margin >= expiry_wall;
        }

        std::string_view as_view(std::span<const std::uint8_t> b)
        {
            return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
        }

        std::span<const std::uint8_t> as_bytes(std::string_view s)
        {
            return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
                                                 s.size());
        }

        /* Fields the listener uses from a "request" node. The views point into
         * the Json it was parsed from, which outlives every use here. */
        struct ParsedRequest
        {
            std::string_view id;
            std::string_view method;
            std::string_view target;
            std::string address; /* empty when the node carried none */
            bool has_method = false;
            bool has_address = false;
            bool has_body = false;
        };

        ParsedRequest parse_request_node(const Json& req_node)
        {
            ParsedRequest pr;
            pr.id = req_node.get_str("id", "");
            const Json* method_node = req_node.find_ci("method");
            pr.has_method = method_node && method_node->is_string();
            pr.method = pr.has_method ? method_node->as_string() : std::string_view{};
            pr.target = req_node.get_str("requestTarget", "/");
            const Json* addr_node = req_node.find_ci("address");
            pr.has_address = addr_node && addr_node->is_string();
            if (pr.has_address) pr.address = std::string(addr_node->as_string());
            pr.has_body = req_node.get_bool("body", false);
            return pr;
        }

        enum class ResponseChannel : std::uint8_t
        {
            Control,
            Rendezvous
        };

        /* Where a response goes. Over kCtrlBodyMax needs a rendezvous
         * connection, but only if the service offered an address and we are
         * answering on the control channel; a response already going out over
         * a rendezvous stays there. Pure, so it is tested directly. */
        ResponseChannel choose_response_channel(bool on_control, std::size_t body_size,
                                                bool has_address) noexcept
        {
            if (on_control && body_size > kCtrlBodyMax && has_address)
                return ResponseChannel::Rendezvous;
            return ResponseChannel::Control;
        }

        template <class Dialler> class Listener
        {
        public:
            /* WebSocket in production, WebSocketT<ScriptedTransport> in tests. */
            using Ws = typename Dialler::websocket_type;

            Listener(const RelayConfig& cfg, const RelayCallbacks& cb, Dialler dialler)
                : cfg_(cfg), cb_(cb), dial_(std::move(dialler))
            {
                ttl_ = cfg_.token_ttl_seconds ? cfg_.token_ttl_seconds : 3600;
            }

            Status run(const std::function<bool()>& stop);

        private:
            void emit(std::string_view event, int code, std::string_view desc)
            {
                if (cb_.on_event) cb_.on_event(event, code, desc);
            }

            std::string make_token()
            {
                /* Mirrors the expiry sas_token stamps into the token, so
                 * renewal can be judged against the same clock the service
                 * validates on. */
                token_expiry_wall_ = static_cast<std::uint64_t>(std::time(nullptr)) + ttl_;
                return sas_token(cfg_.namespace_host, cfg_.hybrid_connection, cfg_.key_name,
                                 cfg_.key, ttl_);
            }

            Status ctrl_connect();
            void renew_token_if_due(Ws& ctrl);

            Json build_response_msg(std::string_view request_id, const RelayResponse& resp,
                                    bool has_body);
            Status send_response_over(Ws& ws, std::string_view request_id,
                                      const RelayResponse& resp);
            Result<Ws> rendezvous_connect(const std::string& address);
            Result<Bytes> read_body(Ws& ws, bool expected);
            void handle_request(Ws& ws, const Json& req_node, bool on_control);
            void handle_rendezvous_offer(const std::string& address);
            RelayResponse invoke_handler(const RelayRequest& req);
            Status deliver_response(Ws& ws, const ParsedRequest& pr, const RelayResponse& resp,
                                    bool on_control);
            void handle_control_message(Ws& ctrl, std::span<const std::uint8_t> payload);

            const RelayConfig& cfg_;
            const RelayCallbacks& cb_;
            Dialler dial_;
            std::optional<Ws> ctrl_;
            unsigned ttl_;
            std::uint64_t token_renew_at_ = 0;    /* monotonic ms */
            std::uint64_t token_expiry_wall_ = 0; /* unix seconds */
            std::uint64_t next_ping_at_ = 0;
        };

        template <class D> Status Listener<D>::ctrl_connect()
        {
            std::string token = make_token();
            if (token.empty()) return std::unexpected(Error::Fail);

            std::string path = "/$hc/" + cfg_.hybrid_connection +
                               "?sb-hc-action=listen&sb-hc-token=" + url_encode(token);

            Result<Ws> ws = dial_(cfg_.namespace_host, 443, path, "", kConnectTimeout);
            /* Propagate, not flatten: the dialler's error carries the OS code
             * for the failure, and CONNECT_FAILED below is the one place an
             * operator sees why the agent cannot reach the relay. */
            if (!ws) return std::unexpected(ws.error());
            ctrl_ = std::move(*ws);

            std::uint64_t now = os::monotonic_ms();
            token_renew_at_ = now + static_cast<std::uint64_t>(ttl_) * 1000 * 3 / 4;
            next_ping_at_ = now + kPingInterval;
            return {};
        }

        template <class D> void Listener<D>::renew_token_if_due(Ws& ctrl)
        {
            if (!renewal_due(os::monotonic_ms(), token_renew_at_,
                             static_cast<std::uint64_t>(std::time(nullptr)), token_expiry_wall_,
                             ttl_))
                return;
            std::string token = make_token();
            if (token.empty()) return;

            Json root = Json::object();
            root.set("renewToken", Json::object().set("token", Json::string(token)));
            std::string msg = root.dump();

            if (ctrl.send(Ws::MsgType::Text, as_bytes(msg)))
            {
                token_renew_at_ =
                    os::monotonic_ms() + static_cast<std::uint64_t>(ttl_) * 1000 * 3 / 4;
                emit("TOKEN_RENEWED", 0, "SAS token renewed");
            }
        }

        template <class D>
        Json Listener<D>::build_response_msg(std::string_view request_id, const RelayResponse& resp,
                                             bool has_body)
        {
            Json r = Json::object();
            r.set("requestId", Json::string(std::string(request_id)));
            r.set("statusCode", Json::number(resp.status_code));
            if (!resp.status_desc.empty())
                r.set("statusDescription", Json::string(resp.status_desc));
            Json hdrs = Json::object();
            hdrs.set("Content-Type", Json::string(resp.content_type.empty() ? "application/json"
                                                                            : resp.content_type));
            r.set("responseHeaders", std::move(hdrs));
            r.set("body", Json::boolean(has_body));

            Json root = Json::object();
            root.set("response", std::move(r));
            return root;
        }

        template <class D>
        Status Listener<D>::send_response_over(Ws& ws, std::string_view request_id,
                                               const RelayResponse& resp)
        {
            bool has_body = !resp.body.empty();
            std::string text = build_response_msg(request_id, resp, has_body).dump();

            Status rc = ws.send(Ws::MsgType::Text, as_bytes(text));
            if (!rc) return rc;
            if (has_body) rc = ws.send(Ws::MsgType::Binary, resp.body);
            return rc;
        }

        template <class D>
        Result<typename Listener<D>::Ws> Listener<D>::rendezvous_connect(const std::string& address)
        {
            Result<Url> u = url_parse(address);
            if (!u) return std::unexpected(u.error());

            std::string path = u->path;
            if (!u->query.empty())
            {
                path += '?';
                path += u->query;
            }
            /* append a token unless the address already carries one */
            if (u->query.empty() || u->query.find("sb-hc-token=") == std::string::npos)
            {
                std::string token = make_token();
                if (!token.empty())
                {
                    path += (u->query.empty() ? '?' : '&');
                    path += "sb-hc-token=";
                    path += url_encode(token);
                }
            }
            Result<Ws> ws = dial_(u->host, u->port, path, "", kConnectTimeout);
            /* Propagate: the dialler's error carries the OS code, and
             * RENDEZVOUS_FAILED is where it gets reported. */
            if (!ws) return std::unexpected(ws.error());
            return std::move(*ws);
        }

        template <class D> Result<Bytes> Listener<D>::read_body(Ws& ws, bool expected)
        {
            if (!expected) return Bytes{};
            Result<typename Ws::Message> m = ws.recv(kBodyTimeout);
            if (!m) return std::unexpected(m.error());
            if (m->type != Ws::MsgType::Binary) return std::unexpected(Error::Protocol);
            return std::move(m->payload);
        }

        template <class D> void Listener<D>::handle_rendezvous_offer(const std::string& address)
        {
            emit("RENDEZVOUS", 0, address);
            Result<Ws> rws = rendezvous_connect(address);
            if (!rws)
            {
                const std::string why = address + ": " + error_detail(rws.error());
                emit("RENDEZVOUS_FAILED", static_cast<int>(rws.error().code()), why);
                return;
            }

            Result<typename Ws::Message> m = rws->recv(kBodyTimeout);
            if (m && m->type == Ws::MsgType::Text)
            {
                Result<Json> root = Json::parse(as_view(m->payload));
                if (root)
                {
                    const Json* inner = root->find_ci("request");
                    if (inner) handle_request(*rws, *inner, false);
                }
            }
            (void)rws->send_close(1000);
        }

        template <class D> RelayResponse Listener<D>::invoke_handler(const RelayRequest& req)
        {
            RelayResponse resp;
            if (!cb_.on_request)
            {
                resp.status_code = 501;
                resp.status_desc = "Not Implemented";
                return resp;
            }
            if (!cb_.on_request(req, resp)) resp = RelayResponse{};
            return resp;
        }

        template <class D>
        Status Listener<D>::deliver_response(Ws& ws, const ParsedRequest& pr,
                                             const RelayResponse& resp, bool on_control)
        {
            if (choose_response_channel(on_control, resp.body.size(), pr.has_address) ==
                ResponseChannel::Rendezvous)
            {
                Result<Ws> rws = rendezvous_connect(pr.address);
                if (rws)
                {
                    Status src = send_response_over(*rws, pr.id, resp);
                    (void)rws->send_close(1000);
                    return src;
                }
                /* No rendezvous: answer on the control channel rather than
                 * drop the response. */
            }
            return send_response_over(ws, pr.id, resp);
        }

        template <class D>
        void Listener<D>::handle_request(Ws& ws, const Json& req_node, bool on_control)
        {
            const ParsedRequest pr = parse_request_node(req_node);

            /* Rendezvous-only offer: no method on the control message, so the
             * full request is delivered on the rendezvous connection. */
            if (on_control && !pr.has_method)
            {
                if (!pr.has_address)
                {
                    emit("REQUEST_ERROR", 0, "request without method or address");
                    return;
                }
                handle_rendezvous_offer(pr.address);
                return;
            }

            /* Body (if any) follows as a binary message on the same channel. */
            Result<Bytes> body = read_body(ws, pr.has_body);
            if (!body)
            {
                emit("REQUEST_ERROR", 0, "failed reading request body");
                return;
            }

            std::string headers_json;
            if (const Json* hdrs = req_node.find_ci("requestHeaders")) headers_json = hdrs->dump();

            RelayRequest req;
            req.id = pr.id;
            req.method = pr.method;
            req.target = pr.target;
            req.headers_json = headers_json;
            req.body = *body;

            const RelayResponse resp = invoke_handler(req);
            const Status src = deliver_response(ws, pr, resp, on_control);

            if (!src)
            {
                const std::string why = "failed to send response: " + error_detail(src.error());
                emit("RESPONSE_ERROR", static_cast<int>(src.error().code()), why);
            }
            else
                emit("RESPONSE_SENT", resp.status_code, pr.target);
        }

        template <class D>
        void Listener<D>::handle_control_message(Ws& ctrl, std::span<const std::uint8_t> payload)
        {
            Result<Json> root = Json::parse(as_view(payload));
            if (!root)
            {
                emit("PROTOCOL", 0, "unparsable control message");
                return;
            }

            if (const Json* req = root->find_ci("request"))
            {
                handle_request(ctrl, *req, true);
                return;
            }
            if (root->find_ci("accept"))
            {
                emit("ACCEPT_IGNORED", 0,
                     "WebSocket accept offers are not supported by this listener");
                return;
            }
            /* Token renew confirmations and the like. The payload goes out as
             * an event, so keep it out of the Info stream. */
            emit("CONTROL", 0, "control message received");
            if (log::enabled(log::Level::Debug))
                log::message(log::Level::Debug, "RELAY CONTROL payload: {}", root->dump());
        }

        template <class D> Status Listener<D>::run(const std::function<bool()>& stop)
        {
            unsigned backoff_ms = 1000;

            while (!stop())
            {
                emit("CONNECTING", 0, cfg_.namespace_host);
                if (const Status cc = ctrl_connect(); !cc)
                {
                    const std::string why = error_detail(cc.error()) + ", will retry";
                    emit("CONNECT_FAILED", static_cast<int>(cc.error().code()), why);
                    for (unsigned waited = 0; waited < backoff_ms && !stop(); waited += 100)
                        os::sleep_ms(100);
                    if (backoff_ms < 60000) backoff_ms *= 2;
                    continue;
                }
                backoff_ms = 1000;
                emit("CONNECTED", 200, "listening on control channel");

                /* ctrl_connect() succeeded, so ctrl_ is engaged for this whole
                 * loop. Bind it once rather than dereference the optional on
                 * every use. */
                if (!ctrl_)
                {
                    emit("DISCONNECTED", static_cast<int>(Error::Fail),
                         "control channel vanished after connect");
                    continue;
                }
                Ws& ctrl = *ctrl_;

                while (!stop())
                {
                    renew_token_if_due(ctrl);

                    std::uint64_t now = os::monotonic_ms();
                    if (now >= next_ping_at_)
                    {
                        (void)ctrl.send_ping();
                        next_ping_at_ = now + kPingInterval;
                    }

                    Result<typename Ws::Message> m = ctrl.recv(kRecvTickMs);
                    if (!m)
                    {
                        if (m.error() == Error::Timeout) continue;
                        const std::string why = "control channel lost: " + error_detail(m.error());
                        emit("DISCONNECTED", static_cast<int>(m.error().code()), why);
                        break;
                    }
                    if (m->type == Ws::MsgType::Close)
                    {
                        emit("DISCONNECTED", static_cast<int>(Error::Closed),
                             "control channel lost");
                        break;
                    }
                    if (m->type == Ws::MsgType::Text) handle_control_message(ctrl, m->payload);
                }

                if (ctrl_)
                {
                    if (stop()) (void)ctrl.send_close(1000);
                    ctrl_.reset();
                }
            }
            emit("STOPPED", 0, "listener stopped");
            return {};
        }
        /* Production dialler: real TCP connect, TLS handshake, upgrade. */
        struct TlsDialler
        {
            using websocket_type = WebSocket;

            Result<WebSocket> operator()(const std::string& host, int port,
                                         const std::string& path_and_query,
                                         std::string_view extra_headers, int timeout_ms) const
            {
                return ws_connect(host, port, path_and_query, extra_headers, timeout_ms);
            }
        };
    } // namespace

    template <class Dialler>
    Status relay_listen_with(const RelayConfig& cfg, const RelayCallbacks& cb, Dialler dialler,
                             const std::function<bool()>& stop_requested)
    {
        if (cfg.namespace_host.empty() || cfg.hybrid_connection.empty())
            return std::unexpected(Error::InvalidArg);

        Listener<Dialler> listener(cfg, cb, std::move(dialler));
        return listener.run(stop_requested);
    }

    /* Instantiated for the scripted dialler so vc-selftest can drive the state
     * machine; the production path below uses TlsDialler directly. */
    template Status relay_listen_with<ScriptedDialler>(const RelayConfig&, const RelayCallbacks&,
                                                       ScriptedDialler,
                                                       const std::function<bool()>&);

    Status relay_listen(const RelayConfig& cfg, const RelayCallbacks& cb,
                        const std::function<bool()>& stop_requested)
    {
        Socket::global_init();
        return relay_listen_with(cfg, cb, TlsDialler{}, stop_requested);
    }
} // namespace vc
