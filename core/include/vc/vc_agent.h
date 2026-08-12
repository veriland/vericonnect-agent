/*
 * vc_agent.h - the reusable agent core: loads settings + adapters and
 * pumps the relay listener, dispatching incoming commands to adapters.
 *
 * The Windows service, a future Linux daemon and the console test app
 * all call vc_agent_run; only process hosting differs per platform.
 */
#ifndef VC_AGENT_H
#define VC_AGENT_H

#include "vc_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vc_agent_options {
    const char *settings_path;  /* NULL = <exe dir>/Settings.ini */
    bool        verbose;        /* force console echo + TRACE level */
} vc_agent_options;

/* Blocking; returns when *stop becomes true (or fatal init error). */
int vc_agent_run(const vc_agent_options *opts, volatile bool *stop);

#ifdef __cplusplus
}
#endif

#endif
