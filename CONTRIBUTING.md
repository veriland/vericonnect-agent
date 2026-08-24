# Contributing to VeriConnect

Thanks for your interest in contributing! This project is **source-available**
under the [Functional Source License 1.1 (FSL-1.1-Apache-2.0)](LICENSE). You
are welcome to read, audit, and propose improvements to the code.

## Ground rules

- By contributing, you agree that your contribution is licensed under the same
  terms as the project (FSL-1.1-Apache-2.0, converting to the Apache License
  2.0 on the second anniversary of each release).
- Be respectful. Assume good intent. Keep discussion technical.

## Developer Certificate of Origin (DCO)

We use the [Developer Certificate of Origin](DCO) (DCO 1.1) instead of a CLA.
It is a lightweight, per-commit statement that you have the right to submit the
code you are contributing.

**Every commit must be signed off.** Add a `Signed-off-by` line to your commit
message by committing with the `-s` flag:

```bash
git commit -s -m "core: fix SAS token renewal off-by-one"
```

This appends a line using your `git config` name and email:

```
Signed-off-by: Jane Doe <jane@example.com>
```

The line must use your real name and a reachable email. A CI check verifies
that every commit in a pull request is signed off; unsigned commits will be
blocked. To fix an existing branch:

```bash
git rebase --signoff main
git push --force-with-lease
```

**Do not use GitHub's "Update branch" button.** It writes a merge commit
server-side with no way to attach a sign-off, and `main` is not configured to
require branches be up to date, so the click buys you nothing. If your branch
genuinely needs `main`, bring it in yourself:

```bash
git merge --signoff origin/main
```

Merge commits are exempt from the sign-off check — a merge carries no code of
its own — so the exemption is not a licence to skip `-s` on real commits. Where
you resolve a conflict by hand, that resolution exists only in the merge commit
and no sign-off covers it; keep such resolutions small and call them out in the
pull request so a reviewer reads them as code.

## How to contribute

1. Open an issue first for anything non-trivial, so we can agree on the
   approach before you invest time.
2. Fork the repo and create a topic branch off `main`.
3. Make your change. Match the surrounding code style (C++23, no new external
   dependencies without discussion) and the design rules in
   [DESIGN.md](DESIGN.md) — its §4 records the naming convention, including
   why the C ABI and the JSON contract are PascalCase while the C++ inside is
   not.
4. Ensure it builds on the platforms you can test (see [README](README.md)).
5. Open a pull request. Fill in the template, sign off your commits, and link
   the issue.

## What we look for

- Small, focused PRs over large ones.
- Conformance to [DESIGN.md](DESIGN.md) — in particular the layering rule
  (`core/` stays portable and includes nothing from `platform/`,
  `adapters/` or `apps/`), RAII ownership, and `Result`/`Status` for
  anything fallible.
- No secrets, credentials, or customer data in code, tests, or history.
- Security-relevant changes (impersonation, TLS, SAS tokens, relay) get extra
  scrutiny — explain the security reasoning in the PR description.

## Reporting security issues

**Do not** open a public issue for security vulnerabilities. See
[SECURITY.md](SECURITY.md) for private disclosure instructions.
