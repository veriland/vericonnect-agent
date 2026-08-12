/*
 * vc-agent - VeriConnect Agent, POSIX host (Linux/macOS).
 *
 * Usage:
 *   vc-agent                       run in the foreground (daemon mode)
 *   vc-agent --console             run in the foreground, verbose
 *   vc-agent --settings <path>     use a specific Settings.ini
 *   vc-agent --help
 *
 * The portable agent logic lives in core (vc_agent_run); this file is
 * only the POSIX process host. It runs in the foreground on purpose so
 * that a service manager (systemd on Linux, launchd on macOS) owns the
 * process lifecycle - see dist/ for sample unit files. SIGTERM/SIGINT
 * trigger a clean shutdown; SIGPIPE is ignored so a peer dropping the
 * TLS connection cannot kill the process.
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "vc/vc_agent.h"

static volatile bool g_stop = false;

static void sig_handler(int sig) { (void)sig; g_stop = true; }

static void install_signal_handlers(void)
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);   /* never die on a write to a dropped peer */
}

static void usage(void)
{
    printf(
        "vc-agent - VeriConnect Agent (POSIX host)\n"
        "\n"
        "  vc-agent [--console] [--settings <path>]\n"
        "\n"
        "  (no options)        Run in the foreground (service/daemon mode).\n"
        "  --console, -c       Run in the foreground with verbose output.\n"
        "  --settings <path>   Settings.ini to use (default: next to the binary).\n"
        "  --help, -h          Show this help.\n"
        "\n"
        "Run under systemd (Linux) or launchd (macOS); see dist/ for units.\n");
}

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 0; i < argc - 1; i++)
        if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

static bool arg_flag(int argc, char **argv, const char *a, const char *b)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], a) || (b && !strcmp(argv[i], b))) return true;
    return false;
}

int main(int argc, char **argv)
{
    if (arg_flag(argc, argv, "--help", "-h")) { usage(); return 0; }

    install_signal_handlers();

    vc_agent_options opts;
    memset(&opts, 0, sizeof opts);
    opts.settings_path = arg_value(argc, argv, "--settings");
    opts.verbose = arg_flag(argc, argv, "--console", "-c");

    printf("VeriConnect Agent starting (%s mode) - send SIGTERM/SIGINT to stop\n",
           opts.verbose ? "console" : "service");
    fflush(stdout);

    return vc_agent_run(&opts, &g_stop) == 0 ? 0 : 1;
}
