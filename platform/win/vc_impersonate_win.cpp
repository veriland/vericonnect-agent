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
    int active;   /* marker: RevertToSelf owed. The logon token is closed
                     immediately after ImpersonateLoggedOnUser, mirroring
                     the Delphi TBaseCommand.Impersonate finally block. */
};

static wchar_t *utf8_to_wide(const char *s)
{
    if (!s) return NULL;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *w = static_cast<wchar_t*>(vc_alloc((size_t)wlen * sizeof(wchar_t)));
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

    /* Match the Delphi TBaseCommand.Impersonate exactly: username, domain
     * and password are passed to LogonUser as-is (domain may be empty),
     * with LOGON32_LOGON_INTERACTIVE / LOGON32_PROVIDER_DEFAULT and no
     * fallback logon type. */
    wchar_t *wuser = utf8_to_wide(user);
    wchar_t *wdom  = utf8_to_wide(domain   ? domain   : "");
    wchar_t *wpass = utf8_to_wide(password ? password : "");
    if (!wuser || !wdom || !wpass) {
        vc_free(wuser); vc_free(wdom); vc_free(wpass);
        if (err) *err = vc_strdup("Credential encoding failed");
        return VC_E_NOMEM;
    }

    HANDLE token = NULL;
    BOOL   ok = LogonUserW(wuser, wdom, wpass,
                           LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT,
                           &token);
    DWORD  last = ok ? 0 : GetLastError();

    /* Scrub the plaintext password copy before releasing it. */
    if (wpass) SecureZeroMemory(wpass, (wcslen(wpass) + 1) * sizeof(wchar_t));
    vc_free(wuser); vc_free(wdom); vc_free(wpass);

    if (!ok) {
        if (err) *err = msg_with_code("Logon failed (Win32 error %lu)", last);
        return VC_E_FAIL;
    }

    BOOL imp_ok = ImpersonateLoggedOnUser(token);
    DWORD imp_err = imp_ok ? 0 : GetLastError();
    CloseHandle(token);   /* as in the Delphi finally, once impersonating */

    if (!imp_ok) {
        if (err) *err = msg_with_code("Impersonation failed (Win32 error %lu)", imp_err);
        return VC_E_FAIL;
    }

    vc_impersonation *imp = static_cast<vc_impersonation*>(vc_alloc(sizeof *imp));
    if (!imp) {
        RevertToSelf();
        if (err) *err = vc_strdup("Out of memory");
        return VC_E_NOMEM;
    }
    imp->active = 1;
    if (out) *out = imp;
    return VC_OK;
}

void vc_impersonate_end(vc_impersonation *imp)
{
    if (!imp) return;
    RevertToSelf();
    vc_free(imp);
}
