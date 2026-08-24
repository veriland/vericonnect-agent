# Design principles

This document is the design rulebook for VeriConnect Agent. It exists so
that "match the surrounding code style" in
[CONTRIBUTING.md](CONTRIBUTING.md) has something concrete behind it, and
so that reviewers and contributors argue about code against a written
standard rather than taste.

## Why not just SOLID

SOLID is sound, but it was written for languages where every abstraction
is a runtime-dispatched class. C++ is not that language: it has a
*physical* structure — headers, translation units, link order, binary
boundaries — that Java and C# do not, and it offers four different ways
to be polymorphic (templates, concepts, virtual functions, function
pointers) with wildly different costs. A rulebook that only says
"depend on abstractions" gives no guidance on which of those to pick.

So SOLID is our *vocabulary* for logical design, not our whole rulebook.
Three bodies of practice carry more weight here:

| Source | What it governs | Why it matters here |
|---|---|---|
| [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/) (Stroustrup, Sutter) | Statement- and type-level rules | The closest thing to an agreed standard of practice; partly machine-enforceable via `clang-tidy` |
| Lakos, *Large-Scale C++ Software Design* | Physical design: components, levelization, insulation | This is the C++-specific analogue of architectural SOLID, and it is what our `core/` `platform/` `adapters/` `apps/` split is already reaching for |
| RAII, value semantics, the rule of zero/three/five | Resource and lifetime discipline | Already pervasive in this tree; codified below so it stays that way |

The sections below record what we actually do. Most of it the code
already does — writing it down is what makes it reviewable.

## 1. Physical design (the layering rule)

The tree is levelized: a component may only depend on components below
it. Nothing may depend on something above it, and there are no cycles.

```
apps/        hosting glue only (service control, argv, signals)
  |
adapters/    capabilities, reached ONLY across the C ABI
  |
core/        portable C++23; owns every interface
  |
platform/    per-OS implementations of interfaces core declares
```

Two consequences are worth stating explicitly, because they are easy to
break by accident:

**`core/` never includes anything from `platform/`, `adapters/` or
`apps/`.** This is the rule the README calls the portability rule; it is
a physical-design rule, and the one most worth defending in review. It
holds today and CI enforces it.

The stronger form — no `#ifdef _WIN32` anywhere in `core/` — does *not*
hold yet. Three exist. One is legitimate and permanent: the
`__declspec(dllexport)` / `visibility` selection in `vc_adapter.h`,
which is an ABI export macro with nowhere else to live. The other two
are leaks, listed in §8.

Note also that `core/CMakeLists.txt` compiles the matching `platform/`
sources into the `vc_core` target, so "core" as a *build artefact* spans
both directories. The separation is real at the source and header level,
which is where it matters for portability review, but `vc_core` is not
an independently linkable portable library.

**The dependency is inverted at the interface, not at the call.** `core/`
declares `vc::Socket`, `vc::Tls`, `vc::fs`, `vc::os` and
`vc::Impersonation`; `platform/win` and `platform/posix` define them.
Core therefore owns the abstraction and the platform layer conforms to
it — the Dependency Inversion Principle expressed physically, resolved
at link time rather than through a vtable. Adding a platform means
implementing existing headers, never adding new ones to `core/`.

**Insulation.** A platform type must never appear in a `core/` header —
no `SOCKET`, no `SSL*`, no `HANDLE`. Where per-platform state is
unavoidable, hide it behind a Pimpl, as `vc::Tls` and
`vc::Impersonation` already do. The test is mechanical: a `core/` header
must compile against no SDK but the standard library.

**Adapters are a binary boundary, not a code boundary.** Everything
crossing it is UTF-8 C strings through the three exported functions
(see the Adapter ABI in the README). Adapters do not link `vc_core`'s
C++ API for communication and must not share C++ types with the host —
that is what keeps them independently buildable and replaceable.

## 2. Resources and lifetime

- **RAII for everything that owns.** Every OS handle lives in a class
  whose destructor releases it. There are no bare `close()`/`free()`
  paths that an early return can skip.
