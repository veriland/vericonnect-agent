/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

/*
 * vc-selftest - unit checks for the portable core.
 * Exit code 0 = all passed.
 */
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vc/vc_sha256.h"
#include "vc/vc_base64.h"
#include "vc/vc_json.h"
#include "vc/vc_url.h"
#include "vc/vc_ini.h"
#include "vc/vc_fs.h"
#include "vc/vc_adapter.h"
#include "vc/vc_ws.h"
#include "vc/vc_scripted_transport.h"
#include "vc/vc_http.h"
#include "vc/vc_relay.h"
#include "vc/vc_relay_testing.h"
#include "vc/vc_log.h"

namespace
{

    int g_failed = 0;

    void check(const char* name, bool cond)
    {
        if (cond)
            std::printf("  ok   %s\n", name);
        else
        {
            std::printf("  FAIL %s\n", name);
            g_failed++;
        }
    }

    std::string hex(std::span<const std::uint8_t> d)
    {
        static const char* h = "0123456789abcdef";
        std::string s;
        s.reserve(d.size() * 2);
        for (std::uint8_t b : d)
        {
            s += h[b >> 4];
            s += h[b & 0xF];
        }
        return s;
    }

    std::span<const std::uint8_t> bytes(std::string_view s)
    {
        return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()),
                                             s.size());
    }

    void test_sha256()
    {
        std::printf("SHA-256\n");
        check("sha256(\"abc\")",
              hex(vc::sha256("abc")) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check("sha256(\"\")",
              hex(vc::sha256("")) ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        check("sha256(56 chars)",
              hex(vc::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }

    void test_hmac()
    {
        std::printf("HMAC-SHA256 (RFC 4231)\n");
        check("tc2", hex(vc::hmac_sha256("Jefe", "what do ya want for nothing?")) ==
                         "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

        std::array<std::uint8_t, 20> key;
        key.fill(0x0b);
        check("tc1", hex(vc::hmac_sha256(std::span<const std::uint8_t>(key), bytes("Hi There"))) ==
                         "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

        std::vector<std::uint8_t> bigkey(131, 0xaa);
        check("tc6", hex(vc::hmac_sha256(
                         std::span<const std::uint8_t>(bigkey),
                         bytes("Test Using Larger Than Block-Size Key - Hash Key First"))) ==
                         "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    }

    void test_base64()
    {
        std::printf("Base64\n");
        check("encode", vc::base64_encode("foobar") == "Zm9vYmFy");
        check("encode pad2", vc::base64_encode("foob") == "Zm9vYg==");

        std::optional<vc::Bytes> d = vc::base64_decode("Zm9vYmE=");
        check("decode",
              d && d->size() == 5 &&
                  std::string_view(reinterpret_cast<const char*>(d->data()), 5) == "fooba");
        check("decode invalid", !vc::base64_decode("!!!"));
    }

    void test_base64_strict()
    {
        std::printf("base64 strictness\n");
        check("truncated single char rejected", !vc::base64_decode("A").has_value());
        check("valid round trip", vc::base64_decode("aGk=").has_value());
        check("two chars decode to one byte",
              vc::base64_decode("aGk=") && vc::base64_decode("aGk=")->size() == 2);
        check("data after padding rejected", !vc::base64_decode("aGk=a").has_value());
        check("bad alphabet rejected", !vc::base64_decode("aG!k").has_value());
        check("empty is valid", vc::base64_decode("") && vc::base64_decode("")->empty());
    }

    void test_json()
    {
        std::printf("JSON\n");
        const char* src = "{\"Adapter\":\"FileSystem\",\"Command\":\"CreateFile\","
                          "\"Parameters\":{\"TargetFolder\":\"C:\\\\tmp\",\"FileName\":\"x.txt\","
                          "\"OverwriteIfExists\":true,\"Depth\":-2.5,"
                          "\"Unicode\":\"caf\\u00e9 \\ud83d\\ude00\",\"Nothing\":null,"
                          "\"List\":[1,2,3]}}";
        vc::Result<vc::Json> root = vc::Json::parse(src);
        check("parse", root.has_value());
        if (!root) return;

        check("get str", root->get_str("Command", "") == "CreateFile");
        const vc::Json* p = root->find_ci("parameters");
        check("ci lookup", p != nullptr);
        check("nested str", p && p->get_str("TargetFolder", "") == "C:\\tmp");
        check("bool", p && p->get_bool("OverwriteIfExists", false));
        check("negative num", p && p->get_num("Depth", 0) == -2.5);
        check("utf8 escape", p && p->get_str("Unicode", "") == "caf\xC3\xA9 \xF0\x9F\x98\x80");
        const vc::Json* list = p ? p->find_ci("List") : nullptr;
        check("array size", list && list->size() == 3);

        std::string out = root->dump();
        check("write", !out.empty());
        check("round trip", vc::Json::parse(out).has_value());

        check("reject junk", !vc::Json::parse("{\"a\":}"));
        check("reject trailing", !vc::Json::parse("{} extra"));
    }

    void test_url()
    {
        std::printf("URL\n");
        check("encode", vc::url_encode("http://ns/path with space&x=1") ==
                            "http%3A%2F%2Fns%2Fpath%20with%20space%26x%3D1");

        vc::Result<vc::Url> u = vc::url_parse(
            "wss://g1-prod.servicebus.windows.net:443/$hc/x?sb-hc-action=accept&id=abc");
        check("parse rc", u.has_value());
        if (!u) return;
        check("host", u->host == "g1-prod.servicebus.windows.net");
        check("port", u->port == 443);
        check("path", u->path == "/$hc/x");
        check("query", u->query == "sb-hc-action=accept&id=abc");
    }

    void test_ini()
    {
        std::printf("INI\n");
        std::string path = vc::fs::join(vc::fs::exe_dir().value_or("."), "selftest.ini");

        vc::Ini ini;
        ini.set("Connection", "Namespace", "x.servicebus.windows.net");
        ini.set_int("Logging", "MaxRotateFiles", 7);
        check("save", ini.save(path).has_value());

        vc::Result<vc::Ini> loaded = vc::Ini::load(path);
        check("load", loaded.has_value());
        if (loaded)
        {
            check("get", loaded->get("Connection", "Namespace").value_or("") ==
                             "x.servicebus.windows.net");
            check("get int", loaded->get_int("Logging", "MaxRotateFiles", 0) == 7);
            check("default", loaded->get_int("Logging", "Missing", 42) == 42);
        }
        (void)vc::fs::remove_file(path);
    }

    int status_of(const std::string& res)
    {
        vc::Result<vc::Json> j = vc::Json::parse(res);
        return j ? static_cast<int>(j->get_num("StatusCode", 0)) : 0;
    }

    std::string make_request(std::string_view cmd, vc::Json params)
    {
        vc::Json r = vc::Json::object();
        r.set("Adapter", vc::Json::string("FileSystem"));
        r.set("Command", vc::Json::string(std::string(cmd)));
        r.set("Parameters", std::move(params));
        return r.dump();
    }

    void test_adapter_roundtrip()
    {
        std::printf("Adapter round trip (vc-adapter-filesystem)\n");
        std::string exe_dir = vc::fs::exe_dir().value_or(".");

        vc::AdapterRegistry reg;
        if (!reg.load(exe_dir))
        {
            std::printf("  skip (adapter dll not found next to vc-selftest)\n");
            return;
        }

        std::string tmp = vc::fs::join(exe_dir, "selftest-fs");
        (void)vc::fs::mkdir(tmp);

        /* CreateFile */
        {
            vc::Json params = vc::Json::object();
            params.set("TargetFolder", vc::Json::string(tmp));
            params.set("FileName", vc::Json::string("hello.txt"));
            params.set("FileContent", vc::Json::string("hello vericonnect"));
            params.set("OverwriteIfExists", vc::Json::boolean(true));
            params.set("Encoding", vc::Json::string("utf-8"));
            check("CreateFile 200",
                  status_of(reg.dispatch(make_request("CreateFile", std::move(params)))) == 200);
        }

        /* ListFolder should now see it */
        {
            vc::Json params = vc::Json::object();
            params.set("TargetFolder", vc::Json::string(tmp));
            check("ListFolder 200",
                  status_of(reg.dispatch(make_request("ListFolder", std::move(params)))) == 200);
        }

        /* ReadFile returns base64 and moves the file to COPY */
        {
            vc::Json params = vc::Json::object();
            params.set("TargetFolder", vc::Json::string(tmp));
            params.set("FileName", vc::Json::string("hello.txt"));
            params.set("OverwriteIfExists", vc::Json::boolean(true));
            std::string res = reg.dispatch(make_request("ReadFile", std::move(params)));
            vc::Result<vc::Json> j = vc::Json::parse(res);
            check("ReadFile 200", j && j->get_num("StatusCode", 0) == 200);
            if (j)
            {
                std::optional<vc::Bytes> raw = vc::base64_decode(j->get_str("Data", ""));
                check("ReadFile content",
                      raw && raw->size() == 17 &&
                          std::string_view(reinterpret_cast<const char*>(raw->data()), 17) ==
                              "hello vericonnect");
            }
        }

        std::string copyfile = vc::fs::join(vc::fs::join(tmp, "COPY"), "hello.txt");
        check("moved to COPY", vc::fs::file_exists(copyfile));
        (void)vc::fs::remove_file(copyfile);

        /* unknown command yields 404 */
        check("unknown cmd 404",
              status_of(reg.dispatch(make_request("Nope", vc::Json::object()))) == 404);

        /* Impersonation wiring: UserCredentials with a bogus user must fail before
         * the file op runs. Windows -> 403 (logon failed); POSIX -> 501. */
        {
            vc::Json creds = vc::Json::object();
            creds.set("Domain", vc::Json::string(""));
            creds.set("Username", vc::Json::string("vc_no_such_user_zzz"));
            creds.set("Password", vc::Json::string("x"));
            vc::Json params = vc::Json::object();
            params.set("TargetFolder", vc::Json::string("C:\\"));
            params.set("FileName", vc::Json::string("vc_imp_selftest.txt"));
            params.set("UserCredentials", std::move(creds));
            int sc = status_of(reg.dispatch(make_request("CreateFile", std::move(params))));
#if defined(_WIN32)
            check("impersonation bad creds -> 403", sc == 403);
#else
            check("impersonation unsupported -> 501", sc == 501);
#endif
        }
    }

    /* ---------------------------------------------------------------------
     * RFC 6455 framing, driven by a ScriptedWire instead of a network.
     * ------------------------------------------------------------------- */

    using WS = vc::WebSocketT<vc::ScriptedTransport>;
    using WirePtr = std::shared_ptr<vc::ScriptedWire>;

    /* Build a server->client frame. Server frames are never masked. */
    std::vector<std::uint8_t> server_frame(std::uint8_t opcode, bool fin, std::string_view payload)
    {
        std::vector<std::uint8_t> f;
        f.push_back(static_cast<std::uint8_t>((fin ? 0x80 : 0x00) | opcode));
        const std::size_t n = payload.size();
        if (n < 126)
            f.push_back(static_cast<std::uint8_t>(n));
        else if (n <= 0xFFFF)
        {
            f.push_back(126);
            f.push_back(static_cast<std::uint8_t>(n >> 8));
            f.push_back(static_cast<std::uint8_t>(n));
        }
        else
        {
            f.push_back(127);
            for (int i = 7; i >= 0; i--)
                f.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(n) >> (i * 8)));
        }
        f.insert(f.end(), payload.begin(), payload.end());
        return f;
    }

    /* One client frame decoded off the wire. */
    struct ClientFrame
    {
        std::uint8_t opcode = 0;
        bool fin = false;
        bool masked = false;
        std::string payload;
        std::size_t total = 0;
    };

    std::optional<ClientFrame> decode_client_frame(std::span<const std::uint8_t> b)
    {
        if (b.size() < 2) return std::nullopt;
        ClientFrame f;
        f.fin = (b[0] & 0x80) != 0;
        f.opcode = b[0] & 0x0F;
        f.masked = (b[1] & 0x80) != 0;
        std::uint64_t len = b[1] & 0x7F;
        std::size_t pos = 2;
        if (len == 126)
        {
            if (b.size() < pos + 2) return std::nullopt;
            len = (static_cast<std::uint64_t>(b[pos]) << 8) | b[pos + 1];
            pos += 2;
        }
        else if (len == 127)
        {
            if (b.size() < pos + 8) return std::nullopt;
            len = 0;
            for (int i = 0; i < 8; i++)
                len = (len << 8) | b[pos + i];
            pos += 8;
        }
        std::uint8_t mask[4] = {0, 0, 0, 0};
        if (f.masked)
        {
            if (b.size() < pos + 4) return std::nullopt;
            for (int i = 0; i < 4; i++)
                mask[i] = b[pos + i];
            pos += 4;
        }
        if (b.size() < pos + len) return std::nullopt;
        for (std::uint64_t i = 0; i < len; i++)
            f.payload.push_back(static_cast<char>(b[pos + i] ^ mask[i & 3]));
        f.total = pos + static_cast<std::size_t>(len);
        return f;
    }

    /* A wire past the upgrade, handshake request cleared so outgoing() shows
     * only what the test triggers. */
    std::optional<WS> upgraded(const WirePtr& wire)
    {
        wire->push_incoming("HTTP/1.1 101 Switching Protocols\r\n"
                            "Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
        auto ws = WS::upgrade(vc::ScriptedTransport(wire), "example.test", "/path?x=1", "", 5000);
        if (!ws) return std::nullopt;
        wire->clear_outgoing();
        return std::optional<WS>(std::move(*ws));
    }

    void test_ws_upgrade()
    {
        std::printf("WebSocket upgrade\n");
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 101 Switching Protocols\r\n\r\n");
            auto ws = WS::upgrade(vc::ScriptedTransport(wire), "example.test", "/path?x=1",
                                  "X-Extra: 1\r\n", 5000);
            check("101 accepted", ws.has_value());

            /* The handshake request must be a well formed upgrade. */
            const std::string req(wire->outgoing_text());
            check("request line", req.starts_with("GET /path?x=1 HTTP/1.1\r\n"));
            check("Host header", req.find("Host: example.test\r\n") != std::string::npos);
            check("Upgrade header", req.find("Upgrade: websocket\r\n") != std::string::npos);
            check("Connection header", req.find("Connection: Upgrade\r\n") != std::string::npos);
            check("version 13", req.find("Sec-WebSocket-Version: 13\r\n") != std::string::npos);
            check("extra header passed through", req.find("X-Extra: 1\r\n") != std::string::npos);
            check("request terminated", req.ends_with("\r\n\r\n"));

            /* Sec-WebSocket-Key must be 16 random bytes, base64 -> 24 chars. */
            const auto kp = req.find("Sec-WebSocket-Key: ");
            check("key present", kp != std::string::npos);
            if (kp != std::string::npos)
            {
                const auto eol = req.find("\r\n", kp);
                const std::string key = req.substr(kp + 19, eol - (kp + 19));
                check("key is 24 base64 chars", key.size() == 24 && key[23] == '=');
                auto raw = vc::base64_decode(key);
                check("key decodes to 16 bytes", raw && raw->size() == 16);
            }
        }
        {
            /* Two upgrades must not reuse the same nonce. */
            std::string k[2];
            for (int i = 0; i < 2; i++)
            {
                auto wire = std::make_shared<vc::ScriptedWire>();
                wire->push_incoming("HTTP/1.1 101 Switching Protocols\r\n\r\n");
                auto ws = WS::upgrade(vc::ScriptedTransport(wire), "h", "/p", "", 5000);
                const std::string req(wire->outgoing_text());
                const auto kp = req.find("Sec-WebSocket-Key: ");
                const auto eol = req.find("\r\n", kp);
                k[i] = req.substr(kp + 19, eol - (kp + 19));
            }
            check("nonce differs per handshake", k[0] != k[1]);
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 400 Bad Request\r\n\r\n");
            auto ws = WS::upgrade(vc::ScriptedTransport(wire), "h", "/p", "", 5000);
            check("non-101 rejected", !ws.has_value() && ws.error() == vc::Error::Protocol);
        }
        {
            /* Header block never completes and the peer closes: not a hang. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 101 Switching");
            wire->set_eof();
            auto ws = WS::upgrade(vc::ScriptedTransport(wire), "h", "/p", "", 5000);
            check("truncated header rejected", !ws.has_value());
        }
        {
            /* A transport that refuses the very first write. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->fail_next_send();
            auto ws = WS::upgrade(vc::ScriptedTransport(wire), "h", "/p", "", 5000);
            check("failed handshake write rejected",
                  !ws.has_value() && ws.error() == vc::Error::Io);
        }
    }

    void test_ws_recv()
    {
        std::printf("WebSocket recv\n");
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            check("upgraded", ws.has_value());
            auto f = server_frame(0x1, true, "hello");
            wire->push_incoming(std::span<const std::uint8_t>(f));
            auto m = ws->recv(1000);
            check("text frame received", m.has_value());
            check("text type", m && m->type == WS::MsgType::Text);
            check("text payload",
                  m && std::string(m->payload.begin(), m->payload.end()) == "hello");
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto a = server_frame(0x1, false, "Hel");
            auto b = server_frame(0x0, true, "lo!");
            wire->push_incoming(std::span<const std::uint8_t>(a));
            wire->push_incoming(std::span<const std::uint8_t>(b));
            auto m = ws->recv(1000);
            check("fragments reassembled",
                  m && std::string(m->payload.begin(), m->payload.end()) == "Hello!");
        }
        {
            /* Ping is answered with a matching pong, never surfaced. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto ping = server_frame(0x9, true, "pp");
            auto txt = server_frame(0x1, true, "after");
            wire->push_incoming(std::span<const std::uint8_t>(ping));
            wire->push_incoming(std::span<const std::uint8_t>(txt));
            auto m = ws->recv(1000);
            check("ping hidden from caller",
                  m && std::string(m->payload.begin(), m->payload.end()) == "after");
            auto pong = decode_client_frame(wire->outgoing());
            check("pong sent", pong && pong->opcode == 0xA);
            check("pong echoes payload", pong && pong->payload == "pp");
            check("pong is masked", pong && pong->masked);
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto cl = server_frame(0x8, true, "\x03\xe8");
            wire->push_incoming(std::span<const std::uint8_t>(cl));
            auto m = ws->recv(1000);
            check("close surfaces as Close", m && m->type == WS::MsgType::Close);
            auto echo = decode_client_frame(wire->outgoing());
            check("close echoed", echo && echo->opcode == 0x8);
            auto again = ws->recv(1000);
            check("recv after close fails",
                  !again.has_value() && again.error() == vc::Error::Closed);
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto bad = server_frame(0x0, true, "orphan");
            wire->push_incoming(std::span<const std::uint8_t>(bad));
            auto m = ws->recv(1000);
            check("orphan continuation rejected",
                  !m.has_value() && m.error() == vc::Error::Protocol);
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto m = ws->recv(0);
            check("empty recv times out", !m.has_value() && m.error() == vc::Error::Timeout);
        }
        {
            /* A frame split across two reads must still parse. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto f = server_frame(0x1, true, "split-me");
            std::vector<std::uint8_t> head(f.begin(), f.begin() + 4);
            std::vector<std::uint8_t> tail(f.begin() + 4, f.end());
            wire->push_incoming(std::span<const std::uint8_t>(head));
            auto partial = ws->recv(0);
            check("incomplete frame does not yield a message", !partial.has_value());
            wire->push_incoming(std::span<const std::uint8_t>(tail));
            auto m = ws->recv(1000);
            check("partial frame reassembled",
                  m && std::string(m->payload.begin(), m->payload.end()) == "split-me");
        }
        {
            /* 126..65535 uses the 16-bit length form. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            const std::string big(1000, 'x');
            auto f = server_frame(0x2, true, big);
            wire->push_incoming(std::span<const std::uint8_t>(f));
            auto m = ws->recv(1000);
            check("16-bit length frame parsed",
                  m && m->payload.size() == 1000 && m->type == WS::MsgType::Binary);
        }
        {
            /* Orderly close mid-message is an error, not a short message. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            auto a = server_frame(0x1, false, "Hel");
            wire->push_incoming(std::span<const std::uint8_t>(a));
            wire->set_eof();
            auto m = ws->recv(1000);
            check("eof mid-message is an error", !m.has_value());
        }
    }

    void test_ws_send()
    {
        std::printf("WebSocket send\n");
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            check("send small ok", ws->send(WS::MsgType::Text, bytes("hi")).has_value());
            auto f = decode_client_frame(wire->outgoing());
            check("one frame written", f.has_value());
            check("opcode text", f && f->opcode == 0x1);
            check("fin set", f && f->fin);
            check("client frame masked", f && f->masked);
            check("payload round-trips", f && f->payload == "hi");
            check("no trailing bytes", f && f->total == wire->outgoing().size());
        }
        {
            /* The same payload twice must not produce identical wire bytes. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            check("first send", ws->send(WS::MsgType::Text, bytes("same")).has_value());
            const vc::Bytes one = wire->outgoing();
            wire->clear_outgoing();
            check("second send", ws->send(WS::MsgType::Text, bytes("same")).has_value());
            const vc::Bytes two = wire->outgoing();
            check("mask varies per frame", one != two);
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            check("ping ok", ws->send_ping().has_value());
            auto f = decode_client_frame(wire->outgoing());
            check("ping opcode", f && f->opcode == 0x9);
            check("ping empty", f && f->payload.empty());
        }
        {
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            check("close ok", ws->send_close(1000).has_value());
            auto f = decode_client_frame(wire->outgoing());
            check("close opcode", f && f->opcode == 0x8);
            check("close carries code 1000",
                  f && f->payload.size() == 2 &&
                      static_cast<unsigned char>(f->payload[0]) == 0x03 &&
                      static_cast<unsigned char>(f->payload[1]) == 0xe8);
        }
        {
            /* 126..65535 payload uses the 16-bit length form. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            const std::string mid(200, 'm');
            check("mid send ok", ws->send(WS::MsgType::Binary, bytes(mid)).has_value());
            const auto& out = wire->outgoing();
            check("16-bit length used", out.size() > 1 && (out[1] & 0x7F) == 126);
            auto f = decode_client_frame(out);
            check("mid payload round-trips", f && f->payload == mid);
        }
        {
            /* Over kFragSize: split, opcode on the first frame, FIN on the
             * last. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            const std::string huge(150 * 1024, 'z');
            check("large send ok", ws->send(WS::MsgType::Binary, bytes(huge)).has_value());

            std::span<const std::uint8_t> rest(wire->outgoing());
            std::string rebuilt;
            int frames = 0;
            bool first = true, last_fin = false, opcodes_ok = true;
            while (!rest.empty())
            {
                auto f = decode_client_frame(rest);
                if (!f) break;
                if (first && f->opcode != 0x2) opcodes_ok = false;
                if (!first && f->opcode != 0x0) opcodes_ok = false;
                if (!f->masked) opcodes_ok = false;
                rebuilt += f->payload;
                last_fin = f->fin;
                rest = rest.subspan(f->total);
                first = false;
                frames++;
            }
            check("fragmented into several frames", frames > 1);
            check("continuation opcodes correct", opcodes_ok);
            check("only the last frame has FIN", last_fin);
            check("fragments rebuild the payload", rebuilt == huge);
        }
        {
            /* A transport write failure propagates instead of being lost. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            wire->fail_next_send();
            auto rc = ws->send(WS::MsgType::Text, bytes("nope"));
            check("send failure propagates", !rc.has_value() && rc.error() == vc::Error::Io);
        }
        {
            /* After close(), sends are refused rather than silently dropped. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            auto ws = upgraded(wire);
            ws->close();
            check("valid() false after close", !ws->valid());
            auto rc = ws->send(WS::MsgType::Text, bytes("x"));
            check("send after close refused", !rc.has_value() && rc.error() == vc::Error::Closed);
        }
    }

    /* ---------------------------------------------------------------------
     * HTTP request shape and response parsing, over a ScriptedWire.
     * ------------------------------------------------------------------- */

    vc::http::Request basic_request(std::string_view method = "GET")
    {
        vc::http::Request r;
        r.method = method;
        r.host = "api.example.test";
        r.port = 443;
        r.path_and_query = "/v1/thing?a=b";
        r.timeout_ms = 1000;
        return r;
    }

    void test_http()
    {
        std::printf("HTTP client\n");
        {
            /* Request shape. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("200 parsed", resp.has_value());
            check("status code", resp && resp->status == 200);
            check("status text", resp && resp->status_text == "OK");
            check("body", resp && std::string(resp->body.begin(), resp->body.end()) == "hi");
            const std::string sent(wire->outgoing_text());
            check("request line", sent.starts_with("GET /v1/thing?a=b HTTP/1.1\r\n"));
            check("host header", sent.find("Host: api.example.test\r\n") != std::string::npos);
            check("connection close", sent.find("Connection: close\r\n") != std::string::npos);
            check("no content-length without a body",
                  sent.find("Content-Length:") == std::string::npos);
        }
        {
            /* A body adds Content-Length and a default Content-Type. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");
            vc::ScriptedTransport t(wire);
            auto r = basic_request("POST");
            const std::string payload = "{\"k\":1}";
            r.body = bytes(payload);
            auto resp = vc::http::exchange(t, r);
            check("201 parsed", resp && resp->status == 201);
            const std::string sent(wire->outgoing_text());
            check("content-length set", sent.find("Content-Length: 7\r\n") != std::string::npos);
            check("default content-type json",
                  sent.find("Content-Type: application/json\r\n") != std::string::npos);
            check("body written", sent.ends_with(payload));
        }
        {
            /* An explicit content type is honoured, and extra headers pass. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
            vc::ScriptedTransport t(wire);
            auto r = basic_request("POST");
            r.body = bytes("x");
            r.content_type = "text/plain";
            r.extra_headers = "X-Trace: abc\r\n";
            auto resp = vc::http::exchange(t, r);
            check("explicit exchange ok", resp.has_value());
            const std::string sent(wire->outgoing_text());
            check("content-type honoured",
                  sent.find("Content-Type: text/plain\r\n") != std::string::npos);
            check("extra header passed", sent.find("X-Trace: abc\r\n") != std::string::npos);
        }
        {
            /* Chunked transfer encoding is decoded. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 200 OK\r\n"
                                "Transfer-Encoding: chunked\r\n\r\n"
                                "5\r\nHello\r\n"
                                "2\r\n, \r\n"
                                "6\r\nworld!\r\n"
                                "0\r\n\r\n");
            wire->set_eof();
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("chunked parsed", resp.has_value());
            check("chunked body reassembled",
                  resp && std::string(resp->body.begin(), resp->body.end()) == "Hello, world!");
        }
        {
            /* Chunk sizes are hex, not decimal: 0x10 == 16 bytes. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            const std::string sixteen = "0123456789abcdef";
            wire->push_incoming("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                                "10\r\n" +
                                sixteen + "\r\n0\r\n\r\n");
            wire->set_eof();
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("hex chunk size honoured",
                  resp && std::string(resp->body.begin(), resp->body.end()) == sixteen);
        }
        {
            /* Response arriving in dribbles across several reads. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 404 Not ");
            wire->push_incoming("Found\r\nContent-Length: 3\r\n");
            wire->push_incoming("\r\nabc");
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("split response parsed", resp && resp->status == 404);
            check("split status text", resp && resp->status_text == "Not Found");
            check("split body", resp && std::string(resp->body.begin(), resp->body.end()) == "abc");
        }
        {
            /* Header block never terminates: a protocol error, not a hang. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n");
            wire->set_eof();
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("unterminated headers rejected",
                  !resp.has_value() && resp.error() == vc::Error::Protocol);
        }
        {
            /* Not HTTP at all. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("GARBAGE\r\n\r\nbody");
            wire->set_eof();
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("non-HTTP rejected", !resp.has_value() && resp.error() == vc::Error::Protocol);
        }
        {
            /* A failed write surfaces instead of being mistaken for an empty
             * response. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->fail_next_send();
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("write failure propagates", !resp.has_value() && resp.error() == vc::Error::Io);
        }
        {
            /* Headers are exposed to the caller. */
            auto wire = std::make_shared<vc::ScriptedWire>();
            wire->push_incoming("HTTP/1.1 200 OK\r\nX-Thing: v\r\nContent-Length: 0\r\n\r\n");
            vc::ScriptedTransport t(wire);
            auto r = basic_request();
            auto resp = vc::http::exchange(t, r);
            check("headers exposed", resp && resp->headers.find("X-Thing: v") != std::string::npos);
            check("headers exclude the blank line",
                  resp && resp->headers.find("\r\n\r\n") == std::string::npos);
        }
    }

    /* ---------------------------------------------------------------------
     * Relay listener state machine, over a ScriptedDialler.
     * ------------------------------------------------------------------- */

    /* A server->client text frame carrying JSON. */
    std::vector<std::uint8_t> text_frame(std::string_view json)
    {
        return server_frame(0x1, true, json);
    }

    vc::RelayConfig test_relay_cfg()
    {
        vc::RelayConfig c;
        c.namespace_host = "ns.servicebus.windows.net";
        c.hybrid_connection = "hc";
        c.key_name = "policy";
        c.key = "c2VjcmV0";
        return c;
    }

    /* Collect every client frame written to a wire. */
    std::vector<ClientFrame> written_frames(const vc::ScriptedWire& wire)
    {
        std::vector<ClientFrame> out;
        std::span<const std::uint8_t> rest(wire.outgoing());
        while (!rest.empty())
        {
            auto f = decode_client_frame(rest);
            if (!f) break;
            rest = rest.subspan(f->total);
            out.push_back(*f);
        }
        return out;
    }

    /* Reassemble a binary message; a body over kFragSize arrives fragmented. */
    std::string binary_message(const vc::ScriptedWire& wire)
    {
        std::string out;
        bool collecting = false;
        for (const auto& f : written_frames(wire))
        {
            if (f.opcode == 0x2)
            {
                out.clear();
                out += f.payload;
                collecting = !f.fin;
                if (f.fin) return out;
            }
            else if (f.opcode == 0x0 && collecting)
            {
                out += f.payload;
                if (f.fin) return out;
            }
        }
        return out;
    }

    /* Text frames only, as strings - the JSON the listener sent. */
    std::vector<std::string> written_text(const vc::ScriptedWire& wire)
    {
        std::vector<std::string> out;
        for (const auto& f : written_frames(wire))
            if (f.opcode == 0x1) out.push_back(f.payload);
        return out;
    }

    /*
     * Stop on the listener's own events, not on a call count: run() calls
     * stop() several times per pass, and the wire can read as drained while
     * whole frames are still buffered inside the WebSocket. EOF on the wire
     * loses the control channel, which emits DISCONNECTED.
     */
    struct RelayRun
    {
        std::vector<std::string> events;
        bool finished = false;

        vc::RelayCallbacks::event_fn recorder()
        {
            return [this](std::string_view e, int, std::string_view)
            {
                events.emplace_back(e);
                if (e == "DISCONNECTED" || e == "CONNECT_FAILED") finished = true;
            };
        }
        [[nodiscard]] bool saw(std::string_view name) const
        {
            for (const auto& e : events)
                if (e == name) return true;
            return false;
        }
    };

    void test_relay()
    {
        std::printf("Relay listener\n");
        {
            /* Bad config is rejected before anything is dialled. */
            vc::ScriptedDialler d;
            vc::RelayConfig empty;
            vc::RelayCallbacks cb;
            auto rc = vc::relay_listen_with(empty, cb, d, [] { return true; });
            check("empty config rejected", !rc.has_value() && rc.error() == vc::Error::InvalidArg);
            check("nothing dialled for bad config", d.dials().empty());
        }
        {
            /* Control channel: namespace host on 443, listen action, token. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->set_eof();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            auto rc = vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("listen returns success on stop", rc.has_value());
            check("one dial made", d.dials().size() == 1);
            if (!d.dials().empty())
            {
                const auto& dial = d.dials()[0];
                check("dialled the namespace host", dial.host == "ns.servicebus.windows.net");
                check("dialled 443", dial.port == 443);
                check("control path is $hc", dial.path_and_query.starts_with("/$hc/hc?"));
                check("listen action",
                      dial.path_and_query.find("sb-hc-action=listen") != std::string::npos);
                check("token present",
                      dial.path_and_query.find("sb-hc-token=") != std::string::npos);
            }
            check("connected event emitted", run.saw("CONNECTED"));
        }
        {
            /* A control-channel request reaches the callback; the response
             * goes back the same way. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->push_incoming(std::span<const std::uint8_t>(text_frame(
                R"({"request":{"id":"r1","method":"POST","requestTarget":"/api/x","body":false}})")));
            ctrl->set_eof();

            std::string seen_method, seen_target, seen_id;
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            cb.on_request = [&](const vc::RelayRequest& req, vc::RelayResponse& resp)
            {
                seen_method = std::string(req.method);
                seen_target = std::string(req.target);
                seen_id = std::string(req.id);
                resp.status_code = 200;
                resp.status_desc = "OK";
                resp.body = vc::Bytes{'o', 'k'};
                return true;
            };
            auto rc = vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("relay run ok", rc.has_value());
            check("callback saw the method", seen_method == "POST");
            check("callback saw the target", seen_target == "/api/x");
            check("callback saw the id", seen_id == "r1");
            check("response sent event", run.saw("RESPONSE_SENT"));

            bool found_response = false;
            for (const auto& t : written_text(*ctrl))
                if (t.find("\"response\"") != std::string::npos &&
                    t.find("\"r1\"") != std::string::npos && t.find("200") != std::string::npos)
                    found_response = true;
            check("response written to the control channel", found_response);

            bool body_frame = false;
            for (const auto& f : written_frames(*ctrl))
                if (f.opcode == 0x2 && f.payload == "ok") body_frame = true;
            check("body sent as a binary frame", body_frame);
        }
        {
            /* No callback installed means 501, not silence. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->push_incoming(std::span<const std::uint8_t>(text_frame(
                R"({"request":{"id":"r2","method":"GET","requestTarget":"/","body":false}})")));
            ctrl->set_eof();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder(); /* on_request deliberately unset */
            (void)vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            bool got501 = false;
            for (const auto& t : written_text(*ctrl))
                if (t.find("501") != std::string::npos) got501 = true;
            check("missing handler answers 501", got501);
        }
        {
            /* A large response goes over a freshly dialled rendezvous. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            auto rdv = d.expect_dial();
            ctrl->push_incoming(std::span<const std::uint8_t>(
                text_frame(R"({"request":{"id":"r3","method":"GET","requestTarget":"/big",)"
                           R"("body":false,"address":"wss://rdv.example.test:443/path"}})")));
            ctrl->set_eof();

            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            cb.on_request = [&](const vc::RelayRequest&, vc::RelayResponse& resp)
            {
                resp.status_code = 200;
                resp.body.assign(70 * 1024, 'B'); /* over the 60 KB control cap */
                return true;
            };
            (void)vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("rendezvous dialled for a large body", d.dials().size() == 2);
            if (d.dials().size() == 2)
            {
                check("rendezvous host from the address", d.dials()[1].host == "rdv.example.test");
                check("rendezvous port from the address", d.dials()[1].port == 443);
            }
            /* 70 KB is over kFragSize, so it arrives as several frames. */
            const std::string on_rdv = binary_message(*rdv);
            check("large body went over the rendezvous", on_rdv.size() == 70 * 1024);
            check("large body intact", on_rdv == std::string(70 * 1024, 'B'));
            check("rendezvous body was fragmented", written_frames(*rdv).size() > 2);
            check("large body did not go over the control channel", binary_message(*ctrl).empty());
        }
        {
            /* A small response stays on the control channel even when an
             * address is offered: the size cap decides. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->push_incoming(std::span<const std::uint8_t>(
                text_frame(R"({"request":{"id":"r4","method":"GET","requestTarget":"/small",)"
                           R"("body":false,"address":"wss://rdv.example.test:443/path"}})")));
            ctrl->set_eof();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            cb.on_request = [&](const vc::RelayRequest&, vc::RelayResponse& resp)
            {
                resp.status_code = 200;
                resp.body = vc::Bytes{'s'};
                return true;
            };
            (void)vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("small response makes no extra dial", d.dials().size() == 1);
            check("small response still answered", run.saw("RESPONSE_SENT"));
        }
        {
            /* An accept offer is reported and ignored, never answered. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->push_incoming(
                std::span<const std::uint8_t>(text_frame(R"({"accept":{"address":"wss://x/y"}})")));
            ctrl->set_eof();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            (void)vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("accept offer ignored", run.saw("ACCEPT_IGNORED"));
            check("accept made no extra dial", d.dials().size() == 1);
        }
        {
            /* Unparsable control traffic is reported, not fatal. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->push_incoming(std::span<const std::uint8_t>(text_frame("{not json")));
            ctrl->set_eof();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            auto rc = vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("bad control JSON reported", run.saw("PROTOCOL"));
            check("bad control JSON is not fatal", rc.has_value());
        }
        {
            /* A request with neither method nor address is rejected. */
            vc::ScriptedDialler d;
            auto ctrl = d.expect_dial();
            ctrl->push_incoming(
                std::span<const std::uint8_t>(text_frame(R"({"request":{"id":"r5"}})")));
            ctrl->set_eof();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            (void)vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("method-less, address-less request rejected", run.saw("REQUEST_ERROR"));
            check("no rendezvous attempted", d.dials().size() == 1);
        }
        {
            /* A failed dial is reported and does not spin. */
            vc::ScriptedDialler d;
            d.fail_next_dial();
            RelayRun run;
            vc::RelayCallbacks cb;
            cb.on_event = run.recorder();
            auto rc = vc::relay_listen_with(test_relay_cfg(), cb, d, [&] { return run.finished; });
            check("failed dial reported", run.saw("CONNECT_FAILED"));
            check("failed dial still returns success on stop", rc.has_value());
            check("failed dial was attempted", d.dials().size() == 1);
        }
    }

    /* ---------------------------------------------------------------------
     * Logger: the guarantees it makes at the sink.
     * ------------------------------------------------------------------- */

    /* Log one message to a temp file and return the file's contents. */
    std::string log_once(vc::log::Level lvl, std::string_view msg, vc::log::Level threshold)
    {
        const std::string dir = vc::fs::exe_dir().value_or(".");
        const std::string path = vc::fs::join(dir, "selftest-log.txt");
        (void)vc::fs::remove_file(path);

        vc::log::Config c;
        c.level = threshold;
        c.enabled = true;
        c.console = false;
        c.file_path = path;
        c.show_event_type = true;
        if (!vc::log::init(c)) return {};
        vc::log::write(lvl, msg);
        vc::log::shutdown();

        auto bytes = vc::fs::read_all(path);
        (void)vc::fs::remove_file(path);
        if (!bytes) return {};
        return std::string(bytes->begin(), bytes->end());
    }

    void test_logging()
    {
        std::printf("Logging\n");
        using L = vc::log::Level;

        /* A newline in a payload must not be able to forge a second line. */
        {
            const std::string out = log_once(L::Info, "first\nsecond", L::Trace);
            check("message written", out.find("first") != std::string::npos);
            check("newline escaped", out.find("first\\nsecond") != std::string::npos);
            check("only one log line", std::count(out.begin(), out.end(), '\n') == 1);
        }
        /* Other control characters are escaped too. */
        {
            const std::string out = log_once(L::Info,
                                             "a\tb\x01"
                                             "c",
                                             L::Trace);
            check("tab escaped", out.find("a\\tb") != std::string::npos);
            check("control byte escaped", out.find("\\x01") != std::string::npos);
        }
        /* Secrets are masked at the sink, whatever the call site does. */
        {
            const std::string out = log_once(
                L::Info, "GET /$hc/x?sb-hc-action=listen&sb-hc-token=SharedAccessSig%2fabc&z=1",
                L::Trace);
            check("token value masked", out.find("SharedAccessSig%2fabc") == std::string::npos);
            check("token key kept", out.find("sb-hc-token=***") != std::string::npos);
            check("other params intact", out.find("sb-hc-action=listen") != std::string::npos);
        }
        {
            const std::string out =
                log_once(L::Info, R"({"Username":"u","Password":"hunter2","x":1})", L::Trace);
            check("json password masked", out.find("hunter2") == std::string::npos);
            check("json key kept", out.find("\"Password\":\"***\"") != std::string::npos);
            check("json neighbours intact", out.find("\"Username\":\"u\"") != std::string::npos);
        }
        {
            const std::string out = log_once(L::Info, "sig=abc123&next=2", L::Trace);
            check("sig masked", out.find("abc123") == std::string::npos);
            check("field after sig intact", out.find("next=2") != std::string::npos);
        }
        /* A long message is truncated rather than written whole. */
        {
            const std::string big(vc::log::kMaxMessageBytes * 3, 'x');
            const std::string out = log_once(L::Info, big, L::Trace);
            check("long message truncated", out.size() < big.size());
            check("truncation is marked", out.find("[truncated") != std::string::npos);
        }
        /* Level filtering, and enabled() agreeing with it. */
        {
            const std::string out = log_once(L::Debug, "quiet", L::Error);
            check("below-threshold message dropped", out.find("quiet") == std::string::npos);
        }
        {
            vc::log::Config c;
            c.level = L::Warn;
            c.enabled = true;
            c.console = false;
            check("init with no file", vc::log::init(c).has_value());
            check("enabled() false below threshold", !vc::log::enabled(L::Info));
            check("enabled() true at threshold", vc::log::enabled(L::Warn));
            check("enabled() true above threshold", vc::log::enabled(L::Error));
            check("Succ filtered as Info", !vc::log::enabled(L::Succ));
            vc::log::shutdown();
        }
    }

    /* ---------------------------------------------------------------------
     * Bounded numeric parsing, used for anything arriving off the machine.
     * ------------------------------------------------------------------- */

    void test_parse_uint()
    {
        std::printf("parse_uint\n");
        check("plain decimal", vc::parse_uint("42", 100) == 42u);
        check("at the limit", vc::parse_uint("100", 100) == 100u);
        check("over the limit rejected", !vc::parse_uint("101", 100).has_value());
        check("empty rejected", !vc::parse_uint("", 100).has_value());
        check("negative rejected", !vc::parse_uint("-1", 100).has_value());
        check("plus rejected", !vc::parse_uint("+1", 100).has_value());
        check("trailing junk rejected", !vc::parse_uint("12abc", 100).has_value());
        check("leading space rejected", !vc::parse_uint(" 12", 100).has_value());
        check("not a number rejected", !vc::parse_uint("abc", 100).has_value());
        check("overflow rejected",
              !vc::parse_uint("99999999999999999999999", 0xFFFFFFFFull).has_value());
        check("hex base", vc::parse_uint("10", 100, 16) == 16u);
        check("hex rejects decimal-only digits", !vc::parse_uint("1g", 100, 16).has_value());
        check("zero accepted", vc::parse_uint("0", 100) == 0u);
    }

    void test_url_port()
    {
        std::printf("URL port validation\n");
        {
            auto u = vc::url_parse("wss://h.example:8443/p");
            check("valid port parsed", u && u->port == 8443);
        }
        {
            auto u = vc::url_parse("wss://h.example/p");
            check("default port for wss", u && u->port == 443);
        }
        {
            auto u = vc::url_parse("wss://h.example:0/p");
            check("port 0 rejected", !u.has_value());
        }
        {
            auto u = vc::url_parse("wss://h.example:99999/p");
            check("out-of-range port rejected", !u.has_value());
        }
        {
            auto u = vc::url_parse("wss://h.example:-5/p");
            check("negative port rejected", !u.has_value());
        }
        {
            auto u = vc::url_parse("wss://h.example:80x/p");
            check("junk port rejected", !u.has_value());
        }
    }

    /* ---------------------------------------------------------------------
     * JSON numbers. Json::parse no longer copies the document, so a number
     * token ending exactly at the end of the buffer is the case to prove.
     * ------------------------------------------------------------------- */

    void test_json_numbers()
    {
        std::printf("JSON numbers\n");
        auto num = [](std::string_view t) -> std::optional<double>
        {
            auto j = vc::Json::parse(t);
            if (!j || !j->is_number()) return std::nullopt;
            return j->as_number();
        };
        check("integer", num("123") == 123.0);
        check("negative", num("-7") == -7.0);
        check("fraction", num("1.5") == 1.5);
        check("exponent", num("2e3") == 2000.0);
        check("negative exponent", num("5e-1") == 0.5);
        check("zero", num("0") == 0.0);

        /* Numbers flush against the end of the buffer, with no NUL to lean on. */
        {
            auto j = vc::Json::parse("[1,2,3]");
            check("array of numbers", j && j->size() == 3);
        }
        {
            auto j = vc::Json::parse(R"({"a":42})");
            check("number at object end", j && j->get_num("a", 0) == 42.0);
        }
        {
            /* string_view over a buffer with no terminator at all. */
            const char raw[] = {'9', '9', '9'};
            auto j = vc::Json::parse(std::string_view(raw, sizeof raw));
            check("unterminated buffer parsed", j && j->as_number() == 999.0);
        }
        /* Malformed input must fail, not read past the end. */
        check("truncated object rejected", !vc::Json::parse(R"({"a":1)").has_value());
        check("truncated array rejected", !vc::Json::parse("[1,").has_value());
        check("lone minus rejected", !vc::Json::parse("-").has_value());
        check("trailing junk rejected", !vc::Json::parse("1 2").has_value());
        /* A pathologically long number is rejected rather than mis-parsed. */
        {
            const std::string huge = "1" + std::string(600, '0');
            check("over-long number rejected", !vc::Json::parse(huge).has_value());
        }
    }

} // namespace

int main()
{
    std::printf("VeriConnect core self-test\n==========================\n");
    test_sha256();
    test_hmac();
    test_base64();
    test_base64_strict();
    test_json();
    test_json_numbers();
    test_url();
    test_parse_uint();
    test_url_port();
    test_ini();
    test_adapter_roundtrip();
    test_ws_upgrade();
    test_ws_recv();
    test_ws_send();
    test_http();
    test_relay();
    test_logging();
    std::printf("==========================\n");
    if (g_failed)
    {
        std::printf("%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("All checks passed.\n");
    return 0;
}
