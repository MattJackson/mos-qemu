# RFC cover — apple-gfx on Linux hosts via libapplegfx-vulkan (v2, external-library model)

Status: **SEND-READY — awaiting operator go. Nothing has been sent.**

Subject: [RFC PATCH 0/4] hw/display: Linux-host support for apple-gfx via libapplegfx-vulkan

To: qemu-devel@nongnu.org
Cc: Phil Dennis-Jordan <phil@philjordan.eu> (apple-gfx maintainer),
    plus `scripts/get_maintainer.pl` output over the four patches
    (hw/display, meson, docs).

Base: QEMU v11.0.2 (series generated against the release tarball tree).

## Body

QEMU gained apple-gfx (hw/display/apple-gfx.m) for macOS hosts, backed by
Apple's ParavirtualizedGraphics.framework. This series adds the Linux-host
counterpart: the same guest-visible PCI device (vendor 0x106b, device
0xeeee), so an unmodified macOS guest binds its stock AppleParavirtGPU.kext
driver, backed by libapplegfx-vulkan — an MIT-licensed, clean-room
implementation of the host side of the paravirtualized graphics protocol
with a Vulkan rendering backend. Rendering works on any Vulkan
implementation including Mesa's software lavapipe driver; no host GPU is
required.

Consumption model mirrors virglrenderer: external library, pkg-config
detection, new meson feature option 'libapplegfx' (auto by default), the
device compiled only when the library is found. The library is released:
v0.1.0 with a stable 20-symbol C ABI, semver from here on, pkg-config
manifest, and dist tarballs — https://github.com/MattJackson/libapplegfx-vulkan

No existing file's behaviour changes; the device is additive and gated.
Both apple-gfx implementations share trace-event names where the shape
matches, so they are observable identically.

Marked RFC for two questions before a non-RFC respin:
1. Packaging shape: is a pkg-config external dependency acceptable pre
   distro-packaging (as with early virglrenderer), or would maintainers
   prefer a meson wrap/subproject fallback in addition?
2. Naming/props: the device intentionally reuses the "apple-gfx-pci" type
   name (the macOS-host and Linux-host devices can never be compiled into
   the same binary). Happy to rename or align properties with the macOS
   device where that helps libvirt.

Follow-ups planned, not in this series:
- qtest probe smoke test (device enumeration without a guest). Left out
  for now because realize initializes the Vulkan backend, so the test
  needs a guard for hosts without a loadable ICD; will come with the
  non-RFC respin.
- vmstate/migration support (the device registers a migration blocker).
- Option ROM story for firmware-time output (the device works without a
  ROM; guests bring up the paravirt driver from the kext).

## Patches

1. `meson: add libapplegfx option for apple-gfx support on Linux hosts`
   — feature option (auto), pkg-config detection, summary line,
   meson-buildoptions.sh entries. Hard error if enabled on non-Linux.
2. `hw/display: add apple-gfx-pci device for Linux hosts`
   — the device: apple-gfx-common-linux.c (transport-agnostic core:
   task/memory shell callbacks, MSI injection, display/cursor BH
   bridging, MMIO forwarding), apple-gfx-pci-linux.c (PCI transport),
   apple-gfx-linux.h, Kconfig + meson wiring, MAINTAINERS entry.
3. `hw/display/apple-gfx-linux: add tracepoints`
   — reuses the existing apple_gfx_* events where shape-compatible;
   adds 5 new events (pci realize/reset, write_memory, vblank).
4. `docs/system: document the apple-gfx-pci device on Linux hosts`
   — docs/system/devices/apple-gfx-linux.rst + toctree entry.
   (checkpatch emits a "does MAINTAINERS need updating?" warning here;
   false positive — the rst is covered by patch 2's MAINTAINERS entry.)

## Diffstat (v11.0.2-base..HEAD)

```
 MAINTAINERS                             |   6 +
 docs/system/device-emulation.rst        |   1 +
 docs/system/devices/apple-gfx-linux.rst |  60 ++
 hw/display/Kconfig                      |   5 +
 hw/display/apple-gfx-common-linux.c     | 934 ++++++++++++++++++++++++++++++++
 hw/display/apple-gfx-linux.h            | 123 +++++
 hw/display/apple-gfx-pci-linux.c        | 284 ++++++++++
 hw/display/meson.build                  |   5 +
 hw/display/trace-events                 |   8 +
 meson.build                             |  11 +
 meson_options.txt                       |   2 +
 scripts/meson-buildoptions.sh           |   4 +
 12 files changed, 1443 insertions(+)
```

## Verification record (2026-07-19)

- checkpatch (v11.0.2 scripts/checkpatch.pl) over all 4 patches:
  0 errors, 0 warnings on patches 1-3; patch 4 has the single
  false-positive MAINTAINERS warning noted above.
- Compile proof (classe, Ubuntu 24.04, /tmp scratch — no repos touched):
  * libapplegfx-vulkan **v0.1.0** (GitHub tag) built and `meson install`ed
    to `/tmp/lagfx-prefix`; `pkg-config --modversion libapplegfx-vulkan`
    = 0.1.0 from that prefix.
  * Pristine `qemu-11.0.2` release tarball + the 4 patches (`patch -p1`,
    all applied clean); `../configure --target-list=x86_64-softmmu
    --enable-libapplegfx --disable-docs --disable-user` with
    `PKG_CONFIG_PATH` pointing at the scratch prefix:
    `Run-time dependency libapplegfx-vulkan found: YES 0.1.0`,
    summary `libapplegfx: enabled`.
  * Both device objects (`hw_display_apple-gfx-{pci,common}-linux.c.o`)
    compile **warning-free**.
  * Bisectability: with patches 3+4 reverse-applied (patch-2 stage,
    tracepoints absent) both objects still compile clean; re-applied OK.
  * **Full `ninja qemu-system-x86_64` link: exit 0**; `ldd` shows
    `libapplegfx-vulkan.so.0` linked.
  * Runtime probe without a guest:
    `./qemu-system-x86_64 -device apple-gfx-pci,help` lists the device
    with `display-modes=<list>` and `gpu_cores=<uint32>` properties.

## Differences from the v1 draft (9-patch, never sent)

- External-library model finalized: library is released (v0.1.0, MIT,
  pkg-config, dist tarball) — the old AGPL/vendoring/packaging blocker
  narrative in LIBAPPLEGFX_DEPENDENCY.md is obsolete.
- Rebased/regenerated against the QEMU v11.0.2 release tree.
- pc-bios option ROM patch DROPPED (the blob was extracted from Apple's
  framework and is not redistributable; the device no longer sets a
  default romfile — users may pass romfile=... explicitly).
- Device squashed from 5 incremental patches into one reviewable patch;
  meson option split out front; tracepoints as a follow-on patch reusing
  upstream event names; docs added; checkpatch-clean.
