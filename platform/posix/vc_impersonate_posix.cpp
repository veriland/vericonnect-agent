/*
 * POSIX impersonation backend - intentionally unsupported.
 *
 * Password-based per-command impersonation has no safe, portable POSIX
 * equivalent: there is no LogonUser, credential verification needs PAM,
 * and setuid/setgid are process-wide and require root. Rather than
 * silently run the command as the service account (a security surprise),
 * a request carrying UserCredentials is rejected with a clear error.
 *
 * See vc_impersonate_win.c for the Windows implementation.
 */
#include "vc/vc_impersonate.h"

int vc_impersonate_begin(const char *user, const char *domain,
                         const char *password, vc_impersonation **out,
                         char **err)
{
    (void)user; (void)domain; (void)password;
    if (out) *out = NULL;
    if (err)
        *err = vc_strdup(
            "Impersonation via UserCredentials is not supported on this platform");
    return VC_E_UNSUPPORTED;
}

void vc_impersonate_end(vc_impersonation *imp)
{
    (void)imp;
}
