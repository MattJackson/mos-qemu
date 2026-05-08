# Testing apple-magic-keyboard / apple-magic-tablet

## Prerequisites

* Linux host with KVM enabled.
* QEMU built with `--target-list=x86_64-softmmu --enable-kvm`,
  including this series.
* A macOS 15.x install image / pre-built guest disk for the
  fidelity-test path (skip to "Linux-guest sanity" otherwise).
* A valid Apple SMC OSK for `isa-applesmc` (required by macOS
  guests; not by the keyboard / tablet themselves).

## Linux-guest sanity (no macOS required)

The descriptors and report descriptors should walk cleanly under
Linux's xHCI + USB-HID stack. Boot any small Linux guest with:

```
qemu-system-x86_64 \
    -enable-kvm -m 2G -smp 2 -machine q35 -cpu host \
    -kernel <vmlinuz> -initrd <initramfs> \
    -append 'console=ttyS0 quiet' \
    -drive file=/path/to/scratch.img,format=raw,if=virtio,snapshot=on \
    -device qemu-xhci,id=xhci \
    -device apple-magic-keyboard,bus=xhci.0 \
    -device apple-magic-tablet,bus=xhci.0 \
    -display none -serial stdio
```

In the guest:

```
$ lsusb -v -d 05ac:026c   # apple-magic-keyboard
$ lsusb -v -d 05ac:0265   # apple-magic-tablet
```

Expected:

- `idVendor 0x05ac Apple, Inc.` / `idProduct 0x026c` (or `0x0265`).
- `iManufacturer = "Apple Inc."`,
  `iProduct = "Magic Keyboard with Numeric Keypad"` /
  `"Magic Trackpad"`.
- Keyboard: two interfaces, the second `bInterfaceClass=3`
  `bInterfaceSubClass=1` (Boot) `bInterfaceProtocol=1` (Keyboard).
- HID report descriptors decode without complaint.

## macOS-guest fidelity test

Boot macOS with the new devices in place of `usb-kbd`/`usb-tablet`:

```
qemu-system-x86_64 \
    -enable-kvm -m 8G -smp 4 -machine q35 -cpu host \
    -device 'isa-applesmc,osk=<valid-OSK>' \
    -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE.fd \
    -drive if=pflash,format=raw,file=OVMF_VARS.fd \
    -drive file=opencore.img,format=raw,if=ide \
    -drive file=macos.qcow2,format=raw,if=virtio \
    -device qemu-xhci,id=xhci \
    -device apple-magic-keyboard,bus=xhci.0 \
    -device apple-magic-tablet,bus=xhci.0 \
    -vnc :0 -qmp unix:/tmp/qmp.sock,server=on,wait=off
```

### HID-stack bind probe (macOS guest)

In the guest, after boot:

```
$ ioreg -c IOUSBHostHIDDevice -l | grep -E 'AppleUSB|VendorID'
```

Expected:

- An `AppleUSBTopCaseHIDDriver` entry under the keyboard's
  registry path (probe score 90000), with
  `AppleDeviceManagementHIDEventService` and
  `AppleUserHIDEventDriver` chained beneath. The previous
  `IOUSBHostHIDDevice` (generic, score ~10000) entry is no
  longer the matching driver.

### Visible keystroke proof

The strongest visual proof is at recovery boot, where the
language-picker UI advances on `Return`. Using QMP:

```
# socat - UNIX-CONNECT:/tmp/qmp.sock
{"execute":"qmp_capabilities"}
{"execute":"screendump","arguments":{"filename":"/tmp/before.png"}}
{"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":"ret"}]}}
{"execute":"screendump","arguments":{"filename":"/tmp/after.png"}}
```

Then on the host:

```
$ compare -metric AE /tmp/before.png /tmp/after.png null:
```

Expected: AE >> 0 (typically 100k+ pixels differ as the panel
advances). Compare the same pair against an `usb-kbd` baseline
to confirm both keyboards drive UI; the apple-magic-keyboard
proof is that this works *without* incurring the first-boot
Keyboard Setup Assistant detour.

## Regression check

`-device usb-kbd` / `-device usb-tablet` must continue to behave
identically to current QEMU (no shared state, no shared vmstate
section). Running both old and new devices simultaneously on the
same xHCI controller must work — they do not collide on
bus/port assignment because each is a separate `USBDevice`.

A migration smoke test: cold-boot guest with apple-magic-keyboard
attached, type, then `migrate exec:gzip > /tmp/m.gz`. The
`unmigratable = 1` flag prevents migration of in-flight HID state
across releases (matches the existing `usb-kbd` behaviour); the
device cleanly fails the migrate step rather than silently
corrupting input state. This is intentional; documented in the
device's vmstate descriptor.
