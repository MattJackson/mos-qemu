# hw/display: add Linux-host port of apple-gfx-pci

Upstream QEMU (since Phil Dennis-Jordan's series in 2024)
carries `hw/display/apple-gfx-pci.m` and `hw/display/apple-gfx.m`,
which implement the host side of Apple's
ParavirtualizedGraphics protocol. Those files are Objective-C
and depend on Apple's ParavirtualizedGraphics.framework; they
are therefore restricted to macOS hosts (`system_ss.add(when:
[pvg, ...])` in `hw/display/meson.build`).

This series adds a companion Linux C port that drives the
same PGDevice/PGShellCallbacks protocol via a library we
call `libapplegfx-vulkan`, backed by Mesa lavapipe. The
guest-side driver (Apple's `AppleParavirtGPU.kext`) is
unchanged; Linux hosts can now run macOS VMs with the same
guest kext that Apple ships, without needing a macOS host.

No upstream file is modified. The new files live alongside
the existing Objective-C implementation:

| File | Status |
|------|--------|
| `hw/display/apple-gfx.m`             | (upstream, untouched) |
| `hw/display/apple-gfx-pci.m`         | (upstream, untouched) |
| `hw/display/apple-gfx-mmio.m`        | (upstream, untouched) |
| `hw/display/apple-gfx.h`             | (upstream, untouched) |
| `hw/display/apple-gfx-pci-linux.c`   | **new (Linux port)** |
| `hw/display/apple-gfx-common-linux.c`| **new (Linux port)** |
| `hw/display/apple-gfx-linux.h`       | **new (Linux port)** |
| `hw/display/meson.build`             | **modified** (build gate) |
| `hw/display/Kconfig`                 | **modified** (new symbol) |
| `pc-bios/meson.build`                | **modified** (ship ROM) |
| `pc-bios/apple-gfx-pci.rom`          | **new blob** |

## Patch list

| Patch | Subject |
|-------|---------|
| 1/8   | `hw/display: add apple-gfx-pci-linux port skeleton` |
| 2/8   | `hw/display/apple-gfx-linux: memory/task plumbing` |
| 3/8   | `hw/display/apple-gfx-linux: MMIO dispatch` |
| 4/8   | `hw/display/apple-gfx-linux: display callbacks and console integration` |
| 5/8   | `hw/display/apple-gfx-linux: PCI device wrapper` |
| 6/8   | `hw/display/apple-gfx-linux: add gpu_cores property` |
| 7/8   | `hw/display/apple-gfx-linux: meson + Kconfig wiring` |
| 8/8   | `pc-bios: ship apple-gfx-pci option ROM` |

## Library-side progress (as of this draft)

The companion `libapplegfx-vulkan` library (pinned at
commit `8edc43c`) now implements enough of the PGDevice /
PGShellCallbacks contract for end-to-end pixel traffic at
the library level:

  * **Phase 1.A** (metal-no-op scaffold): MMIO dispatch,
    opaque handles, shell-callback shape. Proven by the
    library's `protocol-dispatch` test suite (207 `CHECK`
    assertions, zero failures).
  * **Phase 1.B** (Vulkan init): instance + physical device
    + logical device + queue on Mesa lavapipe. Library-
    level proof via the `vulkan-init` test (Linux hosts;
    SKIPs on hosts without a loadable ICD).
  * **Phase 1.B.2** (command-pool + empty-submit smoke):
    proven via the `vulkan-command` test.
  * **Phase 1.C** (task-memory coherence): `mremap()`-based
    page-aliasing so post-map guest writes reach the task
    VA; proven via the `memory-coherence` test.
  * **Phase 2.A** (display opcode handlers): DisplayAck,
    DisplaySwapMapping, DisplayTransaction3 decode and
    route through the device.
  * **Phase 2.B** (first-pixel path): Vulkan clear-color
    render target + image-to-buffer readback, with the
    frame surfaced via the `frame_ready` shell callback.
    Proven via the `vulkan-render` test, which clears a
    64x64 BGRA8 target to a known colour and asserts on
    the read-back centre pixel.

Across the library's full test matrix (header-syntax,
lifecycle, memory-task, memory-coherence, gpu-cores,
protocol-dispatch, vulkan-init, vulkan-command,
vulkan-render) roughly 277 `CHECK` assertions fire; all pass
on a Linux host with Mesa lavapipe installed.

On the QEMU side the frame-readback bottom half is wired
live: `lagfx_display_read_frame` is called from the BH,
pushed into `surface_data`, and dispatched via
`dpy_gfx_update`. Displays therefore render clear-colour
end-to-end **at the library level today**; the only
gap from a running macOS guest is the library-side Metal
-> Vulkan command translation (Phase 2.C+ work),
independent of this QEMU patch series.

## Runtime surface added by the series

Command line for a typical run:

    qemu-system-x86_64 \
        -machine q35,accel=kvm -cpu host -m 8G -smp 4 \
        -object memory-backend-memfd,id=ram0,size=8G,share=on \
        -machine memory-backend=ram0 \
        -device apple-gfx-pci,gpu_cores=8 \
        -drive if=virtio,file=macos15.qcow2 \
        -device isa-applesmc,osk='<osk>' \
        ...

`gpu_cores=N` caps Mesa lavapipe's worker-pool size via
`LP_NUM_THREADS` — useful on hosts with many cores where the
default (all cores) over-schedules against other guest work.
Memory-backend-memfd is recommended for the task-VA aliasing
path to stay on fast-path mremap semantics.

## Dependency

**Hard blocker:** this series depends on `libapplegfx-vulkan`
being discoverable via `pkg-config`. The library is not yet
distributed as a package; upstream submission is therefore
blocked on packaging of the dependency or a decision about
how to bundle it. See `LIBAPPLEGFX_DEPENDENCY.md`.

## Status

**Draft; library ready, packaging decision pending.**
Submission gated on:

  1. `libapplegfx-vulkan` being either accepted as a system
     package (Debian/Fedora/Alpine) or bundled as a
     submodule under `subprojects/`. The library itself is
     API-stable at commit `8edc43c`; the recommended path
     for initial review is Option 3 (vendored submodule) —
     see `LIBAPPLEGFX_DEPENDENCY.md`.
  2. The `pc-bios/apple-gfx-pci.rom` blob being replaced
     with an in-tree EDK2 build (currently a development
     placeholder, extracted from Apple's framework; not
     GPLv2-redistributable).
  3. Sign-off from the apple-gfx.m author on the split:
     this port is structured to avoid any changes to
     `apple-gfx.m` by intent, but a review pass would catch
     places where the core C logic should be pulled out of
     `apple-gfx.m` into a shared `apple-gfx-common.c`.

See `LIBAPPLEGFX_DEPENDENCY.md` for the dependency story and
`TESTING.md` for how to build and run the series.
