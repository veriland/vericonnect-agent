# Contributing to VeriConnect

Thanks for your interest in contributing! This project is **source-available**
under the [Business Source License 1.1](LICENSE). You are welcome to read,
audit, and propose improvements to the code.

## Ground rules

- By contributing, you agree that your contribution is licensed under the same
  terms as the project (the Business Source License 1.1, converting to the
  Change License on the Change Date).
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

## How to contribute

1. Open an issue first for anything non-trivial, so we can agree on the
   approach before you invest time.
2. Fork the repo and create a topic branch off `main`.
3. Make your change. Match the surrounding code style (C99, existing naming
   like `vc_*`, no new external dependencies without discussion).
4. Ensure it builds on the platforms you can test (see [README](README.md)).
5. Open a pull request. Fill in the template, sign off your commits, and link
   the issue.

## What we look for

- Small, focused PRs over large ones.
- No secrets, credentials, or customer data in code, tests, or history.
- Security-relevant changes (impersonation, TLS, SAS tokens, relay) get extra
  scrutiny — explain the security reasoning in the PR description.

## Reporting security issues

**Do not** open a public issue for security vulnerabilities. See
[SECURITY.md](SECURITY.md) for private disclosure instructions.
