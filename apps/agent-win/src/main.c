/*
 * vc-agent - VeriConnect Agent, Windows service host.
 *
 * Usage:
 *   vc-agent --install     install the Windows service
 *   vc-agent --uninstall   remove the Windows service
 *   vc-agent --console     run in the foreground (verbose)
 *   vc-agent               run as a service (invoked by the SCM)
 *
 * The portable agent logic lives in core (vc_agent_run); this file is
 * only the Windows Service Control Manager glue.
 */
#include <windows.h>
#include <stdio.h>

#include "vc/vc_agent.h"
#include "vc/vc_log.h"

#define SVC_NAME  L"VeriConnectAgent"
#define SVC_DISP  L"VeriConnect Agent"
#define SVC_DESC  L"VeriConnect Agent Service"

static SERVICE_STATUS_HANDLE g_status_handle;
static SERVICE_STATUS        g_status;
static volatile bool         g_stop = false;

/* ---------------------------------------------------------------- */
/* Install / uninstall                                                */
/* ---------------------------------------------------------------- */

static int svc_install(void)
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) {
        fprintf(stderr, "GetModuleFileName failed (%lu)\n", GetLastError());
        return 1;
    }

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        fprintf(stderr, "OpenSCManager failed (%lu). Run as administrator.\n",
                GetLastError());
        return 1;
    }

    SC_HANDLE svc = CreateServiceW(
        scm, SVC_NAME, SVC_DISP,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        path, NULL, NULL, NULL, NULL, NULL);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_EXISTS) {
            fprintf(stderr, "Service already installed.\n");
            return 1;
        }
        fprintf(stderr, "CreateService failed (%lu)\n", err);
        return 1;
    }

    SERVICE_DESCRIPTIONW desc = { (LPWSTR)SVC_DESC };
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    /* restart on failure, like a resilient agent should */
    SC_ACTION actions[3] = {
        { SC_ACTION_RESTART, 10000 },
        { SC_ACTION_RESTART, 30000 },
        { SC_ACTION_RESTART, 60000 },
    };
    SERVICE_FAILURE_ACTIONSW fa;
    memset(&fa, 0, sizeof fa);
    fa.dwResetPeriod = 86400;
    fa.cActions = 3;
    fa.lpsaActions = actions;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);

    printf("Service '%ls' installed.\n", SVC_DISP);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int svc_uninstall(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) {
        fprintf(stderr, "OpenSCManager failed (%lu)\n", GetLastError());
        return 1;
    }
    SC_HANDLE svc = OpenServiceW(scm, SVC_NAME, SERVICE_STOP | DELETE);
    if (!svc) {
        fprintf(stderr, "Service not found (%lu)\n", GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }
    SERVICE_STATUS st;
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    if (DeleteService(svc))
        printf("Service '%ls' removed.\n", SVC_DISP);
    else
        fprintf(stderr, "DeleteService failed (%lu)\n", GetLastError());
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

/* ---------------------------------------------------------------- */
/* Service mode                                                       */
/* ---------------------------------------------------------------- */

static void set_state(DWORD state, DWORD win32_exit)
{
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = win32_exit;
    g_status.dwControlsAccepted =
        (state == SERVICE_START_PENDING) ? 0
        : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_status.dwWaitHint = (state == SERVICE_START_PENDING ||
                           state == SERVICE_STOP_PENDING) ? 30000 : 0;
    SetServiceStatus(g_status_handle, &g_status);
}

static DWORD WINAPI svc_ctrl_handler(DWORD ctrl, DWORD event_type,
                                     LPVOID event_data, LPVOID ctx)
{
    (void)event_type; (void)event_data; (void)ctx;
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        set_state(SERVICE_STOP_PENDING, NO_ERROR);
        g_stop = true;
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static void WINAPI svc_main(DWORD argc, LPWSTR *argv)
{
    (void)argc; (void)argv;
    g_status_handle = RegisterServiceCtrlHandlerExW(SVC_NAME,
                                                    svc_ctrl_handler, NULL);
    if (!g_status_handle) return;

    set_state(SERVICE_START_PENDING, NO_ERROR);
    set_state(SERVICE_RUNNING, NO_ERROR);

    vc_agent_options opts;
    memset(&opts, 0, sizeof opts);
    opts.verbose = false;
    int rc = vc_agent_run(&opts, &g_stop);

    set_state(SERVICE_STOPPED, rc == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR);
}

/* ---------------------------------------------------------------- */
/* Console mode                                                       */
/* ---------------------------------------------------------------- */

static BOOL WINAPI console_ctrl(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}

static int run_console(const char *settings_path)
{
    SetConsoleCtrlHandler(console_ctrl, TRUE);
    printf("VeriConnect Agent (console mode) - Ctrl+C to stop\n");
    vc_agent_options opts;
    memset(&opts, 0, sizeof opts);
    opts.settings_path = settings_path;
    opts.verbose = true;
    return vc_agent_run(&opts, &g_stop) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (_stricmp(argv[1], "--install") == 0)   return svc_install();
        if (_stricmp(argv[1], "--uninstall") == 0) return svc_uninstall();
        if (_stricmp(argv[1], "--console") == 0)
            return run_console(argc > 2 ? argv[2] : NULL);
        fprintf(stderr,
            "Usage: vc-agent [--install | --uninstall | --console [settings.ini]]\n");
        return 1;
    }

    SERVICE_TABLE_ENTRYW table[] = {
        { (LPWSTR)SVC_NAME, svc_main },
        { NULL, NULL }
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            fprintf(stderr,
                "Not started by the Service Control Manager.\n"
                "Use --console to run interactively, --install to install.\n");
            return 1;
        }
        return (int)err;
    }
    return 0;
}
