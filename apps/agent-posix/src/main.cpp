/*
 * vc-agent - VeriConnect Agent, POSIX host (Linux/macOS).
 *
 * Usage:
 *   vc-agent                       run in the foreground (daemon mode)
 *   vc-agent --console             run in the foreground, verbose
 *   vc-agent --settings <path>     use a specific Settings.ini
 *   vc-agent --help
 *
 * The portable agent logic lives in core (vc::agent::run); this file is only
 * the POSIX process host. It runs in the foreground on purpose so that a
 * service manager (systemd on Linux, launchd on macOS) owns the process
 * lifecycle. SIGTERM/SIGINT trigger a clean shutdown; SIGPIPE is ignored so a
 * peer dropping the TLS connection cannot kill the process.
 */
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "vc/vc_agent.h"

namespace
{

    volatile bool g_stop = false;

    void sig_handler(int)
    {
        g_stop = true;
    }

    void install_signal_handlers()
    {
        std::signal(SIGINT, sig_handler);
        std::signal(SIGTERM, sig_handler);
        std::signal(SIGPIPE, SIG_IGN); /* never die on a write to a dropped peer */
    }

    void usage()
    {
        std::printf("vc-agent - VeriConnect Agent (POSIX host)\n"
                    "\n"
                    "  vc-agent [--console] [--settings <path>]\n"
                    "\n"
                    "  (no options)        Run in the foreground (service/daemon mode).\n"
                    "  --console, -c       Run in the foreground with verbose output.\n"
                    "  --settings <path>   Settings.ini to use (default: next to the binary).\n"
                    "  --help, -h          Show this help.\n");
    }

    const char* arg_value(int argc, char** argv, std::string_view name)
    {
        for (int i = 0; i < argc - 1; i++)
            if (name == argv[i]) return argv[i + 1];
        return nullptr;
    }

    bool arg_flag(int argc, char** argv, std::string_view a, std::string_view b)
    {
        for (int i = 1; i < argc; i++)
            if (a == argv[i] || (!b.empty() && b == argv[i])) return true;
        return false;
    }

} // namespace

int main(int argc, char** argv)
{
    if (arg_flag(argc, argv, "--help", "-h"))
    {
        usage();
        return 0;
    }

    install_signal_handlers();

    vc::agent::Options opts;
    if (const char* sp = arg_value(argc, argv, "--settings")) opts.settings_path = sp;
    opts.verbose = arg_flag(argc, argv, "--console", "-c");

    std::printf("VeriConnect Agent starting (%s mode) - send SIGTERM/SIGINT to stop\n",
                opts.verbose ? "console" : "service");
    std::fflush(stdout);

    return vc::agent::run(opts, [] { return g_stop; }) ? 0 : 1;
}
