/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc_relay_testing.h - drive the relay listener without a network.
 *
 * The listener does not merely read and write one connection: it dials new
 * ones mid-stream, because a response larger than the control channel's cap
 * goes out over a fresh rendezvous connection. So a transport alone is not
 * enough to test it - the thing that has to be injected is the *dialler*.
 *
 * ScriptedDialler hands out pre-scripted WebSockets in order and records what
 * was dialled, so a test can assert both the bytes exchanged and which
 * endpoints the listener decided to open.
 *
 *   auto d = vc::ScriptedDialler();
 *   auto ctrl = d.expect_dial();          // wire for the control channel
 *   ctrl->push_incoming(text_frame(R"({"request":{...}})"));
 *   ctrl->set_eof();
 *   (void)vc::relay_listen_with(cfg, cb, d, stop_after_one_pass);
 */
#ifndef VC_RELAY_TESTING_H
#define VC_RELAY_TESTING_H

#include "vc_common.h"
#include "vc_relay.h"
#include "vc_scripted_transport.h"
#include "vc_ws.h"

#ifdef __cplusplus

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace vc
{
    class ScriptedDialler
    {
    public:
        using websocket_type = WebSocketT<ScriptedTransport>;

        struct Dial
        {
            std::string host;
            int port = 0;
            std::string path_and_query;
        };

        /*
         * The queue and the record live behind a shared_ptr because
         * relay_listen_with takes its dialler BY VALUE: the listener works on
         * a copy, so a test holding the original has to be looking at the
         * same state or it sees nothing happen.
         */
        struct State
        {
            std::deque<std::shared_ptr<ScriptedWire>> pending;
            std::vector<Dial> dials;
        };

        ScriptedDialler() : state_(std::make_shared<State>()) {}

        /*
         * Queue the next connection and return its wire. The upgrade response
         * is scripted immediately, so anything the test pushes afterwards is
         * read as post-handshake protocol traffic.
         */
        std::shared_ptr<ScriptedWire> expect_dial() const
        {
            auto wire = std::make_shared<ScriptedWire>();
            wire->push_incoming("HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
            state_->pending.push_back(wire);
            return wire;
        }

        /* Make the next dial fail instead of succeeding. */
        void fail_next_dial() const
        {
            state_->pending.push_back(nullptr);
        }

        /* Every dial attempted, in order, including failed ones. */
        [[nodiscard]] const std::vector<Dial>& dials() const noexcept
        {
            return state_->dials;
        }

        Result<websocket_type> operator()(const std::string& host, int port,
                                          const std::string& path_and_query,
                                          std::string_view extra_headers, int timeout_ms) const
        {
            state_->dials.push_back(Dial{host, port, path_and_query});
            if (state_->pending.empty()) return std::unexpected(Error::Io);
            std::shared_ptr<ScriptedWire> wire = state_->pending.front();
            state_->pending.pop_front();
            if (!wire) return std::unexpected(Error::Io);
            auto ws = websocket_type::upgrade(ScriptedTransport(wire), host, path_and_query,
                                              extra_headers, timeout_ms);
            if (!ws) return std::unexpected(ws.error());
            wire->clear_outgoing(); /* drop the handshake from the record */
            return ws;
        }

    private:
        std::shared_ptr<State> state_;
    };
} // namespace vc

#endif /* __cplusplus */

#endif
