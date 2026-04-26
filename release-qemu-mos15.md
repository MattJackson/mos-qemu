# v0.5 — first usable cut

QEMU 10.2.2 patches for running macOS 15 (Sequoia) as a guest in QEMU/KVM.

## What this release provides

Three patched files vs upstream QEMU 10.2.2:

- **`hw/misc/applesmc.c`** — fixes `GET_KEY_BY_INDEX_CMD` (was returning zero bytes, causing macOS to retry at ~1,800 errors/sec). Adds `#KEY` total-count and ~80 realistic iMac20,1 sensor values. **Measured impact: WindowServer CPU 509% → 6%**, kernel_task 70% → 2%.
- **`hw/display/vmware_vga.c`** — extends VMware SVGA II with capability bits the modern macOS driver expects, lifts resolution ceiling toward 4K.
- **`hw/usb/dev-hid.c`** — Apple PnP IDs (`0x05ac`) for keyboard/mouse/trackpad so macOS doesn't run Keyboard Setup Assistant on first boot.

## Build

Must build inside Alpine 3.21 (musl libc) — see README. **Do not** copy a glibc-built binary into the runtime container; the misleading "required file not found" error is the dynamic linker mismatch.

## License

AGPL-3.0; QEMU-derived files inherit GPL-2.0+. Combined work satisfies both.