- **Rule of zero, else rule of five.** Prefer types that need no
  user-declared special members. When a type owns a handle, declare all
  five and delete what makes no sense — see `vc::Socket`, `vc::Tls`,
  `vc::DynLib`, `vc::Impersonation`, all move-only with copying deleted.
- **Move-only over reference counting.** Ownership is single and
  explicit; `shared_ptr` needs justification in review.
- **Factory over throwing constructor.** Since we do not use exceptions,
  a constructor cannot report failure. Fallible construction is a static
  factory returning `Result<T>` or `std::optional<T>`
  (`Socket::connect`, `Tls::connect`, `DynLib::open`, `Adapter::load`,
  `Impersonation::begin`), leaving the default constructor to make a
  valid empty object.

## 3. Errors

- **No exceptions, no error codes in out-parameters.** Fallible
  operations return `vc::Result<T>` (`std::expected<T, Error>`) or
  `vc::Status`. This is a deliberate constraint: the agent runs as an
  unattended service across a C ABI boundary, where an escaping
  exception is undefined behaviour.
- **Every fallible result is `[[nodiscard]]`.** An ignored `Status` is
  the failure mode this design is most exposed to, and the compiler can
  catch it for free.
- **Errors are values, and they are logged where they are handled, not
  where they are produced.** Deep code returns; the run loop decides
  severity.
- **An error carries the platform code that produced it.** `vc::Error` is
  a category *and* the OS code behind it — `errno`, `GetLastError`,
  `std::error_code::value()` — captured at the point of failure, before a
  `close()` or a `freeaddrinfo()` can overwrite it, and rendered by
  `error_detail()`. Comparing against a category ignores the code:
  control flow turns on "was this a timeout", never on which timeout.
- **Never let a credential into an error message.** `ImpersonationError`
  carries a message documented as log-safe; keep it that way.

## 4. Interfaces

- **Narrow and total.** A function takes what it needs — `std::span`,
  `std::string_view` — not a container it will not own. It should be
  impossible to call incorrectly rather than documented as needing care.
- **Prefer free functions in a namespace** for stateless operations
  (`vc::fs`, `vc::os`, `vc::log`, `vc::sha256`). Reach for a class only
  when there is state or a resource to own.
- **`const`-correct and `noexcept` where true.** Both are part of the
  interface, not decoration.
- **Choose the cheapest polymorphism that fits.** Compile-time
  (templates, C++23 concepts) when the set of implementations is known
  at build time; a C function pointer table at a binary boundary, as the
  adapter ABI does; virtual functions only when the choice is genuinely
  a runtime one. This project's answer is usually compile-time, and that
  is a feature — it is why there is no vtable in the hot path.

## 5. SOLID, mapped honestly

Where the classical principles land in a codebase with almost no
inheritance:

- **Single responsibility** — applies as written, at file and function
  scope. It is the principle we most often fall short of; the run loops
  are where to look.
- **Open/closed** — the adapter ABI is the project's main answer:
  new capabilities ship as new libraries with the core untouched. This
  is OCP achieved by dynamic loading rather than subclassing.
- **Liskov substitution** — largely not applicable; we have no
  inheritance hierarchies to violate it. It reappears as a constraint on
  *platform parity*: two implementations of the same `core/` header must
  be behaviourally interchangeable, or must fail loudly where they are
  not. `vc::Impersonation` on POSIX returning `Error::Unsupported`
  rather than silently running as the service account is the pattern —
  an honest refusal beats a divergent success.
- **Interface segregation** — read as "no fat headers": a caller that
  wants a SHA-256 should not compile the relay protocol. Our
  one-header-per-concern layout in `core/include/vc/` is this principle.
- **Dependency inversion** — see §1. Applied physically to the platform
  layer, and now to the transport as well: `vc::Transport` is a concept,
  the protocol code is written against it rather than against `Tls` or
  `Socket`, and that is what made the framing, the HTTP parser and the
  relay state machine testable (§8).

## 6. Constraints we hold on purpose

