/*
 * vc-selftest - unit checks for the portable core.
 * Exit code 0 = all passed.
 */
#include <stdio.h>
#include "vc/vc_sha256.h"
#include "vc/vc_base64.h"
#include "vc/vc_json.h"
#include "vc/vc_url.h"
#include "vc/vc_str.h"
#include "vc/vc_ini.h"
#include "vc/vc_fs.h"
#include "vc/vc_adapter.h"

static int g_failed = 0;

#define CHECK(name, cond) do { \
    if (cond) printf("  ok   %s\n", name); \
    else { printf("  FAIL %s\n", name); g_failed++; } \
} while (0)

static void hex(const uint8_t *d, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", d[i]);
    out[n * 2] = 0;
}

static void test_sha256(void)
{
    printf("SHA-256\n");
    uint8_t dg[32];
    char h[65];

    vc_sha256("abc", 3, dg);
    hex(dg, 32, h);
    CHECK("sha256(\"abc\")", !strcmp(h,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    vc_sha256("", 0, dg);
    hex(dg, 32, h);
    CHECK("sha256(\"\")", !strcmp(h,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    /* long input crossing block boundaries */
    vc_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, dg);
    hex(dg, 32, h);
    CHECK("sha256(56 chars)", !strcmp(h,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

static void test_hmac(void)
{
    printf("HMAC-SHA256 (RFC 4231)\n");
    uint8_t dg[32];
    char h[65];

    /* RFC 4231 test case 2 */
    vc_hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, dg);
    hex(dg, 32, h);
    CHECK("tc2", !strcmp(h,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

    /* RFC 4231 test case 1 */
    uint8_t key[20];
    memset(key, 0x0b, sizeof key);
    vc_hmac_sha256(key, 20, "Hi There", 8, dg);
    hex(dg, 32, h);
    CHECK("tc1", !strcmp(h,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

    /* key longer than a block (RFC 4231 test case 6) */
    uint8_t bigkey[131];
    memset(bigkey, 0xaa, sizeof bigkey);
    vc_hmac_sha256(bigkey, sizeof bigkey,
                   "Test Using Larger Than Block-Size Key - Hash Key First", 54, dg);
    hex(dg, 32, h);
    CHECK("tc6", !strcmp(h,
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

static void test_base64(void)
{
    printf("Base64\n");
    char *e = vc_base64_encode("foobar", 6);
    CHECK("encode", e && !strcmp(e, "Zm9vYmFy"));
    vc_free(e);

    e = vc_base64_encode("foob", 4);
    CHECK("encode pad2", e && !strcmp(e, "Zm9vYg=="));
    vc_free(e);

    size_t n = 0;
    uint8_t *d = vc_base64_decode("Zm9vYmE=", &n);
    CHECK("decode", d && n == 5 && !memcmp(d, "fooba", 5));
    vc_free(d);

    d = vc_base64_decode("!!!", &n);
    CHECK("decode invalid", d == NULL);
}

static void test_json(void)
{
    printf("JSON\n");
    const char *src =
        "{\"Adapter\":\"FileSystem\",\"Command\":\"CreateFile\","
        "\"Parameters\":{\"TargetFolder\":\"C:\\\\tmp\",\"FileName\":\"x.txt\","
        "\"OverwriteIfExists\":true,\"Depth\":-2.5,"
        "\"Unicode\":\"caf\\u00e9 \\ud83d\\ude00\",\"Nothing\":null,"
        "\"List\":[1,2,3]}}";
    vc_json *root = vc_json_parse(src);
    CHECK("parse", root != NULL);
    if (!root) return;

    CHECK("get str", !strcmp(vc_json_get_str(root, "Command", ""), "CreateFile"));
    vc_json *p = vc_json_obj_get_ci(root, "parameters");
    CHECK("ci lookup", p != NULL);
    CHECK("nested str", p && !strcmp(vc_json_get_str(p, "TargetFolder", ""), "C:\\tmp"));
    CHECK("bool", p && vc_json_get_bool(p, "OverwriteIfExists", false));
    CHECK("negative num", p && vc_json_get_num(p, "Depth", 0) == -2.5);
    CHECK("utf8 escape", p && !strcmp(vc_json_get_str(p, "Unicode", ""),
                                      "caf\xC3\xA9 \xF0\x9F\x98\x80"));
    vc_json *list = p ? vc_json_obj_get_ci(p, "List") : NULL;
    CHECK("array size", list && vc_json_array_size(list) == 3);

    char *out = vc_json_write(root);
    CHECK("write", out != NULL);
    vc_json *re = vc_json_parse(out);
    CHECK("round trip", re != NULL);
    vc_json_free(re);
    vc_free(out);
    vc_json_free(root);

    CHECK("reject junk", vc_json_parse("{\"a\":}") == NULL);
    CHECK("reject trailing", vc_json_parse("{} extra") == NULL);
}

static void test_url(void)
{
    printf("URL\n");
    char *e = vc_url_encode("http://ns/path with space&x=1");
    CHECK("encode", e && !strcmp(e,
        "http%3A%2F%2Fns%2Fpath%20with%20space%26x%3D1"));
    vc_free(e);

    vc_url u;
    int rc = vc_url_parse("wss://g1-prod.servicebus.windows.net:443/$hc/x?sb-hc-action=accept&id=abc", &u);
    CHECK("parse rc", rc == VC_OK);
    CHECK("host", !strcmp(u.host, "g1-prod.servicebus.windows.net"));
    CHECK("port", u.port == 443);
    CHECK("path", !strcmp(u.path, "/$hc/x"));
    CHECK("query", u.query && !strcmp(u.query, "sb-hc-action=accept&id=abc"));
    vc_url_free(&u);
}

static void test_ini(void)
{
    printf("INI\n");
    char *dir = vc_fs_exe_dir();
    char *path = vc_fs_join(dir ? dir : ".", "selftest.ini");
    vc_free(dir);

    vc_ini *ini = vc_ini_new();
    vc_ini_set(ini, "Connection", "Namespace", "x.servicebus.windows.net");
    vc_ini_set_int(ini, "Logging", "MaxRotateFiles", 7);
    CHECK("save", vc_ini_save(ini, path) == VC_OK);
    vc_ini_free(ini);

    ini = vc_ini_load(path);
    CHECK("load", ini != NULL);
    CHECK("get", !strcmp(vc_ini_get(ini, "Connection", "Namespace", ""),
                         "x.servicebus.windows.net"));
    CHECK("get int", vc_ini_get_int(ini, "Logging", "MaxRotateFiles", 0) == 7);
    CHECK("default", vc_ini_get_int(ini, "Logging", "Missing", 42) == 42);
    vc_ini_free(ini);
    vc_fs_remove_file(path);
    vc_free(path);
}

static void test_adapter_roundtrip(void)
{
    printf("Adapter round trip (vc-adapter-filesystem)\n");
    char *exe_dir = vc_fs_exe_dir();
    vc_adapter_registry reg;
    if (vc_adapter_registry_load(&reg, exe_dir ? exe_dir : ".") != VC_OK) {
        printf("  skip (adapter dll not found next to vc-selftest)\n");
        vc_free(exe_dir);
        return;
    }

    /* CreateFile then ReadFile in a temp folder next to the exe */
    char *tmp = vc_fs_join(exe_dir, "selftest-fs");
    vc_fs_mkdir(tmp);

    vc_buf req;
    vc_buf_init(&req);
    vc_buf_appendf(&req,
        "{\"Adapter\":\"FileSystem\",\"Command\":\"CreateFile\","
        "\"Parameters\":{\"TargetFolder\":");
    vc_json_escape_str(tmp, &req);
    vc_buf_append_str(&req,
        ",\"FileName\":\"hello.txt\",\"FileContent\":\"hello vericonnect\","
        "\"OverwriteIfExists\":true,\"Encoding\":\"utf-8\"}}");

    char *res = vc_adapter_dispatch(&reg, req.data);
    vc_json *j = vc_json_parse(res);
    CHECK("CreateFile 200", j && vc_json_get_num(j, "StatusCode", 0) == 200);
    vc_json_free(j);
    vc_free(res);
    vc_buf_free(&req);

    /* ListFolder should now see it */
    vc_buf_init(&req);
    vc_buf_append_str(&req,
        "{\"Adapter\":\"FileSystem\",\"Command\":\"ListFolder\",\"Parameters\":{\"TargetFolder\":");
    vc_json_escape_str(tmp, &req);
    vc_buf_append_str(&req, "}}");
    res = vc_adapter_dispatch(&reg, req.data);
    j = vc_json_parse(res);
    CHECK("ListFolder 200", j && vc_json_get_num(j, "StatusCode", 0) == 200);
    vc_json_free(j);
    vc_free(res);
    vc_buf_free(&req);

    /* ReadFile returns base64 and moves the file to COPY */
    vc_buf_init(&req);
    vc_buf_append_str(&req,
        "{\"Adapter\":\"FileSystem\",\"Command\":\"ReadFile\",\"Parameters\":{\"TargetFolder\":");
    vc_json_escape_str(tmp, &req);
    vc_buf_append_str(&req,
        ",\"FileName\":\"hello.txt\",\"OverwriteIfExists\":true}}");
    res = vc_adapter_dispatch(&reg, req.data);
    j = vc_json_parse(res);
    CHECK("ReadFile 200", j && vc_json_get_num(j, "StatusCode", 0) == 200);
    if (j) {
        const char *b64 = vc_json_get_str(j, "Data", "");
        size_t n = 0;
        uint8_t *raw = vc_base64_decode(b64, &n);
        CHECK("ReadFile content", raw && n == 17 && !memcmp(raw, "hello vericonnect", 17));
        vc_free(raw);
    }
    vc_json_free(j);
    vc_free(res);
    vc_buf_free(&req);

    char *copyfile = vc_fs_join(tmp, "COPY\\hello.txt");
    CHECK("moved to COPY", vc_fs_file_exists(copyfile));

    /* cleanup */
    vc_fs_remove_file(copyfile);
    vc_free(copyfile);
    char *copydir = vc_fs_join(tmp, "COPY");
    /* best effort dir removal not in vc_fs API; leave empty dirs */
    vc_free(copydir);

    /* unknown command yields 404 */
    res = vc_adapter_dispatch(&reg,
        "{\"Adapter\":\"FileSystem\",\"Command\":\"Nope\",\"Parameters\":{}}");
    j = vc_json_parse(res);
    CHECK("unknown cmd 404", j && vc_json_get_num(j, "StatusCode", 0) == 404);
    vc_json_free(j);
    vc_free(res);

    /* Impersonation wiring: UserCredentials inside Parameters with a bogus
     * user must fail before the file op runs - never 200/404 from the
     * adapter. Windows -> 403 (logon failed); POSIX -> 501 (unsupported). */
    res = vc_adapter_dispatch(&reg,
        "{\"Adapter\":\"FileSystem\",\"Command\":\"CreateFile\",\"Parameters\":{"
        "\"TargetFolder\":\"C:\\\\\",\"FileName\":\"vc_imp_selftest.txt\","
        "\"UserCredentials\":{\"Domain\":\"\","
        "\"Username\":\"vc_no_such_user_zzz\",\"Password\":\"x\"}}}");
    j = vc_json_parse(res);
    {
        int sc = j ? (int)vc_json_get_num(j, "StatusCode", 0) : 0;
#if defined(_WIN32)
        CHECK("impersonation bad creds -> 403", sc == 403);
#else
        CHECK("impersonation unsupported -> 501", sc == 501);
#endif
    }
    vc_json_free(j);
    vc_free(res);

    vc_free(tmp);
    vc_free(exe_dir);
    vc_adapter_registry_unload(&reg);
}

int main(void)
{
    printf("VeriConnect core self-test\n==========================\n");
    test_sha256();
    test_hmac();
    test_base64();
    test_json();
    test_url();
    test_ini();
    test_adapter_roundtrip();
    printf("==========================\n");
    if (g_failed) {
        printf("%d check(s) FAILED\n", g_failed);
        return 1;
    }
    printf("All checks passed.\n");
    return 0;
}
