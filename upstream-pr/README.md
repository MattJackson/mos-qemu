# Upstream PR staging area

Five discrete patch packages destined for
[qemu-project/qemu](https://gitlab.com/qemu-project/qemu) upstream,
staged here before submission. Each subdirectory is a
self-contained PR: cover letter (`SERIES.md`), numbered
`git format-patch`-compatible files, GitHub-style PR
description (`PR_DESCRIPTION.md`), and a testing recipe
(`TESTING.md`).

| Package | Subject | Size | Status |
|---------|---------|-----:|--------|
| [A](./applesmc-fix/) | Fix `GET_KEY_BY_INDEX` and populate Apple SMC key set | 2 patches | **v3 SENT 2026-07-19** (rebased resend after quiet thread; v1 2026-05-06, v2 2026-05-07 addressing Maydell review) ([lore v1](https://lore.kernel.org/qemu-devel/20260507040153.14565-1-matthew@pq.io/), [v2 search](https://lore.kernel.org/qemu-devel/20260507152020.48728-1-matthew@pq.io/)) |
| [B](./apple-gfx-pci-linux/) | Linux-host port of `apple-gfx-pci` | 4 patches (v2) | **SEND-READY — awaiting operator go** (2026-07-19: reworked to the external-library model against QEMU v11.0.2; libapplegfx-vulkan v0.1.0 released, MIT, pkg-config; checkpatch-clean; compile-verified. NOT sent.) |
| [C](./vmware-svga-caps/) | VMware SVGA II capability bits + 5K cap | 4 patches | **v1 SENT 2026-07-19** (qemu-devel, Cc kraxel) |
| ~~D~~ | ~~USB HID Apple vendor IDs~~ | — | **WITHDRAWN 2026-05-07** — descriptor-only wrappers were a wrong-shape solution; broke macOS recovery's HID stack. Superseded by Package E. |
| [E](./apple-magic-hid/) | Apple Magic Keyboard + Magic Trackpad USB-mode emulators | 2 patches | **v1 SENT 2026-07-19** (qemu-devel, Cc kraxel) — real-protocol replacement for withdrawn Package D; binds `AppleUSBTopCaseHIDDriver` at probe score 90000; descriptors byte-identical to real hardware |

A, C, E sent 2026-07-19. B is **SEND-READY — awaiting
operator go**: the v2 series (4 patches, external-library
model, QEMU v11.0.2 base) lives in
`apple-gfx-pci-linux/v2/`; libapplegfx-vulkan v0.1.0 is
released (MIT, 20-symbol stable ABI, pkg-config, dist
tarball), so the old packaging-path blocker is resolved —
see the reworked `LIBAPPLEGFX_DEPENDENCY.md` and
`RFC-COVER.md`. Nothing is sent until the operator says go. Precedent for upstream fork
submissions from this tree: our OpenCorePkg submission
([acidanthera/OpenCorePkg PR #600](https://github.com/acidanthera/OpenCorePkg/pull/600), similar hand-off shape) — the
next step for B is a discussion with the upstream maintainer
on which of the three library-packaging options they prefer
to review.

## Per-package status

### A - applesmc fix (ready)

Fixes a real, reproducible macOS-guest bug:
`APPLESMC_GET_KEY_BY_INDEX_CMD` returns zeros, which macOS
interprets as `kSMCSpuriousData` and retries at ~1800
errors/sec, burning `kernel_task` at 70 % CPU and
`WindowServer` at 509 %. The four-patch series implements
real iteration, adds `#KEY`, populates the sensor table with
realistic iMac20,1 values, and fills in the boot-required
key set.

Dependencies: none.
Ready to submit: **yes, immediately**.

### B - apple-gfx-pci Linux port (SEND-READY — awaiting operator go)

Companion to Phil Dennis-Jordan's upstream apple-gfx.m /
apple-gfx-pci.m work: the same guest-visible PCI device on
Linux hosts, backed by the external `libapplegfx-vulkan`
library (Vulkan backend; works on Mesa lavapipe, no host GPU
required). Additive only; no upstream file's behaviour
changes.

**v2 series (2026-07-19, `apple-gfx-pci-linux/v2/`),
regenerated against the QEMU v11.0.2 release tree in the
external-library model (virglrenderer-style pkg-config
dependency):**

  1. `meson: add libapplegfx option for apple-gfx support on
     Linux hosts` — feature option (auto), pkg-config
     detection, summary, meson-buildoptions.sh.
  2. `hw/display: add apple-gfx-pci device for Linux hosts`
     — device + Kconfig + meson wiring + MAINTAINERS.
  3. `hw/display/apple-gfx-linux: add tracepoints` — reuses
     upstream apple_gfx_* event names where shape-compatible;
     adds 5 new events.
  4. `docs/system: document the apple-gfx-pci device on Linux
     hosts`.

The v1 9-patch draft (never sent) is preserved in
`apple-gfx-pci-linux/v1-draft/`; its pc-bios option-ROM blob
patch was dropped for v2 (non-redistributable blob; the
device no longer sets a default romfile).

Dependency: **resolved.** libapplegfx-vulkan v0.1.0 is
released (MIT, stable 20-symbol ABI, `libapplegfx-vulkan.pc`,
GitHub dist tarball). See the reworked
[LIBAPPLEGFX_DEPENDENCY.md](./apple-gfx-pci-linux/LIBAPPLEGFX_DEPENDENCY.md).

Verification (2026-07-19): checkpatch (v11.0.2) clean over
all 4 patches (one false-positive MAINTAINERS warning on the
docs patch); pristine v11.0.2 tree + series compiles with
lagfx v0.1.0 from a scratch prefix (`configure` reports
`libapplegfx: enabled`, both device objects build warning-
free, bisect-checked at the patch-2 stage); full
qemu-system-x86_64 link result recorded in `RFC-COVER.md`.

Ready to submit: **yes — held for operator go. Do not send
without it.** Cover letter: `RFC-COVER.md` ([RFC PATCH 0/4]).


### C - vmware_vga capability bits (ready)

Advertises capability bits the modern VMware SVGA II drivers
expect (PITCHLOCK, EXTENDED_FIFO, 8BIT_EMULATION, ALPHA_BLEND,
ALPHA_CURSOR) and raises the software-side resolution cap
from 2368x1770 to 5120x2880. All of the underlying machinery
is already present in the upstream device; this series only
flips the capability bits.

Dependencies: none.
Ready to submit: **yes, immediately**.

### ~~D~~ - USB HID Apple vendor IDs (WITHDRAWN 2026-05-07)

Withdrawn: descriptor-only wrappers were a wrong-shape
solution. Apple's `AppleUSBTopCaseHIDDriver` claims the
device by VID/PID/strings but then refuses to bind because
the report descriptor is standard HID boot-protocol rather
than Apple's vendor-defined `UsagePage 0xff00` protocol.
Result on iMac20,1 SMBIOS guests: dangling unclaimed
device, "Power on Bluetooth Keyboard" recovery UI, all
input lost. Replaced by Package E.

### E - Apple Magic Keyboard + Magic Trackpad emulators (ready 2026-05-08)

Two new self-contained USB-HID devices in `hw/usb/dev-hid.c`:

  * **apple-magic-keyboard** (PID 0x026c) — composite
    device, two HID interfaces. Interface 0 carries Apple's
    vendor HID protocol on `UsagePage 0xff00` (vendor input
    report IDs 0xe0 / 0x9a / 0x90 + 1 Hz battery
    heartbeat); interface 1 is a standard HID Boot Keyboard
    wired to QEMU's input subsystem via
    `qemu_input_handler_register`. Descriptors are
    byte-identical to a real Magic Keyboard with Numeric
    Keypad in USB-cable mode.
  * **apple-magic-tablet** (PID 0x0265) — single vendor HID
    interface emulating the Magic Trackpad 2 boot-mouse
    face. Two input reports: 1 Hz heartbeat + boot-mouse
    pointer frame (~66 Hz cadence). 30 ms idle timer
    flips the surface state from touching to lifted.

Apple HID driver chain
(`AppleUSBTopCaseHIDDriver` → `AppleDeviceManagementHIDEvent
Service` → `AppleUserHIDEventDriver`) binds at probe score
90000 on macOS 15.7.5 recovery. Setup Assistant does not
appear. Visible keystroke proof captured via QMP `send-key`
advancing recovery's language-picker UI.

Zero behaviour change for existing users: `-device usb-kbd`
/ `usb-mouse` / `usb-tablet` and their vmstate are
unchanged. Migration of existing VMs is unaffected.

Vendor multitouch protocol (per-finger absolute frames) is
gated behind a vendor-enable SET_REPORT macOS sends after
enumeration; out of scope for v1, follow-up series will add
it once the vendor-enable sequence is reverse-engineered.

Dependencies: none.
Ready to submit: **yes, pending operator-driven
`git send-email` from postfix on classe** (same workflow as
Patch A on 2026-05-06). CC list to be populated by running
`scripts/get_maintainer.pl` on the patches from a full QEMU
tree (this fork lacks the scripts/ directory; use the
operator's classe checkout). Default CCs: `qemu-devel@nongnu.org`,
`qemu-trivial@nongnu.org`, plus `hw/usb` maintainer (Gerd
Hoffmann <kraxel@redhat.com>).

## Submission order recommendation

1. **Package A** (applesmc) first - clearest bug, measurable
   impact, cleanest diff.
2. **Package C** (vmware_vga) alongside or immediately after -
   similarly uncontroversial.
3. **Package E** (apple-magic-keyboard / apple-magic-tablet)
   alongside A and C - additive, zero behaviour change for
   existing users; supersedes withdrawn Package D.
4. **Package B** (apple-gfx-pci-linux) last, once the
   dependency story is resolved.

## Commit style

All upstream commits follow qemu-project convention:

  * `<subsystem>: <imperative subject>` subject line
    (e.g. `hw/misc/applesmc: fix GET_KEY_BY_INDEX ...`).
  * Body wrapped at roughly 75 columns.
  * `Signed-off-by: Matthew Jackson <matthew@pq.io>`
    trailer on every commit.
  * `Reported-by:` / `Tested-by:` where applicable.
  * **No** `Co-Authored-By: Claude ...` trailers on
    upstream-destined commits.

## Local cleanup commit

HEAD of the source tree contains a cleanup commit
(`43dcccb cleanup: strip fork-local identifiers from
custom patches`) that replaces `mos15:` comment prefixes
and `mos15-smc:` stderr strings with neutral,
subsystem-scoped phrasing, and routes non-fatal debug
output through `qemu_log_mask` instead of `fprintf(stderr)`.
The upstream patches in this directory are generated
against the post-cleanup tree and the upstream QEMU master
reference, so there are no fork-local identifiers in any
of the patch files.

Fork-local identifiers found and cleaned across the
custom files:
  * `hw/misc/applesmc.c`: 24
  * `hw/display/vmware_vga.c`: 2
  * `hw/display/apple-gfx-linux.h`: 1

`hw/usb/dev-hid.c` previously carried 13 fork-local
descriptor-string edits ("mos15:" comments and Apple IDs
baked into the base devices' descriptors). Those have been
reverted to upstream verbatim; the Apple identity now lives
in the opt-in `apple-kbd` / `apple-mouse` / `apple-tablet`
wrapper types added at the bottom of the same file (see
Package D).
