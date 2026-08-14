/*
 * The reusable agent core.
 *
 * Loads settings + adapters, then runs the Azure Relay listener, translating
 * each relay request into an adapter dispatch. The OS-specific hosts just call
 * vc::agent::run.
 */
#include "vc/vc_agent.h"
#include "vc/vc_settings.h"
#include "vc/vc_adapter.h"
#include "vc/vc_relay.h"
#include "vc/vc_log.h"
#include "vc/vc_fs.h"
#include "vc/vc_json.h"

#include <cctype>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vc::agent
{
    namespace
    {
        std::string_view as_view(std::span<const std::uint8_t> b)
        {
            return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
        }

        bool iequals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        /* Relay -> adapter bridge. */
        bool on_request(AdapterRegistry& registry, const RelayRequest& req, RelayResponse& resp)
        {
            log::message(log::Level::Info, "REQUEST [{}] {} {} ({} byte body)",
                         req.id, req.method, req.target, req.body.size());

            /* The command JSON is the request body (UTF-8). */
            std::string result = registry.dispatch(std::string(as_view(req.body)));

            log::message(log::Level::Succ, "ADAPTER RESULT [{}]: {}", req.id, result);

            /* Map the adapter's StatusCode/StatusDescription onto the HTTP response. */
            int status = 200;
            std::string desc = "OK";
            if (Result<Json> root = Json::parse(result))
            {
                status = static_cast<int>(root->get_num("StatusCode", 200));
                desc = std::string(root->get_str("StatusDescription", "OK"));
            }
            resp.status_code = status;
            resp.status_desc = desc;
            resp.content_type = "application/json";
            resp.body.assign(reinterpret_cast<const std::uint8_t*>(result.data()),
                             reinterpret_cast<const std::uint8_t*>(result.data() + result.size()));
            return true;
        }

        void on_event(std::string_view event, int code, std::string_view desc)
        {
            if (iequals(event, "CONNECT_FAILED") || iequals(event, "RESPONSE_ERROR") ||
                iequals(event, "RENDEZVOUS_FAILED"))
                log::message(log::Level::Error, "RELAY {} [{}]: {}", event, code, desc);
            else if (iequals(event, "DISCONNECTED"))
                log::message(log::Level::Warn, "RELAY {} [{}]: {}", event, code, desc);
            else
                log::message(log::Level::Info, "RELAY {} [{}]: {}", event, code, desc);
        }

        std::string resolve_adapters_dir(const std::string& configured)
        {
            std::string d = configured.empty() ? "." : configured;
            bool absolute = d[0] == '/' || d[0] == '\\' || (d.size() > 1 && d[1] == ':');
            if (absolute) return d;
            if (std::optional<std::string> exe = fs::exe_dir()) return fs::join(*exe, d);
            return d;
        }
    } // namespace

    Status run(const Options& opts, const std::function<bool()>& stop_requested)
    {
        std::string settings_path = opts.settings_path.empty()
                                        ? Settings::default_path()
                                        : opts.settings_path;

        Result<Settings> loaded = Settings::load(settings_path);
        Settings settings = loaded.value_or(Settings{});
        if (!loaded) settings.relay.token_ttl_seconds = 3600;

        /* logging first so everything after is captured */
        if (opts.verbose)
        {
            settings.logging.console = true;
            settings.logging.level = log::Level::Trace;
        }
        if (std::optional<std::string> dir = fs::exe_dir())
            settings.logging.file_path = fs::join(*dir, "VeriConnect.log");
        log::init(settings.logging);

        log::message(log::Level::Info, "VeriConnect agent starting");
        if (!loaded)
            log::message(log::Level::Warn, "Settings file not found ({}); using defaults",
                         settings_path);
        else
            log::message(log::Level::Info, "Settings loaded from {}", settings_path);
        log::message(log::Level::Info, "Connection: ns={} hc={} keyName={}",
                     settings.relay.namespace_host, settings.relay.hybrid_connection,
                     settings.relay.key_name);

        std::string adapters_dir = resolve_adapters_dir(settings.adapters_dir);
        log::message(log::Level::Info, "Loading adapters from {}", adapters_dir);

        AdapterRegistry registry;
        if (!registry.load(adapters_dir))
            log::message(log::Level::Warn, "No adapters loaded from {}", adapters_dir);

        RelayCallbacks cb;
        cb.on_request = [&registry](const RelayRequest& req, RelayResponse& resp)
        {
            return on_request(registry, req, resp);
        };
        cb.on_event = on_event;

        Status rc;
        if (settings.relay.namespace_host.empty() || settings.relay.hybrid_connection.empty())
        {
            log::message(log::Level::Error, "Connection settings incomplete; cannot start listener");
            rc = std::unexpected(Error::InvalidArg);
        }
        else
        {
            rc = relay_listen(settings.relay, cb, stop_requested);
        }

        log::message(log::Level::Info, "VeriConnect agent stopped");
        log::shutdown();
        return rc;
    }
} // namespace vc::agent
