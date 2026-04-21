# Testing the apple-gfx-pci-linux port

## Prerequisites

* Linux host with KVM, Mesa >= 23.0 (lavapipe driver), and a
  graphics stack that can be driven from a CPU rasteriser.
* `libapplegfx-vulkan` built and installed (see note below).
  The patch series is developed and tested against commit
  `8edc43c` (Phase 2.B complete).
* A macOS 15.x guest image configured to use Apple's
  `AppleParavirtGPU.kext` (the default on aarch64; on x86
  the kext must be present and matched).

## Building the library

Until `libapplegfx-vulkan` is packaged, build it from source:

```bash
git clone https://example.invalid/libapplegfx-vulkan.git
cd libapplegfx-vulkan
git checkout 8edc43c   # or a later stable tag
meson setup build --prefix=/usr
ninja -C build
sudo ninja -C build install
```

(Substitute the real library repository URL; the reference
implementation used to develop this patch series is
maintained out-of-tree pending upstream packaging.)

Verify `pkg-config --exists libapplegfx-vulkan` returns 0.

### Library-level test matrix (pre-QEMU)

The library ships a meson test suite that exercises every
moving part that QEMU depends on. Run before wiring it into
QEMU so that any regression is isolated to the library side:

    meson test -C build

Test coverage (`meson test --list`) currently includes:

| Test                | Phase    | What it proves                         |
|---------------------|----------|----------------------------------------|
| `header syntax`     | 0        | Public header parses under strict C    |
| `lifecycle smoke`   | 1.A      | Device + display create/destroy        |
| `memory task`       | 1.A      | Task VA reservation + memfd backing    |
| `memory coherence`  | 1.C      | `mremap()` page aliasing is live       |
| `gpu cores`         | -        | `thread_count` -> `LP_NUM_THREADS` env |
| `protocol dispatch` | 1.A      | Opcode decoder (207 `CHECK` asserts)   |
| `vulkan init`       | 1.B      | VkInstance + device + queue on lavapipe|
| `vulkan command`    | 1.B.2    | Command pool + empty submit round-trip |
| `vulkan render`     | 2.B      | Clear-colour render target + readback  |

Linux-only tests (`memory coherence`, `vulkan init`,
`vulkan command`, `vulkan render`) register as `SKIP` on
non-Linux hosts or on hosts without a loadable Vulkan ICD;
they build unconditionally to catch syntax regressions.

On a Linux host with Mesa lavapipe present, the suite runs
~277 `CHECK` assertions across the matrix and reports zero
failures at `libapplegfx-vulkan` HEAD.

## Building QEMU with the device

```bash
./configure --target-list=x86_64-softmmu --enable-kvm
make -j$(nproc) qemu-system-x86_64
```

Check the Kconfig symbol was picked up:

```bash
grep APPLE_GFX_PCI_LINUX build/config-host.h
# Expected: #define CONFIG_APPLE_GFX_PCI_LINUX 1
```

And that the library was found:

```bash
./build/qemu-system-x86_64 -device help 2>&1 | grep apple-gfx-pci
# Expected: apple-gfx-pci, bus PCI, ...
```

If `libapplegfx-vulkan` is absent at configure time the
device silently drops out of the build (meson
`required: false`); this is the expected behaviour on hosts
that lack the library.

## Running a guest

```bash
qemu-system-x86_64 \
    -machine q35,accel=kvm -cpu host \
    -m 8G -smp 4 \
    -object memory-backend-memfd,id=ram0,size=8G,share=on \
    -machine memory-backend=ram0 \
    -device apple-gfx-pci,gpu_cores=8 \
    -drive if=virtio,file=macos15.qcow2 \
    -device isa-applesmc,osk='<osk>' \
    ...
```

`gpu_cores=8` caps Mesa lavapipe's worker pool at 8 threads
(plumbed via `LP_NUM_THREADS`). Omit for lavapipe's default
(one thread per host core).

`memory-backend-memfd,share=on` is recommended but not
required: the task-VA aliasing path in `libapplegfx-vulkan`
uses `mremap()` which works with any anonymous memfd-backed
RAM region. Without a memfd backend the library falls back
to copy-on-map, which is correct but slower for
write-heavy DMA regions.

The option ROM is loaded automatically from
`$prefix/share/qemu/apple-gfx-pci.rom`. Override with
`-device apple-gfx-pci,romfile=/path/to/alt.rom` if needed.

## Expected behaviour

First-boot kernel log should show the guest kext attaching:

```
AppleParavirtGPU: IOPCIDevice attached (vendor 0x106B
    device 0xEEEE)
AppleParavirtGPU: option ROM present
AppleParavirtGPUControl::start() entered
```

(The Apple guest kext's Info.plist declares
`IOPCIMatch = "0xEEEE106B"`, which IOKit encodes as
`(device_id << 16) | vendor_id`. The device therefore
presents `vendor_id=0x106B, device_id=0xEEEE`; the
subsystem IDs are populated identically so both strict
and subsystem-aware IOKit match modes resolve.)

The QEMU host log (`-d trace:apple_gfx_*`) shows the
expected sequence:

```
apple_gfx_common_init apple-gfx-linux 16384
apple_gfx_pci_realize apple-gfx-pci: device realize
apple_gfx_create_task vm_size=... base=...
apple_gfx_map_memory task=... count=... offset=...
apple_gfx_read_memory gpa=... length=... dst=...
apple_gfx_write_memory gpa=... length=... src=...
apple_gfx_raise_irq vector=...
apple_gfx_new_frame
```

## Test matrix (this patch series)

| Phase   | Scope                      | State                                |
|---------|----------------------------|--------------------------------------|
| 1.A     | metal-no-op scaffold       | library `protocol-dispatch` passes   |
| 1.B     | Vulkan init                | library `vulkan-init` passes (Linux) |
| 1.B.2   | command pool + submit      | library `vulkan-command` passes      |
| 1.C     | task memory coherence      | library `memory-coherence` passes    |
| 2.A     | display opcode handlers    | library dispatch tests exercise it   |
| 2.B     | clear-colour render + read | library `vulkan-render` passes       |
| 2 E2E   | booted macOS guest + frame | awaiting out-of-tree first-pixel     |
|         |                            | harness (booted guest + noVNC capture)|

The QEMU-side frame-readback BH has been exercised against
the library's Phase 2.B render path (buffer flows all the
way from VkImage to `DisplaySurface`). The end-to-end
boot-a-macOS-guest gate is out-of-scope for this series and
depends on library-side Metal -> Vulkan command translation
work (Phase 2.C+ in the library, independent of QEMU).

## Regression check

On a host without `libapplegfx-vulkan` installed, the meson
gate should drop the device from the build silently, and
`-device help` should not list `apple-gfx-pci-linux`. The
build must succeed regardless of library presence.
