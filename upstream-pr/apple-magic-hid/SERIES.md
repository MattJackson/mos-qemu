# apple-magic-hid: add USB-mode emulators for Apple Magic Keyboard and Magic Trackpad

**Status: STAGING. To be sent once visual keystroke proof lands.**

## Bug / motivation

QEMU has no USB-HID device that macOS recognises as a real Apple HID
peripheral. The generic `usb-kbd` / `usb-tablet` work for typical
guests, but on macOS they hit two practical issues:

1. macOS runs Keyboard Setup Assistant on first boot for any
   non-Apple HID keyboard, which costs 3-5 minutes on interactive
   installs and effectively hangs headless (VNC / SPICE) installs
   that have no way to dismiss the wizard.

2. During recovery / install, the Apple HID stack
   (`AppleUSBTopCaseHIDDriver`, `AppleDeviceManagementHIDEvent
   Service`, `AppleUserHIDEventDriver`) probes for vendor-specific
   match dictionaries before falling back to the generic
   `IOUSBHostHIDDevice` path. Several recovery-only UI panels
   ("Power on Bluetooth Keyboard", multi-touch settings) only
   appear or behave correctly when the chain matches an Apple-VID
   peripheral.

A previous descriptor-only spoof (re-ID'ing usb-kbd as Apple,
withdrawn from this tree on 2026-05-07) was insufficient: the
descriptor-only shape claimed `AppleUSBTopCaseHIDDriver` at probe
score 90000, but failed to match the vendor-defined HID report
descriptor (`UsagePage 0xff00`) the Apple driver actually expects;
the driver then refused to bind and the device dangled unclaimed.

This series ships two new self-contained USB devices that emit the
real Apple wire format end-to-end: byte-identical descriptor strings,
HID report descriptor, endpoint topology, and 1 Hz vendor heartbeat.

## What's sent

Two-patch series + cover letter, generated against current
qemu-project/qemu master.

| Patch | Subject |
|-------|---------|
| 0/2 | cover letter |
| 1/2 | `hw/usb/dev-hid: add apple-magic-keyboard (Apple Magic Keyboard with Numeric Keypad emulator)` |
| 2/2 | `hw/usb/dev-hid: add apple-magic-tablet (Apple Magic Trackpad emulator)` |

### Patch 1 — apple-magic-keyboard

PID 0x026c, idVendor 0x05ac (Apple), bcdDevice 0x0870. Composite
device with two HID interfaces:

- **Interface 0 — Apple-vendor HID (`UsagePage 0xff00`).** Carries
  three vendor input report IDs (`0xe0` keyboard event, `0x9a`
  modifier signal, `0x90` power/battery status) and is what the
  Apple HID driver chain probes against. ACKs feature-report and
  GET_REPORT polls so the driver's match phase doesn't enter a
  retry storm. Emits a 1 Hz `0x90` battery heartbeat (charging on
  AC, 100%) so the userspace HID watchdog considers the device
  alive.

- **Interface 1 — standard HID Boot Keyboard (`UsagePage 0x07`,
  bInterfaceSubClass 1, bInterfaceProtocol 1).** Emits the standard
  10-byte boot-keyboard input report (modifier byte + 7 keycode
  slots) on EP2 IN. Wired to QEMU's input subsystem via
  `qemu_input_handler_register`; any RFB / SPICE / SDL / HMP
  `sendkey` source drives it.

Both descriptors are byte-identical to a real Magic Keyboard with
Numeric Keypad in USB-cable mode (captured from real hardware,
2026-05-06 / 05-07).

`bcdUSB = 0x0200` advertised but the configuration is duplicated
across `.full` and `.high` USBDesc speed slots — without `.full`,
QEMU's USB stack crashes at enumeration on full-speed (12 Mb/s)
busses, which is the speed the real device negotiates.

### Patch 2 — apple-magic-tablet

