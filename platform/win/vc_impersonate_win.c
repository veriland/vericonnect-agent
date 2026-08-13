/*
 * Windows impersonation backend.
 *
 * Logs the caller-supplied user on (LogonUser) and impersonates them on the
 * current thread (ImpersonateLoggedOnUser) for the duration of one adapter
 * command. Reverted by vc_impersonate_end (RevertToSelf).
 *
 * The host process must hold SE_IMPERSONATE_NAME - LocalSystem,
 * NetworkService and most service accounts do by default.
 */
#include "vc/vc_impersonate.h"

#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>

struct vc_impersonation {
    HANDLE token;
};

static wchar_t *utf8_to_wide(const char *s)
{
    if (!s) return NULL;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *w = vc_alloc((size_t)wlen * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wlen);
    return w;
}

static char *msg_with_code(const char *fmt, DWORD code)
{
    char buf[128];
    snprintf(buf, sizeof buf, fmt, (unsigned long)code);
    return vc_strdup(buf);
}

int vc_impersonate_begin(const char *user, const char *domain,
                         const char *password, vc_impersonation **out,
                         char **err)
{
    if (out) *out = NULL;
    if (err) *err = NULL;

    if (!user || !user[0]) {
        if (err) *err = vc_strdup("Missing username");
        return VC_E_INVALID_ARG;
    }

    /* "." or empty domain means a local account on this machine. */
    bool local = !(domain && domain[0]) || strcmp(domain, ".") == 0;

    wchar_t *wuser = utf8_to_wide(user);
    wchar_t *wdom  = local ? NULL : utf8_to_wide(domain);
    wchar_t *wpass = utf8_to_wide(password ? password : "");
    if (!wuser || !wpass || (!local && !wdom)) {
        vc_free(wuser); vc_free(wdom); vc_free(wpass);
        if (err) *err = vc_strdup("Credential encoding failed");
        return VC_E_NOMEM;
    }

    /* Prefer an interactive-quality token; fall back to a network logon
     * only when the account lacks the "log on locally" right. */
    const DWORD types[2] = { LOGON32_LOGON_INTERACTIVE, LOGON32_LOGON_NETWORK };
    HANDLE token = NULL;
    DWORD  last = 0;
    BOOL   ok = FALSE;
    for (int i = 0; i < 2 && !ok; i++) {
        ok = LogonUserW(wuser, wdom ? wdom : L".", wpass,
                        types[i], LOGON32_PROVIDER_DEFAULT, &token);
        if (!ok) {
            last = GetLastError();
            if (last != ERROR_LOGON_TYPE_NOT_GRANTED) break;
        }
    }

    /* Scrub the plaintext password copy before releasing it. */
    if (wpass) SecureZeroMemory(wpass, (wcslen(wpass) + 1) * sizeof(wchar_t));
    vc_free(wuser); vc_free(wdom); vc_free(wpass);

    if (!ok) {
        if (err) *err = msg_with_code("Logon failed (Win32 error %lu)", last);
        return VC_E_FAIL;
    }

    if (!ImpersonateLoggedOnUser(token)) {
        DWORD e = GetLastError();
        CloseHandle(token);
        if (err) *err = msg_with_code("Impersonation failed (Win32 error %lu)", e);
        return VC_E_FAIL;
    }

    vc_impersonation *imp = vc_alloc(sizeof *imp);
    if (!imp) {
        RevertToSelf();
        CloseHandle(token);
        if (err) *err = vc_strdup("Out of memory");
        return VC_E_NOMEM;
    }
    imp->token = token;
    if (out) *out = imp;
    return VC_OK;
}

void vc_impersonate_end(vc_impersonation *imp)
{
    if (!imp) return;
    RevertToSelf();
    if (imp->token) CloseHandle(imp->token);
    vc_free(imp);
}
