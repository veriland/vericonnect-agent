#include "vc/vc_adapter.h"
#include "vc/vc_fs.h"
#include "vc/vc_json.h"
#include "vc/vc_log.h"
#include "vc/vc_impersonate.h"

#include <cctype>
#include <cstring>

namespace vc
{
    namespace
    {
#if defined(_WIN32)
        constexpr const char* kAdapterExt = ".dll";
#elif defined(__APPLE__)
        constexpr const char* kAdapterExt = ".dylib";
#else
        constexpr const char* kAdapterExt = ".so";
#endif

        bool iends_with(std::string_view s, std::string_view suffix) noexcept
        {
            if (s.size() < suffix.size()) return false;
            std::string_view tail = s.substr(s.size() - suffix.size());
            for (std::size_t i = 0; i < tail.size(); i++)
                if (std::tolower(static_cast<unsigned char>(tail[i])) !=
                    std::tolower(static_cast<unsigned char>(suffix[i])))
                    return false;
            return true;
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

        std::string json_error(int code, std::string_view desc)
        {
            Json root = Json::object();
            root.set("StatusCode", Json::number(code));
            root.set("StatusDescription", Json::string(std::string(desc)));
            return root.dump();
        }
    } // namespace

    std::optional<Adapter> Adapter::load(const std::string& path)
    {
        std::optional<DynLib> lib = DynLib::open(path);
        if (!lib) return std::nullopt;

        auto run = reinterpret_cast<vc_adapter_run_fn>(lib->symbol("RunAdapterCommand"));
        auto fr = reinterpret_cast<vc_adapter_free_fn>(lib->symbol("FreeAdapterString"));
        auto info = reinterpret_cast<vc_adapter_info_fn>(lib->symbol("GetAdapterInfo"));
        if (!run || !fr) return std::nullopt;

        Adapter ad;
        ad.lib_ = std::move(*lib);
        ad.run_ = run;
        ad.free_ = fr;
        ad.info_ = info;
        ad.path_ = path;

        if (info)
        {
            if (const char* raw = info())
            {
                Result<Json> root = Json::parse(raw);
                if (root) ad.id_ = std::string(root->get_str("id", ""));
            }
        }
        if (ad.id_.empty()) ad.id_ = path;
        return ad;
    }

    std::string Adapter::run(const std::string& request_json) const
    {
        char* raw = run_(request_json.c_str());
        if (!raw) return {};
        std::string result(raw);
        free_(raw);
        return result;
    }

    Status AdapterRegistry::load(const std::string& dir)
    {
        Result<std::vector<std::string>> names = fs::list_files(dir);
        if (!names) return std::unexpected(Error::NotFound);

        for (const std::string& n : *names)
        {
            if (!iends_with(n, kAdapterExt)) continue;
            std::optional<Adapter> ad = Adapter::load(fs::join(dir, n));
            if (ad)
            {
                log::message(log::Level::Info, "Loaded adapter '{}' from {}", ad->id(), ad->path());
                adapters_.push_back(std::move(*ad));
            }
        }
        return adapters_.empty() ? std::unexpected(Error::NotFound) : Status{};
    }

    const Adapter* AdapterRegistry::find(std::string_view id) const
    {
        for (const Adapter& ad : adapters_)
            if (iequals(ad.id(), id)) return &ad;
        return nullptr;
    }

    std::string AdapterRegistry::dispatch(const std::string& request_json)
    {
        /* Parse once: route on "Adapter" and read optional "UserCredentials". */
        const Adapter* ad = nullptr;
        std::string_view imp_user, imp_domain, imp_pass;

        Result<Json> root = Json::parse(request_json);
        if (root)
        {
            std::string_view adapter_id = root->get_str("Adapter", "");
            if (!adapter_id.empty()) ad = find(adapter_id);

            /* UserCredentials lives inside "Parameters"; accept it at the top
             * level too for backward compatibility. */
            const Json* params = root->find_ci("Parameters");
            const Json* uc = params ? params->find_ci("UserCredentials") : nullptr;
            if (!uc) uc = root->find_ci("UserCredentials");
            if (uc)
            {
                imp_user = uc->get_str("Username", "");
                imp_domain = uc->get_str("Domain", "");
                imp_pass = uc->get_str("Password", "");
            }
        }
        if (!ad)
        {
            if (adapters_.empty()) return json_error(404, "No adapter available");
            ad = &adapters_.front();
        }

        /* Optionally impersonate the requested user for just this command. Never
         * log the password; the user name is fine to log. */
        std::optional<Impersonation> imp;
        if (!imp_user.empty())
        {
            auto r = Impersonation::begin(imp_user, imp_domain, imp_pass);
            if (!r)
            {
                log::message(log::Level::Error, "Impersonation failed for user '{}': {}", imp_user,
                             r.error().message);
                /* 501 when the platform can't do it, 403 for a rejected logon. */
                return json_error(r.error().code == Error::Unsupported ? 501 : 403,
                                  r.error().message);
            }
            imp = std::move(*r);
            log::message(log::Level::Info, "Impersonating user '{}' for this command", imp_user);
        }

        std::string result = ad->run(request_json);

        /* Revert before returning (RAII). */
        imp.reset();

        if (result.empty()) return json_error(500, "Adapter returned no result");
        return result;
    }
} // namespace vc
