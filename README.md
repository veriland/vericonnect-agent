# VeriConnect Agent

The VeriConnect Agent is the on-premise component of **VeriConnect**,
written in modern **C++23**. It receives commands from a server through
**Azure Relay Hybrid Connections** and executes them via pluggable
**adapter** shared libraries.

The Hybrid Connections listener protocol is implemented from scratch —
SAS token generation, WebSocket client, control channel + rendezvous
handling, proactive token renewal and automatic reconnect — with no
third-party dependencies.

## Role

The Agent is the **on-premise** piece of a larger integration story. It
lets Microsoft **Dynamics 365 Finance & Operations (D365FO)** and
**Customer Engagement (CE)** reach resources that live behind a customer's
firewall: the cloud application sends a command over Azure Relay, the agent
executes it locally through the matching adapter and returns the result —
no inbound firewall ports and no VPN required.

Every capability is an **adapter**. The **FileSystem** adapter ships today.
Planned adapters extend the agent to more back ends — **MSSQL**, **Azure
SQL** and other SQL databases, and generic **HTTP/API** calls — each a
drop-in `vc-adapter-<name>` library speaking the same ABI. The agent core
stays fixed; the set of adapters grows.

## Layout

```
core/                 Portable C++23 - no platform code. The reusable heart.
  include/vc/         Public headers
  src/                JSON, base64, SHA-256/HMAC, URL, INI, logging,
                      SAS tokens, WebSocket client, HTTP client,
                      Azure Relay listener, adapter loader, settings,
                      and the agent run loop (vc_agent_run)
platform/
  win/                Winsock, SChannel TLS, LoadLibrary, Win32 FS (UTF-8<->UTF-16)
  posix/              BSD sockets, OpenSSL TLS, dlopen, POSIX FS  (Linux/macOS)
adapters/
  filesystem/         FileSystem adapter DLL/.so: ListFolder, CreateFolder,
                      CreateFile, ReadFile, DeleteFile, MoveFile
apps/
  agent-win/          vc-agent.exe   - Windows service host (SCM glue only)
  agent-posix/        vc-agent       - Linux/macOS host (systemd/launchd glue)
  testapp/            vc-test.exe    - verbose console tester (listen + send)
  selftest/           vc-selftest.exe- unit checks (RFC vectors, round trips)
config/
  Settings.ini        Sample settings
```

Core headers keep the `.h` suffix and public C++ symbols; the
implementation lives in `.cpp` alongside them. **Portability rule:**
everything in `core/` compiles anywhere; only
`platform/` differs per OS. The Windows service (`apps/agent-win`), the
Linux/macOS host (`apps/agent-posix`) and the console test app all call
the same `vc_agent_run()`; only process hosting differs. The
`platform/posix/` layer is OpenSSL based (see the macOS build notes
below) and not yet CI-tested.

## Building (Windows)

Requires an MSVC toolchain with C++23 support and CMake ≥ 3.20. You do
**not** need the full Visual Studio IDE — the free standalone [Build Tools
for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)
("Desktop development with C++" workload) are enough, and bundle CMake. A
recent Visual Studio 2022 (17.x) toolset is needed for the C++23 standard;
earlier toolsets (VS 2019 and older) do not support C++23 and will not
build the agent.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Binaries land in `build/bin/Release/`. Run `vc-selftest.exe` to verify
the build.

**Building on Linux/macOS** (needs a C++23 compiler — GCC 13+ or Clang
16+ — and OpenSSL dev headers):

```
cmake -S . -B build && cmake --build build
```

On macOS install OpenSSL first and point CMake at it (Homebrew keeps it
off the default path):

```
brew install openssl@3
cmake -S . -B build -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
cmake --build build
```

This produces `vc-agent`, `vc-test` and `vc-selftest` in `build/bin/`.
To run `vc-agent` as a managed service, see the sample systemd and
launchd units in `apps/agent-posix/dist/`.

## Configuration

`Settings.ini` sits next to the executable:

```ini
[Connection]
AccessKey=<SAS key>
AccessKeyName=saspolicy
Namespace=yournamespace.servicebus.windows.net
HybridConnection=yourhybridconnection

[Logging]
LogLevel=LOG_DEBUG
MaxRotateFiles=10
MaxFileSizeInMB=10
...

[Adapters]
Directory=.        ; where adapter libraries live, relative to the exe
```

