/*
 * vc_settings.h - agent settings (Settings.ini).
 */
#ifndef VC_SETTINGS_H
#define VC_SETTINGS_H

#include "vc_common.h"
#include "vc_log.h"
#include "vc_relay.h"

#ifdef __cplusplus

#include <string>

namespace vc
{
    struct Settings
    {
        RelayConfig relay;              /* [Connection] */
        log::Config logging;            /* [Logging]    */
        std::string adapters_dir = "."; /* [Adapters] Directory */

        /* Load settings; a missing file yields Error::NotFound (use value_or with
         * a default-constructed Settings for defaults). */
        [[nodiscard]] static Result<Settings> load(const std::string& path);
        [[nodiscard]] Status save(const std::string& path) const;

        /* Default path: <exe dir>/Settings.ini. */
        static std::string default_path();
    };
} // namespace vc

#endif /* __cplusplus */

#endif
