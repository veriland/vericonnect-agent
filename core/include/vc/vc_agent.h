/*
 * vc_agent.h - the reusable agent core: loads settings + adapters and pumps
 * the relay listener, dispatching incoming commands to adapters.
 *
 * The Windows service, a Linux daemon and the console test app all call
 * vc::agent::run; only process hosting differs per platform.
 */
#ifndef VC_AGENT_H
#define VC_AGENT_H

#include "vc_common.h"

#ifdef __cplusplus

#include <functional>
#include <string>

namespace vc::agent
{
    struct Options
    {
        std::string settings_path; /* empty = <exe dir>/Settings.ini */
        bool verbose = false; /* force console echo + TRACE level */
    };

    /* Blocking; returns when stop_requested() becomes true (or a fatal init
 * error). */
    Status run(const Options& opts, const std::function<bool()>& stop_requested);
} // namespace vc::agent

#endif /* __cplusplus */

#endif