- No third-party runtime dependencies. The Hybrid Connections protocol
  is implemented from scratch on purpose; OpenSSL on POSIX and SChannel
  on Windows are the only external crypto, and both sit behind
  `vc::Tls`.
- No exceptions, no RTTI-dependent designs.
- No global mutable state beyond the logger, which is a deliberate
  process-wide singleton.
- Standard library over hand-rolled utilities, except where a binary
  boundary forbids it (`vc_alloc`/`vc_free`).

## 7. Enforcement

What is mechanically checked today:

- `clang-format` (Microsoft base, Allman, 100 columns) on every PR.
- Warnings on: `/W3` on MSVC, `-Wall -Wextra` otherwise.
- `vc-selftest` on Windows, Linux and macOS in CI.

Deliberately not automated yet, and reviewed by hand: the layering rule
of §1, insulation, and `[[nodiscard]]` coverage. See the adoption status
below.

## 8. Adoption status

Every item first listed here is done. The tree follows §1–§6, and §7 now
checks §1 and part of §2–§4 mechanically.

- [x] **Testability of the protocol layer.** `vc::Transport` is a concept
      satisfied by `vc::Tls` and `vc::Socket`; `WebSocketT`, the HTTP
      client's `exchange()` and the relay's `Listener` are written
      against it. `vc-selftest` now covers the WebSocket framing, the
      HTTP response parser and the relay state machine — roughly 120
      checks over code that previously had none.

      Two things turned out differently from the plan. The template
      definitions did *not* have to move into public headers:
      `ScriptedTransport` is a library type, so each template is
      explicitly instantiated for it in the same translation unit and
      §4's insulation is intact. And the relay needed its *dialler*
      injected, not just a transport, because it opens rendezvous
      connections mid-stream.
- [x] **The OS code behind an `Error`** (#25). `Error` was a bare enum,
      so every I/O failure reached its caller as `Error::Io` — the same
      value for a refused connection, a peer that hung up and a full
      disk. It is now a value type carrying the category *and* the
      platform code, `error_detail()` renders both, and the socket,
      filesystem, random and impersonation paths capture the code where
      the call actually fails.

      Two things are worth recording. The old spelling survived: the
      categories are `static constexpr ErrorCode` members of `Error`, so
      `Error::Io` and `std::unexpected(Error::Io)` still compile at all
      150 construction sites, and the diff stayed in the platform layer
      the codes come from rather than spreading across core.

      And capturing *at* the failure rather than after it was most of the
      work. `connect()`'s asynchronous path never sets `errno` at all —
      the reason arrives in `SO_ERROR` — and the cleanup between the
      failure and the return overwrites whatever was there, so the log
      line #30 added on POSIX could report the cleanup instead of the
      failure. Both platforms now capture inside the loop; that log line
      is gone, because §3 logs where the error is handled and the code
      now reaches the relay's `CONNECT_FAILED` and `RENDEZVOUS_FAILED`
      events, which is what an operator reads.

      Deliberately not carried: `Error::Tls`. SChannel's
      `SECURITY_STATUS` and OpenSSL's error queue are separate code
      spaces, and each needs its own renderer before there is any point
      putting them in a field documented as holding an OS code.
- [x] **`[[nodiscard]]` on the 37 fallible declarations** in
      `core/include/vc/`. Sixteen of the seventeen discards it found
      were best-effort and are now explicit `(void)` casts; the
      seventeenth was a real bug in the filesystem adapter's `ReadFile`,
      whose unchecked archive move could report 200 while leaving the
      file in the polled folder to be read again.
- [x] **Single responsibility in `handle_request`**, down from 98 lines
      to 43. The control-versus-rendezvous size decision is now a pure
      function, which is what made it checkable.
- [x] **Enforce the layering rule in CI** — the `design-rules` job
      greps for `platform/`, `adapters/` and `apps/` includes inside
      `core/` and compiles each `core/` header on its own against
      nothing but the standard library.
- [x] **`clang-tidy`**, clean and blocking, over `core/`, `adapters/`
      and `platform/posix/`. Getting there took diagnosing it locally
      rather than through CI runs: clang-tidy uses clang's frontend, so
      the compilation database has to come from the *same major version*
      of clang or it reports standard library internals instead of our
      code.

      Broad `cppcoreguidelines-*` stays off: the pointer arithmetic in
      the framing and codec paths and the `reinterpret_cast` byte views
      are intrinsic to the design, so those checks are noise here.
      Three `bugprone-*` checks are off for reasons recorded in
      `.clang-tidy`, and `apps/` is out of scope as hosting glue and
      tests.

      What it found and what was fixed: three Pimpl types
      (`Tls::Impl` on both platforms, `Impersonation::Impl` on Windows)
      had destructors releasing OS handles but no deleted copy or move,
      so a copy would have released them twice — the rule-of-five
      violation §2 exists to prevent. Also an unchecked `std::optional`
      dereference in the relay's run loop, and two `int` multiplications
      widened to `size_t` in the SHA-256 core.
- [x] **Two `#ifdef _WIN32` leaks in `core/`** — `vc_log.cpp`'s
      `localtime_s`/`_ftime_s` split (now `os::local_time()`) and
      `vc_adapter.cpp`'s library suffix (now
      `os::shared_library_extension()`). The only conditional left in
      `core/` is the ABI export macro in `vc_adapter.h`, which §1
      records as legitimate and permanent.
