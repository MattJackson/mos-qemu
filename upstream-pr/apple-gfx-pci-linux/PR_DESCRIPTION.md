# hw/display: add Linux-host port of apple-gfx-pci

## Summary

Upstream carries `hw/display/apple-gfx-pci.m` and
`hw/display/apple-gfx.m`, which drive Apple's
ParavirtualizedGraphics (PG) protocol from a macOS host via
the vendor-supplied `ParavirtualizedGraphics.framework`.
Linux QEMU hosts cannot use those files because the
framework has no Linux equivalent.

This series adds a companion Linux C port that speaks the
same PG protocol via `libapplegfx-vulkan` (a Mesa-lavapipe-
backed reimplementation of the PG framework's shell surface).
The guest-side `AppleParavirtGPU.kext` is unchanged; macOS
VMs running under KVM on Linux can now attach the same
paravirt GPU they attach on macOS hosts.

## What's new

  * `hw/display/apple-gfx-pci-linux.c` (~250 lines)
  * `hw/display/apple-gfx-common-linux.c` (~770 lines)
  * `hw/display/apple-gfx-linux.h` (~120 lines)
  * `hw/display/Kconfig` (1 new symbol)
  * `hw/display/meson.build` (1 new gated module)
  * `pc-bios/meson.build` (1 new blob entry)
  * `pc-bios/apple-gfx-pci.rom` (option ROM, development
    placeholder - see per-patch note)

The series is 8 patches and adds a new device type
`apple-gfx-pci` on Linux hosts, with a `gpu_cores=N`
property tunable for lavapipe's worker-pool cap. No
upstream file is functionally modified. The build gate is
additive; `libapplegfx-vulkan` is declared `required: false`
so the device drops out silently on hosts without the
library.

## What works today vs what's blocked

At the **library level** end-to-end pixel traffic works
today against `libapplegfx-vulkan` commit `8edc43c`:

  * Task-memory management, MMIO dispatch, display-plane
    opcode handling, Vulkan init, command-pool submit, and
    a Phase 2.B clear-colour render target + readback all
    pass their in-tree test suites (~277 `CHECK` assertions,
    zero failures on a Linux host with Mesa lavapipe
    installed). The `protocol-dispatch` test alone covers
    207 assertions on the opcode decoder.
  * The QEMU-side frame-readback bottom half is wired live:
    `lagfx_display_read_frame` -> `surface_data` ->
    `dpy_gfx_update`. Displays render clear-colour frames
    end-to-end today at the library level.

**Blocked on:**

  * Packaging `libapplegfx-vulkan` so the build-dependency
    on `pkg-config --exists libapplegfx-vulkan` can be
    satisfied on a generic distribution. The library is
    API-stable but not yet distributed anywhere. See
    `LIBAPPLEGFX_DEPENDENCY.md` for the three resolution
    paths.
  * Replacing the `pc-bios/apple-gfx-pci.rom` development
    placeholder with an in-tree EDK2 build.
  * Library-side Metal -> Vulkan command translation
    (Phase 2.C+ work in the library, out of scope for this
    QEMU series) before a booted macOS guest will paint
    real application pixels. That work is independent of
    the QEMU port - the QEMU side is a pure pixel pipe.

## Dependency

The `libapplegfx-vulkan` library is not yet packaged in any
distribution. See `LIBAPPLEGFX_DEPENDENCY.md` for the three
paths to unblock upstream merge (system package / subproject
/ inlined) and the recommendation for Option 3 (vendored
subproject) as the most-reviewer-friendly initial path.

## Status

**Draft - do not merge.** The library side is ready
(API-stable at commit `8edc43c`); submission is blocked on:

  1. `libapplegfx-vulkan` distribution packaging or
     bundling decision.
  2. Replacing the placeholder `pc-bios/apple-gfx-pci.rom`
     blob with an in-tree EDK2 build.
  3. Review of the split by the apple-gfx.m author - the
     port is structured to avoid touching the existing
     Objective-C files, but a follow-up may want to factor
     the portable C logic out of `apple-gfx.m` into a
     shared `apple-gfx-common.c`.

## Testing

See `TESTING.md` for the end-to-end recipe. Short version:

    -device apple-gfx-pci,gpu_cores=8

plus an optional memory-backend-memfd for fast-path task
VA aliasing.
