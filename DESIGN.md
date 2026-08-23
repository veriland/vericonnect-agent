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
`apps/`.** It compiles standalone, on any OS, with no `#ifdef _WIN32`
outside a header guard. This is the rule the README already calls the
portability rule; it is a physical-design rule, and it is the one most
worth defending in review.

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
  layer; *not yet applied to the transport*, which is the known gap
  below.

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

The tree already follows §2, §3 (minus `[[nodiscard]]`), §4 and §6
closely. The open items, roughly in order of value:

- [ ] **Testability of the protocol layer.** `vc_ws`, `vc_http` and
      `vc_relay` bind directly to concrete `vc::Socket`/`vc::Tls`, so
      they cannot be exercised without a real network. This is why
      `vc-selftest` covers only the pure leaf modules — SHA-256,
      base64, JSON, URL, INI — and none of the WebSocket framing,
      HTTP parsing or relay state machine, which is the most intricate
      and highest-risk code in the project. This is the single
      highest-value change available.

      The agreed design is compile-time polymorphism (§4): a
      `vc::Transport` concept satisfied by `vc::Tls`, with `WebSocket`
      and the HTTP client templated on it and a mock transport in
      `vc-selftest`. Two prerequisites, both worth doing on their own
      merits:

      - `Socket::send` returns `Result<std::size_t>` while `Tls::send`
        returns `Status`, so they do not satisfy one concept today. No
        call site uses the count — every one tests it as a boolean —
        and the declaration already promises to send all bytes, so the
        count is redundant. Normalise on `Status`.
      - `WebSocket::connect` and `vc::http::request` dial the socket
        themselves. Construction has to be inverted — an `upgrade()`
        taking an already-connected transport, with `connect()` kept as
        the convenience wrapper — or a mock can never get in.

      Note the relay needs more than a transport: `rendezvous_connect`
      opens *new* connections mid-stream, so it also needs the dialler
      injected, as a second concept or a callable.
- [ ] **`[[nodiscard]]` on the ~37 fallible declarations** in
      `core/include/vc/`, then fix whatever the compiler surfaces.
- [ ] **Single responsibility in `handle_request`.** `relay_listen` is
      a thin wrapper over a `Listener` class that is already decomposed
      into nine short methods — `ctrl_connect`, `renew_token_if_due`,
      `read_body`, `handle_control_message` and the rest are all 8–26
      lines. The outliers are `handle_request` at 98 lines, which mixes
      request parsing, the control-vs-rendezvous size decision, body
      reading and dispatch, and `run` at 55. Splitting the decision
      from the I/O in `handle_request` is what makes it testable once
      the transport seam exists.
- [ ] **Enforce the layering rule in CI** — a grep for `platform/`,
      `adapters/` and `apps/` includes inside `core/`, plus a
      standalone-compile check of each `core/` header.
- [ ] **`clang-tidy` with a `cppcoreguidelines-*` subset**, warnings
      only at first, so §7 covers §2–§4 mechanically.
- [ ] **`.clang-format` says `Standard: c++20`** while the project is
      C++23.