- [x] **`core/CMakeLists.txt`'s header comment**, which described the
      tree as "platform independent C11".
- [x] **`.clang-format` said `Standard: c++20`** on a C++23 tree. Now
      `Latest`; clang-format has no `c++23` enumerator.

## 9. Open design questions

Recorded so they are argued once, from the same facts, rather than
re-litigated whenever someone new reads the code.

### 9.1 Should a command outcome be an HTTP status?

**The question.** When an adapter command fails for a reason outside the
agent's control — a file is missing, a parameter is malformed, a
destination is locked — should that surface as a non-2xx HTTP status on
the relay response, or as HTTP 200 carrying the outcome in the payload?

**The case for 200-with-payload.** HTTP status belongs to the transport
and app layers. The listener did its job: it accepted the request,
dispatched it and produced an answer. Nothing failed at the level HTTP
describes. Reporting a command-level outcome as 5xx or 4xx conflates two
layers, and makes "the agent is broken" indistinguishable from "the
command could not be satisfied".

**The case for the status code.** A non-2xx is the only signal that
reaches the party who can act on it. The adapters have no logging of
their own; the host logs the full adapter JSON (`ADAPTER RESULT` in
`vc_agent.cpp`), but that lands in `VeriConnect.log` on a machine behind
the customer's firewall, which nobody reads until something else has
already gone wrong. A 2xx-only design also requires every consumer to
parse the body to notice failure, where most integration frameworks
handle a non-2xx automatically — retry, dead-letter, alert.

**What the code already does.** This is not an open choice at the
margins; it is the established contract. `vc_agent.cpp` assigns the
adapter's `StatusCode` directly to `resp.status_code`, and the
FileSystem adapter already returns 400 for bad parameters (eleven
call sites), 404 for a missing file (five), 409 for a destination that
exists without overwrite, and 500 for an internal failure. The README
documents 403 for a failed logon and 501 for impersonation on an
unsupported platform. Every one of those is a command-level outcome
already expressed as an HTTP status.

**Current decision: keep the mapping.** The operational argument wins
while the adapters cannot report anything except through the response,
and consistency with the existing contract matters more than layering
purity applied to one command.

**What changing it would cost.** Moving to 200-with-payload is a
breaking change for every consumer of every code above, not a local
edit — it cannot be done for one command without making the contract
incoherent. If it is ever revisited, the honest split would be: HTTP
status for what the *host* is responsible for (malformed request,
authentication, unsupported platform, agent-internal failure) and 200
with the outcome in the payload for a command that ran and produced a
definite answer. That needs coordinating with the D365FO side, and a
major version.

