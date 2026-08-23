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
        constexpr std::size_t kCtrlBodyMax = 60 * 1024; /* response via control ch */
        constexpr int kRecvTickMs = 1000;
        constexpr std::uint64_t kPingInterval = 30000;
        constexpr int kConnectTimeout = 20000;
        constexpr int kBodyTimeout = 30000;

        std::string_view as_view(std::span<const std::uint8_t> b)
        {
            return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
        }

        std::span<const std::uint8_t> as_bytes(std::string_view s)
        {
            return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
                                                 s.size());
        }

        /* The fields the listener actually uses out of a "request" node. The
         * string_views point into the Json it was parsed from, which outlives
         * every use here. */
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

        enum class ResponseChannel
        {
            Control,
            Rendezvous
        };

        /*
         * Where a response goes. The control channel caps a message at
         * kCtrlBodyMax, so a larger body needs a rendezvous connection - but
         * only if the service offered an address, and only if we are answering
         * on the control channel in the first place (a response already being
         * written to a rendezvous connection stays there).
         *
         * Pure, so it is tested directly rather than through the socket.
         */
        ResponseChannel choose_response_channel(bool on_control, std::size_t body_size,
                                                bool has_address) noexcept
        {
            if (on_control && body_size > kCtrlBodyMax && has_address)
                return ResponseChannel::Rendezvous;
            return ResponseChannel::Control;
        }

        class Listener
        {
        public:
            Listener(const RelayConfig& cfg, const RelayCallbacks& cb) : cfg_(cfg), cb_(cb)
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
                return sas_token(cfg_.namespace_host, cfg_.hybrid_connection, cfg_.key_name,
                                 cfg_.key, ttl_);
            }

            Status ctrl_connect();
            void renew_token_if_due();

            Json build_response_msg(std::string_view request_id, const RelayResponse& resp,
                                    bool has_body);
            Status send_response_over(WebSocket& ws, std::string_view request_id,
                                      const RelayResponse& resp);
            std::optional<WebSocket> rendezvous_connect(const std::string& address);
            Result<Bytes> read_body(WebSocket& ws, bool expected);
            void handle_request(WebSocket& ws, const Json& req_node, bool on_control);
            void handle_rendezvous_offer(const std::string& address);
            RelayResponse invoke_handler(const RelayRequest& req);
            Status deliver_response(WebSocket& ws, const ParsedRequest& pr,
                                    const RelayResponse& resp, bool on_control);
            void handle_control_message(std::span<const std::uint8_t> payload);

            const RelayConfig& cfg_;
            const RelayCallbacks& cb_;
            std::optional<WebSocket> ctrl_;
            unsigned ttl_;
            std::uint64_t token_renew_at_ = 0;
            std::uint64_t next_ping_at_ = 0;
        };

        Status Listener::ctrl_connect()
        {
            std::string token = make_token();
            if (token.empty()) return std::unexpected(Error::Fail);

            std::string path = "/$hc/" + cfg_.hybrid_connection +
                               "?sb-hc-action=listen&sb-hc-token=" + url_encode(token);

            Result<WebSocket> ws = ws_connect(cfg_.namespace_host, 443, path, "", kConnectTimeout);
            if (!ws) return std::unexpected(Error::Io);
            ctrl_ = std::move(*ws);

            std::uint64_t now = os::monotonic_ms();
            token_renew_at_ = now + static_cast<std::uint64_t>(ttl_) * 1000 * 3 / 4;
            next_ping_at_ = now + kPingInterval;
            return {};
        }

        void Listener::renew_token_if_due()
        {
            if (os::monotonic_ms() < token_renew_at_) return;
            std::string token = make_token();
            if (token.empty()) return;

            Json root = Json::object();
            root.set("renewToken", Json::object().set("token", Json::string(token)));
            std::string msg = root.dump();

            if (ctrl_->send(WebSocket::MsgType::Text, as_bytes(msg)))
            {
                token_renew_at_ =
                    os::monotonic_ms() + static_cast<std::uint64_t>(ttl_) * 1000 * 3 / 4;
                emit("TOKEN_RENEWED", 0, "SAS token renewed");
            }
        }

        Json Listener::build_response_msg(std::string_view request_id, const RelayResponse& resp,
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

        Status Listener::send_response_over(WebSocket& ws, std::string_view request_id,
                                            const RelayResponse& resp)
        {
            bool has_body = !resp.body.empty();
            std::string text = build_response_msg(request_id, resp, has_body).dump();

            Status rc = ws.send(WebSocket::MsgType::Text, as_bytes(text));
            if (!rc) return rc;
            if (has_body) rc = ws.send(WebSocket::MsgType::Binary, resp.body);
            return rc;
        }

        std::optional<WebSocket> Listener::rendezvous_connect(const std::string& address)
        {
            Result<Url> u = url_parse(address);
            if (!u) return std::nullopt;

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
            Result<WebSocket> ws = ws_connect(u->host, u->port, path, "", kConnectTimeout);
            if (!ws) return std::nullopt;
            return std::move(*ws);
        }

        Result<Bytes> Listener::read_body(WebSocket& ws, bool expected)
        {
            if (!expected) return Bytes{};
            Result<WebSocket::Message> m = ws.recv(kBodyTimeout);
            if (!m) return std::unexpected(m.error());
            if (m->type != WebSocket::MsgType::Binary) return std::unexpected(Error::Protocol);
            return std::move(m->payload);
        }

        void Listener::handle_rendezvous_offer(const std::string& address)
        {
            emit("RENDEZVOUS", 0, address);
            std::optional<WebSocket> rws = rendezvous_connect(address);
            if (!rws)
            {
                emit("RENDEZVOUS_FAILED", 0, address);
                return;
            }

            Result<WebSocket::Message> m = rws->recv(kBodyTimeout);
            if (m && m->type == WebSocket::MsgType::Text)
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

        RelayResponse Listener::invoke_handler(const RelayRequest& req)
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

        Status Listener::deliver_response(WebSocket& ws, const ParsedRequest& pr,
                                          const RelayResponse& resp, bool on_control)
        {
            if (choose_response_channel(on_control, resp.body.size(), pr.has_address) ==
                ResponseChannel::Rendezvous)
            {
                std::optional<WebSocket> rws = rendezvous_connect(pr.address);
                if (rws)
                {
                    Status src = send_response_over(*rws, pr.id, resp);
                    (void)rws->send_close(1000);
                    return src;
                }
                /* No rendezvous available: try the control channel anyway
                 * rather than dropping the response entirely. */
            }
            return send_response_over(ws, pr.id, resp);
        }

        void Listener::handle_request(WebSocket& ws, const Json& req_node, bool on_control)
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
                emit("RESPONSE_ERROR", static_cast<int>(src.error()), "failed to send response");
            else
                emit("RESPONSE_SENT", resp.status_code, pr.target);
        }

        void Listener::handle_control_message(std::span<const std::uint8_t> payload)
        {
            Result<Json> root = Json::parse(as_view(payload));
            if (!root)
            {
                emit("PROTOCOL", 0, "unparsable control message");
                return;
            }

            if (const Json* req = root->find_ci("request"))
            {
                handle_request(*ctrl_, *req, true);
                return;
            }
            if (root->find_ci("accept"))
            {
                emit("ACCEPT_IGNORED", 0,
                     "WebSocket accept offers are not supported by this listener");
                return;
            }
            /* token renew confirmations etc. are informational */
            emit("CONTROL", 0, root->dump());
        }

        Status Listener::run(const std::function<bool()>& stop)
        {
            unsigned backoff_ms = 1000;

            while (!stop())
            {
                emit("CONNECTING", 0, cfg_.namespace_host);
                if (!ctrl_connect())
                {
                    emit("CONNECT_FAILED", static_cast<int>(Error::Io), "will retry");
                    for (unsigned waited = 0; waited < backoff_ms && !stop(); waited += 100)
                        os::sleep_ms(100);
                    if (backoff_ms < 60000) backoff_ms *= 2;
                    continue;
                }
                backoff_ms = 1000;
                emit("CONNECTED", 200, "listening on control channel");

                while (!stop())
                {
                    renew_token_if_due();

                    std::uint64_t now = os::monotonic_ms();
                    if (now >= next_ping_at_)
                    {
                        (void)ctrl_->send_ping();
                        next_ping_at_ = now + kPingInterval;
                    }

                    Result<WebSocket::Message> m = ctrl_->recv(kRecvTickMs);
                    if (!m)
                    {
                        if (m.error() == Error::Timeout) continue;
                        emit("DISCONNECTED", static_cast<int>(m.error()), "control channel lost");
                        break;
                    }
                    if (m->type == WebSocket::MsgType::Close)
                    {
                        emit("DISCONNECTED", static_cast<int>(Error::Closed),
                             "control channel lost");
                        break;
                    }
                    if (m->type == WebSocket::MsgType::Text) handle_control_message(m->payload);
                }

                if (ctrl_)
                {
                    if (stop()) (void)ctrl_->send_close(1000);
                    ctrl_.reset();
                }
            }
            emit("STOPPED", 0, "listener stopped");
            return {};
        }
    } // namespace

    Status relay_listen(const RelayConfig& cfg, const RelayCallbacks& cb,
                        const std::function<bool()>& stop_requested)
    {
        if (cfg.namespace_host.empty() || cfg.hybrid_connection.empty())
            return std::unexpected(Error::InvalidArg);

        Socket::global_init();
        Listener listener(cfg, cb);
        return listener.run(stop_requested);
    }
} // namespace vc
