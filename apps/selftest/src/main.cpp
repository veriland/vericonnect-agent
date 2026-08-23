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
    std::printf("==========================\n");
    if (g_failed)
    {
        std::printf("%d check(s) FAILED\n", g_failed);
        return 1;
    }
    std::printf("All checks passed.\n");
    return 0;
}
