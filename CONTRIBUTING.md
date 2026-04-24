# Contributing to mos-qemu

`mos-qemu` is a fork of [qemu-project/qemu](https://gitlab.com/qemu-project/qemu) carrying specific patches for running macOS 15 (Sequoia) as a guest. Contributions are welcome and must be authored as if submitting upstream tomorrow.

## Upstream-submission discipline

The goal for code changes in this repo is that commits can be submitted to qemu-project/qemu unchanged. That means:

- **Commit message format:** `hw/<subsystem>/<file>: <subject>` — subsystem prefix, imperative mood, no trailing period, 75-char wrap. Match recent upstream commits for cadence.
- **Licensing:** new files are GPL-2.0+. QEMU-derived files retain upstream license headers verbatim.
- **Coding style:** 4-space indent, K&R braces on functions, match `hw/display/` and `hw/misc/` conventions.
- **Logging:** `qemu_log_mask(LOG_GUEST_ERROR, ...)`, `trace_*`, `warn_report`. No inline `printf` / bare `qemu_log` for runtime diagnostics.
- **No project-internal names:** never reference `mos15`, `mos-qemu`, `mos-docker` in source, debug strings, or comments. Generic identifiers only.
- **No personal paths or credentials** — grep for usernames, internal IPs, tokens before committing.
- **Trailers:** committed work may carry `Co-Authored-By:` trailers; these are filtered to `Signed-off-by:` when submitting upstream.

## Adding a new patch

Stage new upstream-destined work under `upstream-pr/<topic>/` alongside the existing `apple-gfx-pci-linux/`, `applesmc-fix/`, `usb-hid-apple-ids/`, and `vmware-svga-caps/` examples. Each package carries `PR_DESCRIPTION.md`, `SERIES.md`, and `TESTING.md` describing intent, commit order, and validation.

## Testing

CI runs against the pinned QEMU version in `.github/workflows/ci.yml`. The `meson-setup` job overlays our files onto an upstream QEMU tarball and hard-fails on any build breakage. Local validation: clone matching upstream, apply patches, `meson setup build && meson compile -C build`.

## License

This repository carries AGPL-3.0 on our additions (see `LICENSE`). Upstream-derived files retain GPL-2.0+ as required by QEMU's licensing.