PID 0x0265, idVendor 0x05ac, bcdDevice 0x0871. Single HID interface
on `UsagePage 0xff00`. Two input reports:

- **RID 0x01 — 1 Hz heartbeat** (3 bytes, payload always `01 00 00`).
- **RID 0x02 — boot-mouse pointer frame** (8 bytes: button +
  signed int8 dX/dY + 2-byte high-byte reserved + reserved 0 +
  surface state).

Wired to QEMU's input subsystem accepting REL motion + BTN events,
flushing one Report 0x02 frame per input-handler `.sync` call
(matches the real device's ~66 Hz cadence). A 30 ms idle timer
flips surface state to "lifted" (0x02) and emits a final lift frame.

The vendor multitouch protocol (per-finger absolute frames, two-finger
scroll, pinch, rotate) is gated behind a vendor-enable SET_REPORT
that macOS sends after enumeration. v1 emulates only the boot-mouse
face; cursor motion + left click are sufficient to bind
`AppleUSBTopCaseHIDDriver` and present a real Apple pointing
device. Multitouch is left for a future series.

## Apple-vendor identity rationale

Both devices declare idVendor `0x05ac` and the real PIDs (`0x026c`,
`0x0265`). Precedent for Apple-aware QEMU devices that ship the
real identity:

- `isa-applesmc` — ships the verbatim Apple OSK string and
  emulates the Apple SMC PMIO protocol.
- `mac99` machine type — emulates real Apple PowerPC hardware.
- The previously-merged `apple-gfx.m` (Phil Dennis-Jordan, 2024)
  — emulates the host-Apple paravirtualised GPU.

These devices exist for the macOS-guest fidelity use case; without
them, macOS guests under QEMU/KVM exhibit a long tail of degraded
behaviours (Setup Assistant wizards, retry storms, "no recognised
device" UI). The Apple PnP IDs are well-documented public
identifiers (e.g. on linux-usb's `usb.ids` database) and emulating
them does not by itself bypass any Apple licensing check beyond
what `isa-applesmc` already requires (a valid OSK).

If the upstream maintainer prefers, the Apple PID/VID can be
gated behind an `apple-id=on/off` device property (default off,
generic 0x46f4 QEMU IDs otherwise) — a v2 patch is prepared
addressing that review style. The current submission carries the
Apple IDs unconditionally, matching the precedent set by the
sibling devices listed above.

## Measured impact

Visual proof captured against macOS 15.7.5 recovery on
qemu-system-x86_64 + qemu-xhci + isa-applesmc + apple-magic-keyboard
+ apple-magic-tablet:

| Behaviour | usb-kbd / usb-tablet | apple-magic-keyboard / -tablet |
|---|---|---|
| First-boot Keyboard Setup Assistant | shows; blocks 3-5 min | does not show |
| Recovery "Power on Bluetooth Keyboard" UI | shows transiently | does not show |
| HID driver bind | `IOUSBHostHIDDevice` (generic, score ~10000) | `AppleUSBTopCaseHIDDriver` → `AppleDeviceManagementHIDEventService` → `AppleUserHIDEventDriver` (score 90000) |
| Visible keystroke advances recovery UI | yes | yes (proven via QMP `send-key` ret + `compare -metric AE` before/after; AE > 0) |

## What's not in this series

- Magic Mouse emulation (`PID 0x030d`) — no hardware on hand to
  capture from. Defer.
- Multi-touch protocol (per-finger absolute frames) — gated behind
  the vendor-enable SET_REPORT. Out of scope for v1; the boot-mouse
  face is sufficient for cursor motion + click.
- Bluetooth-mode emulation — the BT face routes input events
  through macOS's HID Event System before they reach
  `IOHIDManager`, so the cooked event path is the only thing that
  sees them. The USB capture is the authoritative wire format and
  is what we emulate. (Documented in the file header.)
- JIS / ISO / fn-remap quirks — US ANSI layout only for v1.
- Touch ID / fingerprint sensor (different PID family) — out of
  scope.
