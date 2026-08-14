# Security Policy

VeriConnect is an agent that performs privileged operations (user
impersonation, TLS relay connections, filesystem access). We take security
reports seriously.

## Reporting a vulnerability

**Please do not report security vulnerabilities through public GitHub issues,
discussions, or pull requests.**

Instead, report privately using one of:

- GitHub's [private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
  (the **Security** tab → **Report a vulnerability**), or
- Email **security@veriland.co.uk** with the details.

Please include:

- A description of the issue and its impact.
- Steps to reproduce or a proof of concept.
- Affected version/commit and platform.

## What to expect

- We aim to acknowledge reports within **3 business days**.
- We will keep you informed as we investigate and work on a fix.
- Please give us a reasonable window to release a fix before any public
  disclosure. We are happy to credit you unless you prefer to remain anonymous.

## Scope

Areas of particular interest: the impersonation logic, TLS/Schannel/OpenSSL
handling, SAS token generation and renewal, and the relay control channel.
