/*
 * vc_impersonate.h - run a single adapter command under a caller-supplied
 * user identity ("UserCredentials" in the command JSON).
 *
 * The agent normally executes commands as its own service account. When a
 * command carries UserCredentials, the host impersonates that user for the
 * duration of the adapter call and reverts immediately afterwards.
 *
 * Impersonation is applied to the CURRENT THREAD only, so it is safe in the
 * agent's single-threaded, synchronous dispatch path and never leaks onto
 * other threads.
 *
 * Platform support:
 *   Windows  - LogonUser + ImpersonateLoggedOnUser (real).
 *   POSIX    - returns VC_E_UNSUPPORTED (no silent fallback to the
 *              service account).
 */
#ifndef VC_IMPERSONATE_H
#define VC_IMPERSONATE_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque per-platform impersonation context (holds a logon token). */
typedef struct vc_impersonation vc_impersonation;

/*
 * Begin impersonating `user` on the calling thread.
 *   user     : account name (UTF-8), required.
 *   domain   : domain or machine name (UTF-8); NULL, empty or "." = local.
 *   password : plaintext password (UTF-8); may be empty.
 *
 * On success returns VC_OK and *out receives a context that MUST be passed
 * to vc_impersonate_end to revert. On failure returns an error code and
 * *out is NULL.
 *
 * If `err` is non-NULL, *err receives a short, human-readable message that
 * is SAFE TO LOG (it never contains the password); free it with vc_free.
 */
int  vc_impersonate_begin(const char *user, const char *domain,
                          const char *password, vc_impersonation **out,
                          char **err);

/* Revert to the process identity and release the context. NULL = no-op. */
void vc_impersonate_end(vc_impersonation *imp);

#ifdef __cplusplus
}
#endif

#endif /* VC_IMPERSONATE_H */
