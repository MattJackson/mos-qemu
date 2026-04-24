# Security Policy

## Reporting a Vulnerability

Please report security issues privately via GitHub Security Advisories:

**https://github.com/MattJackson/mos-qemu/security/advisories/new**

Do not file security issues in the public issue tracker and do not send
them by email until we have acknowledged the advisory privately.

When reporting, please include:

- Affected version(s) and the upstream QEMU base version.
- A description of the issue and its impact.
- Reproduction steps or a proof-of-concept, if available.
- Any suggested mitigation or fix.

We will acknowledge the advisory within a reasonable window and
coordinate a fix and disclosure timeline with you.

## Scope

This project is a **fork** of
[qemu-project/qemu](https://gitlab.com/qemu-project/qemu) that carries a
small set of additional patches on top of a specific upstream base. The
security policy here covers **only the patches added by this fork**.

Vulnerabilities in unmodified upstream QEMU code (the vast majority of
the binary) are out of scope here and should be reported to the QEMU
project directly, following their security process:

- https://www.qemu.org/contribute/security-process/

If you are unsure whether a given issue is in our added code or in
upstream QEMU, report it to us via GitHub Security Advisories and we
will help triage and, where appropriate, coordinate reporting to the
upstream QEMU security team.

## Supported Versions

| Version | Upstream QEMU base | Supported           |
|--------:|--------------------|---------------------|
| 0.5.x   | 10.2.x             | Yes — active branch |
| < 0.5   | n/a                | No                  |

Only the most recent minor line is supported for security fixes. Users
on older versions are expected to upgrade to a supported release before
a fix can be backported.