Logs go to console (in console modes) and `VeriConnect.log` next to the
exe, with size-based rotation.

## Running

**Test console (no install):**

```
vc-test listen                     # connect to Azure Relay now, verbose
vc-test send --command ListFolder --folder C:\Temp
vc-test send --command CreateFile --folder C:\Temp --file a.txt --content "hi" --overwrite
vc-test send --command ReadFile   --folder C:\Temp --file a.txt --overwrite
vc-test send --json @request.json  # raw command JSON pass-through
```

`send` posts through the relay's HTTPS endpoint exactly like the real
server does, so `listen` + `send` gives a full end-to-end test on one
machine.

**Windows service:**

```
vc-agent --install      # then: sc start VeriConnectAgent
vc-agent --uninstall
vc-agent --console      # foreground with verbose output
```

## Adapter ABI

Adapters are shared libraries named `vc-adapter-<name>.dll`
(`libvc-adapter-<name>.so`/`.dylib` on POSIX) exporting:

```c
char*       RunAdapterCommand(const char* request_json); /* returns JSON */
void        FreeAdapterString(char* p);
const char* GetAdapterInfo(void);                        /* static JSON  */
```

All strings crossing the boundary are **UTF-8**. Command shape:

```json
{ "Adapter": "FileSystem", "Command": "CreateFile",
  "Parameters": { "TargetFolder": "C:\\in", "FileName": "a.txt",
                   "FileContent": "...", "OverwriteIfExists": true,
                   "Encoding": "utf-8" } }
```

Responses carry `StatusCode`, `StatusDescription` and optionally `Data`
(`ReadFile` returns the file content base64-encoded in `Data` and then
moves the file into a `COPY` subfolder so a polled folder drains).

## Impersonation (UserCredentials)

By default commands run as the agent's own service account. When the
command's `Parameters` carry a `UserCredentials` object, the host runs
**that one command** as the named user and reverts immediately afterwards
(the same mechanism as the Delphi `TBaseCommand.Impersonate`):

```json
{ "Adapter": "FileSystem", "Command": "CreateFile",
  "Parameters": { "TargetFolder": "C:\\in", "FileName": "a.txt",
                  "UserCredentials": { "Domain": "CORP",
                                       "Username": "svc_files",
                                       "Password": "..." } } }
```

- Impersonation is applied when `Username` is non-empty. `Domain` and
  `Password` are passed to `LogonUser` as given (`Domain` may be empty).
- **Windows:** `LogonUser` (`LOGON32_LOGON_INTERACTIVE`,
  `LOGON32_PROVIDER_DEFAULT`) + `ImpersonateLoggedOnUser`, scoped to the
  dispatch thread, reverted with `RevertToSelf`. The agent's account
  needs the *Impersonate a client* privilege (`SE_IMPERSONATE_NAME`) -
  LocalSystem, NetworkService and most service accounts have it. A failed
  logon returns HTTP `403`.
- **Linux/macOS:** unsupported - the command is rejected with `501`
  rather than running as the service account.
- The password is never written to the log and its transient in-memory
  copy is zeroed after logon. Send commands only over the TLS relay.
- A `UserCredentials` object at the top level of the command is also
  accepted, for backward compatibility.

## Known limitations

- Request bodies larger than ~64 KB arriving over a rendezvous
  connection are read as a single WebSocket message; large *responses*
  are sent over a rendezvous connection automatically.
- WebSocket accept offers (relay-tunnelled WebSockets, as opposed to
  HTTP requests) are logged and ignored.
- Impersonation via `UserCredentials` is implemented on **Windows** only
  (see below). On Linux/macOS a command carrying `UserCredentials` is
  rejected with a "not supported on this platform" error rather than
  silently running as the service account.

## License

VeriConnect is **source-available** under the
[Functional Source License 1.1 (FSL-1.1-Apache-2.0)](LICENSE). You may read,
audit, modify, and use the Software for any purpose other than a Competing
Use (offering it as a hosted or managed service, or embedding it in a product
or service that competes with a commercial offering of the Licensor). Each
version automatically converts to the Apache License 2.0 on the second
anniversary of its release.

For commercial licensing outside these terms, contact legal@veriland.co.uk.

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) —
all commits must be signed off under the [Developer Certificate of Origin](DCO)
(`git commit -s`). To report a security issue privately, see
[SECURITY.md](SECURITY.md).
