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
     * WebSocket framing, driven by a ScriptedWire instead of a network. None
     * of this code had any coverage before the transport seam existed.
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

    /* A wire that has already answered the upgrade, with the handshake
     * request cleared so outgoing() shows only what the test triggers. */
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
            /* A ping is answered with a pong carrying the same payload, and
             * never surfaces to the caller. */
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
            /* An orderly close mid-message is an error, not a truncated
             * message silently returned as success. */
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
            /* Masking must actually vary: the same payload twice must not
             * produce identical bytes on the wire. */
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
            /* Over the fragment size the message is split, first frame
             * carrying the opcode and only the last one FIN. */
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

} // namespace

int main()
{
    std::printf("VeriConnect core self-test\n==========================\n");
    test_sha256();
    test_hmac();
    test_base64();
    test_json();
    test_url();
    test_ini();
    test_adapter_roundtrip();
    test_ws_upgrade();
    test_ws_recv();
    test_ws_send();
    std::printf("==========================\n");
    if (g_failed)
    {
        std::printf("%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("All checks passed.\n");
    return 0;
}
