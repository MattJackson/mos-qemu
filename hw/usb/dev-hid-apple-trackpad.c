/*
 * Apple Magic Trackpad 2 USB-mode emulator (PID 0x0265)
 *
 * Self-contained QEMU USB HID device that exposes the same wire-format
 * face as a real Apple Magic Trackpad 2 in USB-cable mode:
 *   - 4 USB HID interfaces (vendor / Mouse+Digitizer / vendor / vendor)
 *   - Per-interface HID Report Descriptors byte-identical to real device
 *     (verified against a real Magic Trackpad 2 USB pcap — see
 *     paravirt-re/library/apple-magic-hid/captures/usb-pcap-2026-05-10-multitouch.md)
 *   - SET_REPORT(feature, ID=0x02, payload={0x02, 0x01}) on Interface 1 enables
 *     "raw multitouch" mode; thereafter Report 0x02 carries multitouch frames
 *     of 12 + 9N bytes (N active fingers) instead of the 8-byte boot-mouse shape
 *
 * Driver bind: macOS PID match → AppleUSBTopCaseHIDDriver →
 * AppleDeviceManagementHIDEventService → AppleMultitouchTrackpadHIDEventDriver
 * (NOT the generic IOHIDPointing fallback).
 *
 * v1 scope: structural shape + SET_REPORT handler + 1-finger multitouch emit
 * driven from QEMU's input subsystem (single-pointer abs from VNC). Multi-
 * finger gestures (2-finger scroll, pinch, etc.) need a multi-pointer source
 * (SPICE multi-touch path or evdev passthrough) — out of scope for v1.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/input.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"
#include "hw/usb/hid.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"

/* ---------------------------------------------------------------------------
 * String table
 * ------------------------------------------------------------------------ */

enum {
    STR_AMTP_MFR = 1,
    STR_AMTP_PRODUCT,
    STR_AMTP_SERIAL,
    STR_AMTP_IFACE0,    /* "Device Management" historically */
    STR_AMTP_IFACE1,    /* mouse / digitizer / vendor */
    STR_AMTP_IFACE2,    /* vendor 0xd */
    STR_AMTP_IFACE3,    /* vendor 0x03 */
};

static const USBDescStrings desc_strings_amtp = {
    [STR_AMTP_MFR]     = "Apple Inc.",
    [STR_AMTP_PRODUCT] = "Magic Trackpad",
    [STR_AMTP_SERIAL]  = "CC2916600VBJ2XQA5",
    [STR_AMTP_IFACE0]  = "Device Management",
    [STR_AMTP_IFACE1]  = "Touchpad",
    [STR_AMTP_IFACE2]  = "Vendor",
    [STR_AMTP_IFACE3]  = "Vendor",
};

/* ---------------------------------------------------------------------------
 * HID Report Descriptors (byte-for-byte from real-device pcap)
 *
 * Source: paravirt-re/library/apple-magic-hid/captures/usb-pcap-2026-05-09.md
 * §3 "HID Report descriptors (per interface) — full hex". DO NOT modify
 * without re-capturing — Apple's macOS HID stack matches the literal byte
 * sequence and rejects close-but-not-equal descriptors.
 * ------------------------------------------------------------------------ */

/* Single-interface boot-mouse descriptor. 56 bytes.
 *
 * v3.0b: dropped the 4-iface "Magic Trackpad" surface (vendor TopCase
 * iface 0, multitouch iface 1, vendor 0xd iface 2, vendor 0x03 iface 3)
 * because:
 *   - Apple VID + iface 0 vendor TopCase triggers AppleUSBTopCaseHIDDriver
 *     which anchors the rest of the chain to either AppleMultitouch
 *     TrackpadHIDEventDriver (PID 0x0265, parser-type=1000 silently
 *     drops every report) or AppleUserUSBHostHIDDevice dext (which
 *     crashes on iface start). Neither path delivers cursor events.
 *   - Without iface 0/2/3, macOS can't anchor the Apple-specific stack
 *     and falls through to the generic boot-mouse driver chain
 *     (AppleHIDMouseEventDriver / IOHIDPointing) which DIRECTLY
 *     consumes 4-byte boot-mouse Report 0x02. Cursor moves.
 *
 * This is a single 3-button + dx + dy boot mouse with Report ID 0x02.
 * 4 reserved bytes appended so the wire frame is 8 bytes total
 * (matching our amtp_emit_boot_mouse output shape). */
static const uint8_t amtp_iface0_report_desc[] = {
    0x05, 0x01,                   /* Usage Page (Generic Desktop)        */
    0x09, 0x02,                   /* Usage (Mouse)                       */
    0xa1, 0x01,                   /* Collection (Application)            */
    0x09, 0x01,                   /*   Usage (Pointer)                   */
    0xa1, 0x00,                   /*   Collection (Physical)             */
    0x85, 0x02,                   /*     Report ID (0x02)                */
    0x05, 0x09,                   /*     Usage Page (Button)             */
    0x19, 0x01, 0x29, 0x03,       /*     Usage Min..Max (1..3)           */
    0x15, 0x00, 0x25, 0x01,       /*     Logical Min..Max (0..1)         */
    0x95, 0x03, 0x75, 0x01,       /*     Count=3 Size=1                  */
    0x81, 0x02,                   /*     Input (Data,Var,Abs) — buttons  */
    0x95, 0x01, 0x75, 0x05,       /*     Count=1 Size=5                  */
    0x81, 0x01,                   /*     Input (Const)         — padding */
    0x05, 0x01,                   /*     Usage Page (Generic Desktop)    */
    0x09, 0x30, 0x09, 0x31,       /*     Usage (X), Usage (Y)            */
    0x15, 0x81, 0x25, 0x7f,       /*     Logical Min..Max (-127..127)    */
    0x75, 0x08, 0x95, 0x02,       /*     Size=8 Count=2                  */
    0x81, 0x06,                   /*     Input (Data,Var,Rel)  — dx/dy   */
    0x95, 0x04, 0x75, 0x08,       /*     Count=4 Size=8                  */
    0x81, 0x01,                   /*     Input (Const)         — 4B pad  */
    0xc0,                         /*   End Collection (Physical)         */
    0xc0,                         /* End Collection (Application)        */
};

/* ---------------------------------------------------------------------------
 * USB descriptor structures
 * ------------------------------------------------------------------------ */

#define AMTP_VID                0x05ac
/*
 * v3.0 PID escape: 0x0265 is "Magic Trackpad 2" — macOS' kext
 * `AppleMultitouchTrackpadHIDEventDriver` has a PID-specific match on
 * this value, and once bound its `handleInterruptReport` silently
 * drops every report (parser-type=1000 expects an Apple-private
 * OSDictionary built by `MultitouchHID.plugin` from the descriptor —
 * see memory/research_apple_multitouch_parser_re.md). Using a
 * different Apple PID (0x0266 — one off Magic Trackpad 2, not in any
 * known multitouch match list) makes macOS fall through to generic
 * `AppleHIDMouseEventDriver`, which consumes our boot-mouse Report
 * 0x02 directly. Tradeoff: device is no longer "Magic Trackpad 2" in
 * `system_profiler`, but the cursor actually moves.
 *
 * QEMU device name stays `apple-magic-trackpad` because the device
 * implementation still emulates the Magic Trackpad 2 USB shape
 * (4-iface composite, boot-mouse iface 1, vendor TopCase iface 0).
 * Only the USB-host-visible PID is changed.
 */
#define AMTP_PID                0x0266
#define AMTP_BCD_DEVICE         0x0871

#define AMTP_EP_IFACE0_IN       1   /* 0x81 — vendor heartbeats */
#define AMTP_EP_IFACE1_IN       3   /* 0x83 — multitouch / boot mouse */
#define AMTP_EP_IFACE2_IN       4   /* 0x84 — vendor 0xd input */
#define AMTP_EP_IFACE2_OUT      4   /* 0x04 — vendor 0xd output */
#define AMTP_EP_IFACE3_IN       5   /* 0x85 — vendor 0x03 input */

static const USBDescIface desc_iface_amtp[] = {
    /* Single boot-mouse interface. bSubClass=1 bProto=2 (Boot Mouse) so
     * macOS' AppleHIDMouseEventDriver / IOHIDPointing binds directly
     * and consumes Report 0x02 from EP1 IN. */
    {
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_HID,
        .bInterfaceSubClass            = 1,    /* Boot */
        .bInterfaceProtocol            = 2,    /* Mouse */
        .iInterface                    = STR_AMTP_IFACE1,
        .ndesc                         = 1,
        .descs = (USBDescOther[]) {
            {
                .data = (uint8_t[]) {
                    0x09, USB_DT_HID, 0x10, 0x01, 0x00,
                    0x01, USB_DT_REPORT,
                    sizeof(amtp_iface0_report_desc), 0x00,
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | AMTP_EP_IFACE1_IN,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 8,
                .bInterval             = 1,    /* 125µs poll @ high-speed */
            },
        },
    },
};

static const USBDescDevice desc_device_amtp = {
    .bcdUSB                = 0x0200,
    .bMaxPacketSize0       = 64,
    .bNumConfigurations    = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = 0,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 250,    /* 500 mA */
            .nif                   = ARRAY_SIZE(desc_iface_amtp),
            .ifs                   = desc_iface_amtp,
        },
    },
};

static const USBDesc desc_amtp = {
    .id = {
        .idVendor          = AMTP_VID,
        .idProduct         = AMTP_PID,
        .bcdDevice         = AMTP_BCD_DEVICE,
        .iManufacturer     = STR_AMTP_MFR,
        .iProduct          = STR_AMTP_PRODUCT,
        .iSerialNumber     = STR_AMTP_SERIAL,
    },
    .full = &desc_device_amtp,
    .high = &desc_device_amtp,
    .str  = desc_strings_amtp,
};

/* ---------------------------------------------------------------------------
 * Device state + report-emit logic
 * ------------------------------------------------------------------------ */

#define TYPE_USB_APPLE_MAGIC_TRACKPAD "apple-magic-trackpad"

#define AMTP_QUEUE_DEPTH       32       /* power of two */
#define AMTP_REPORT_MAX_LEN    64       /* boot mouse 8B, multitouch up to ~64B */

typedef struct USBAppleMagicTrackpadReport {
    uint8_t data[AMTP_REPORT_MAX_LEN];
    uint8_t len;
} USBAppleMagicTrackpadReport;

typedef struct USBAppleMagicTrackpadState {
    USBDevice dev;

    /* Set by SET_REPORT(feature, ID=0x02, payload={0x02, 0x01}) on Interface 1. */
    bool multitouch_enabled;

    /* Single-pointer state from QEMU's input subsystem. VNC delivers ABS;
     * we map (abs_x, abs_y) directly into trackpad-coordinate fingertip.
     * prev_abs_{x,y} feed boot-mouse abs→rel delta computation when the
     * trackpad is still in pre-multitouch-enable mode (Report 0x02 boot
     * mouse shape). */
    int32_t  abs_x;
    int32_t  abs_y;
    int32_t  prev_abs_x;
    int32_t  prev_abs_y;
    bool     prev_abs_valid;     /* false until first ABS event seen, so we
                                  * don't compute a huge bogus delta from
                                  * prev=(0,0) on the first emit. */
    bool     button_left;
    bool     finger_down;       /* edge-triggered: was a touch active last frame? */
    bool     pending_event;

    /* Sequence/timestamp counter for the multitouch frame header.
     * Real device uses what looks like a free-running counter; macOS doesn't
     * appear to validate the value, so a monotonic increment is sufficient. */
    uint32_t mt_seq;

    /* Ring queue of pending Interface 1 input reports (boot-mouse OR
     * multitouch — same ring, the report's len field discriminates). */
    USBAppleMagicTrackpadReport queue[AMTP_QUEUE_DEPTH];
    unsigned q_head;
    unsigned q_tail;

    /* QEMU input handler binding. */
    QemuInputHandlerState *input_handler;

    /* Cached EP3 IN endpoint pointer for usb_wakeup() — pattern stolen from
     * dev-hid.c apple-magic-keyboard's `boot_intr`. Without wakeup, after
     * the host's first NAK on EP3 IN it parks the URB and never re-polls
     * (interrupt-IN endpoints are level-triggered in the xhci sense — the
     * device must signal "data ready" or the host stays parked). */
    USBEndpoint *intr_ep3;
} USBAppleMagicTrackpadState;

OBJECT_DECLARE_SIMPLE_TYPE(USBAppleMagicTrackpadState, USB_APPLE_MAGIC_TRACKPAD)

static inline unsigned amtp_q_count(USBAppleMagicTrackpadState *s)
{
    return (s->q_head - s->q_tail) & (AMTP_QUEUE_DEPTH - 1);
}

static inline bool amtp_q_empty(USBAppleMagicTrackpadState *s)
{
    return s->q_head == s->q_tail;
}

static inline bool amtp_q_full(USBAppleMagicTrackpadState *s)
{
    return amtp_q_count(s) == AMTP_QUEUE_DEPTH - 1;
}

static void amtp_enqueue(USBAppleMagicTrackpadState *s,
                         const uint8_t *data, uint8_t len)
{
    if (amtp_q_full(s)) {
        s->q_tail = (s->q_tail + 1) & (AMTP_QUEUE_DEPTH - 1);
    }
    USBAppleMagicTrackpadReport *r = &s->queue[s->q_head];
    memcpy(r->data, data, len);
    r->len = len;
    s->q_head = (s->q_head + 1) & (AMTP_QUEUE_DEPTH - 1);

    /* Notify QEMU's USB stack that EP3 IN now has data. Without this, the
     * host stays parked after its first NAK and never re-polls. */
    if (s->intr_ep3) {
        usb_wakeup(s->intr_ep3, 0);
    }
}

/* Boot-mouse Report 0x02 (8 bytes) — emitted BEFORE multitouch enable.
 *   byte 0: 0x02 (Report ID)
 *   byte 1: button mask (3-bit)
 *   byte 2: int8 dX
 *   byte 3: int8 dY
 *   byte 4..7: reserved 0
 */
static void amtp_emit_boot_mouse(USBAppleMagicTrackpadState *s,
                                 int8_t dx, int8_t dy, uint8_t buttons)
{
    uint8_t buf[8] = { 0x02, buttons, (uint8_t)dx, (uint8_t)dy, 0, 0, 0, 0 };
    amtp_enqueue(s, buf, sizeof(buf));
}

/* Multitouch Report 0x02 — emitted AFTER multitouch enable.
 *   byte 0:    0x02 (Report ID)
 *   byte 1:    button state (0x01 = pressed)
 *   bytes 2..7: 6-byte sequence/timestamp counter (little-endian)
 *   bytes 8 onwards: N × 9-byte per-finger records (Linux hid-magicmouse layout)
 *   last 4 bytes: trailer (purpose unverified — zeros are accepted by macOS
 *                 in observed wire frames)
 *
 * For v1 we emit either 0-finger (12-byte idle frame) or 1-finger (21-byte
 * active frame). 2+ finger paths require multi-pointer input which QEMU's
 * input subsystem doesn't expose via VNC.
 *
 * Per-finger record (9 bytes per Linux hid-magicmouse.c):
 *   byte 0..2: x — signed 13-bit packed: ((b1<<27 | b0<<19) >> 19)
 *   byte 2..4: y — signed 13-bit packed similarly
 *   byte 4: touch_major (low nibble) | touch_minor (high nibble of byte 5)
 *   byte 5: size (high nibble) | touch_minor (low nibble)
 *   byte 6: orientation (signed 4-bit, range -31..32 stretched)
 *   byte 7: pressure (0..253)
 *   byte 8: id (low 4 bits) | state (high 4 bits)
 *
 * v1 fixed values (good enough for cursor + click; gestures need a multi-
 * pointer source):
 *   touch_major = 0x07 (smallish ellipse)
 *   touch_minor = 0x05
 *   size        = 0x40 (mid)
 *   orientation = 0   (no axis tilt)
 *   pressure    = button_left ? 0xC0 : 0x40
 *   id          = 0
 *   state       = 0x4 (active touch) when finger down, 0x0 (lifted) otherwise
 */
static void amtp_pack_finger(uint8_t *out, int32_t x, int32_t y,
                             uint8_t pressure, uint8_t state, uint8_t id)
{
    /* x: signed 13-bit. Pack low 8 in byte 0, high 5 in low 5 of byte 1. */
    uint16_t xu = (uint16_t)(x & 0x1fff);
    uint16_t yu = (uint16_t)(y & 0x1fff);
    out[0] = xu & 0xff;
    out[1] = ((xu >> 8) & 0x1f) | ((yu & 0x07) << 5);
    out[2] = (yu >> 3) & 0xff;
    out[3] = 0x07;                  /* touch_major nibble + low minor */
    out[4] = 0x40 | 0x05;            /* size | touch_minor */
    out[5] = 0;                     /* orientation */
    out[6] = pressure;
    out[7] = (id & 0x0f) | ((state & 0x0f) << 4);
    out[8] = 0;                     /* trailing per-finger byte (observed pattern; verify when multi-finger gestures come online) */
}

/* x range from VNC abs (0..32767) to trackpad x-coord (-3678..3825 per real
 * Magic Trackpad 2 sensor range observed in Linux driver). Symmetric for y. */
static int32_t amtp_map_abs(int32_t v, int32_t out_min, int32_t out_max)
{
    int64_t span = (int64_t)(out_max - out_min);
    return out_min + (int32_t)((int64_t)v * span / 32767);
}

static void amtp_emit_multitouch(USBAppleMagicTrackpadState *s)
{
    uint8_t buf[AMTP_REPORT_MAX_LEN];
    int n_fingers = s->finger_down ? 1 : 0;
    int frame_len = 12 + 9 * n_fingers;

    memset(buf, 0, frame_len);
    buf[0] = 0x02;                                  /* Report ID */
    buf[1] = s->button_left ? 0x01 : 0x00;          /* button state */
    /* 6-byte counter at bytes 2..7 (little-endian) */
    buf[2] = (s->mt_seq >>  0) & 0xff;
    buf[3] = (s->mt_seq >>  8) & 0xff;
    buf[4] = (s->mt_seq >> 16) & 0xff;
    buf[5] = (s->mt_seq >> 24) & 0xff;
    buf[6] = 0;
    buf[7] = 0x01;                                  /* observed: low byte of trailing field is 0x01 */

    if (n_fingers >= 1) {
        int32_t mt_x = amtp_map_abs(s->abs_x, -3678,  3825);
        int32_t mt_y = amtp_map_abs(s->abs_y, -2280,  2280);
        amtp_pack_finger(&buf[8], mt_x, mt_y,
                         s->button_left ? 0xC0 : 0x40,
                         /* state: 4=touching */ 4,
                         /* id */ 0);
    }
    /* trailer (last 4 bytes) — observed all-zero in some frames, varies in
     * others. macOS' AppleMultitouchTrackpadHIDEventDriver appears to ignore
     * non-zero trailing bytes; zero is safe. */

    s->mt_seq++;
    amtp_enqueue(s, buf, frame_len);
}

/* ---------------------------------------------------------------------------
 * QEMU input handler — VNC abs pointer → 1-finger multitouch frames
 * ------------------------------------------------------------------------ */

static void amtp_input_event(DeviceState *dev, QemuConsole *src,
                             InputEvent *evt)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN: {
        InputBtnEvent *btn = evt->u.btn.data;
        if (btn->button == INPUT_BUTTON_LEFT) {
            s->button_left = btn->down;
        }
        s->pending_event = true;
        break;
    }
    case INPUT_EVENT_KIND_ABS: {
        InputMoveEvent *m = evt->u.abs.data;
        if (m->axis == INPUT_AXIS_X) {
            s->abs_x = m->value;
        } else if (m->axis == INPUT_AXIS_Y) {
            s->abs_y = m->value;
        }
        s->finger_down = true;
        s->pending_event = true;
        break;
    }
    default:
        break;
    }
}

static void amtp_input_sync(DeviceState *dev)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);

    if (!s->pending_event) {
        return;
    }
    s->pending_event = false;

    if (s->multitouch_enabled) {
        amtp_emit_multitouch(s);
    } else {
        /* Pre-enable: boot-mouse Report 0x02 (8-byte shape: ID + button
         * + dx + dy + 4B reserved). VNC delivers ABS only; convert to
         * REL by tracking the previous-frame absolute and emitting the
         * delta. Scale: VNC abs is 0..32767 across the screen width
         * (~1080px on a 1920×1080 host), so 1px ≈ 30 abs units. We use
         * a /4 divisor (≈ 7-8 abs units per dx unit) — this gives a
         * snappy cursor while keeping each delta within the int8 range
         * a boot-mouse report can carry. Larger pointer jumps in one
         * frame get clamped to ±127. */
        int32_t dx = 0, dy = 0;
        if (s->prev_abs_valid) {
            int32_t dx_abs = s->abs_x - s->prev_abs_x;
            int32_t dy_abs = s->abs_y - s->prev_abs_y;
            dx = dx_abs / 4;
            dy = dy_abs / 4;
            if (dx > 127)  dx = 127;
            if (dx < -127) dx = -127;
            if (dy > 127)  dy = 127;
            if (dy < -127) dy = -127;
        }
        s->prev_abs_x = s->abs_x;
        s->prev_abs_y = s->abs_y;
        s->prev_abs_valid = true;
        amtp_emit_boot_mouse(s, (int8_t)dx, (int8_t)dy,
                             s->button_left ? 0x01 : 0x00);
    }
}

static QemuInputHandler amtp_input_handler = {
    .name  = "Apple Magic Trackpad 2",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = amtp_input_event,
    .sync  = amtp_input_sync,
};

/* ---------------------------------------------------------------------------
 * USB device callbacks
 * ------------------------------------------------------------------------ */

static void usb_apple_magic_trackpad_handle_reset(USBDevice *dev)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);
    s->multitouch_enabled = false;
    s->pending_event      = false;
    s->finger_down        = false;
    s->button_left        = false;
    s->mt_seq             = 0;
    s->prev_abs_x         = 0;
    s->prev_abs_y         = 0;
    s->prev_abs_valid     = false;
    s->q_head = s->q_tail = 0;
}

static void usb_apple_magic_trackpad_handle_control(USBDevice *dev, USBPacket *p,
                                                    int request, int value,
                                                    int index, int length,
                                                    uint8_t *data)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    /* GET_DESCRIPTOR(REPORT) — per-interface HID Report Descriptor lookup.
     * usb_desc_handle_control doesn't deliver these (HID-class extension);
     * we have to dispatch by wIndex (interface number) ourselves. macOS'
     * AppleUserUSBHostHIDDevice dext crashes with "::start fail" + IOUserServer
     * "server exit before start()" if this STALLs — the dext can't parse the
     * report layout and bails. Pattern mirrors dev-hid.c:1500 for
     * apple-magic-keyboard. */
    case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        if ((value >> 8) == 0x22 && index == 0) {
            uint16_t copy = length < sizeof(amtp_iface0_report_desc)
                          ? length : sizeof(amtp_iface0_report_desc);
            memcpy(data, amtp_iface0_report_desc, copy);
            p->actual_length = copy;
            return;
        }
        break;
    /* HID class SET_REPORT — macOS issues feature/Report-0x02/{0x02, 0x01} on
     * Interface 1 to enable multitouch raw mode. ANY Interface-1 SET_REPORT
     * with a {0x02, *} payload trips multitouch; Linux's hid-magicmouse uses
     * the same magic. */
    case HID_SET_REPORT: {
        uint8_t report_type = (value >> 8) & 0xff;
        uint8_t report_id   = value & 0xff;

        if (report_type == 0x03 && report_id == 0x02 &&
            index == 1 && length >= 1 && data[0] == 0x02) {
            s->multitouch_enabled = true;
        }
        /* ACK with success — even unknown SET_REPORTs (other report IDs,
         * other interfaces) get silently absorbed so a probe loop never
         * stalls waiting for a STALL. */
        p->actual_length = length;
        break;
    }

    /* HID class SET_IDLE / GET_IDLE — boilerplate. Accept any SET_IDLE; report
     * idle=0 (infinite) on GET_IDLE. macOS probes these on every HID iface
     * during AppleUSBTopCaseHIDDriver match-probe; STALLing trips a retry
     * loop that pushes the driver-chain attach into the BT-fallback path. */
    case HID_SET_IDLE:
        break;
    case HID_GET_IDLE:
        data[0] = 0;
        p->actual_length = 1;
        break;

    /* HID class GET_PROTOCOL / SET_PROTOCOL — accept SET; report protocol=1
     * (report mode, not boot) on GET. Same defensive reasoning as SET_IDLE. */
    case HID_GET_PROTOCOL:
        data[0] = 1;
        p->actual_length = 1;
        break;
    case HID_SET_PROTOCOL:
        break;

    /* HID class GET_REPORT — only Report 0x02 (boot mouse: button + dx +
     * dy + 4B reserved = 7B payload). Sizes must mirror the descriptor
     * exactly. */
    case HID_GET_REPORT: {
        uint8_t report_id = value & 0xff;
        uint16_t reply_len = 0;

        if (index == 0 && report_id == 0x02) {
            reply_len = 7;
        }

        if (reply_len == 0) {
            p->status = USB_RET_STALL;
            break;
        }
        if (reply_len > length) {
            reply_len = length;
        }
        memset(data, 0, reply_len);
        p->actual_length = reply_len;
        break;
    }

    default:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_apple_magic_trackpad_handle_data(USBDevice *dev, USBPacket *p)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);

    if (p->pid != USB_TOKEN_IN) {
        p->status = USB_RET_STALL;
        return;
    }

    /* Only Interface 1 EP3 IN carries cursor/multitouch data in v1. Other
     * IN endpoints return NAK so the driver doesn't queue garbage. */
    if (p->ep->nr != AMTP_EP_IFACE1_IN) {
        p->status = USB_RET_NAK;
        return;
    }

    if (amtp_q_empty(s)) {
        p->status = USB_RET_NAK;
        return;
    }

    USBAppleMagicTrackpadReport *r = &s->queue[s->q_tail];
    s->q_tail = (s->q_tail + 1) & (AMTP_QUEUE_DEPTH - 1);
    usb_packet_copy(p, r->data, r->len);
}

static void usb_apple_magic_trackpad_realize(USBDevice *dev, Error **errp)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);

    /* Cache EP3 IN for usb_wakeup() in amtp_enqueue(). */
    s->intr_ep3 = usb_ep_get(dev, USB_TOKEN_IN, AMTP_EP_IFACE1_IN);

    s->input_handler = qemu_input_handler_register(DEVICE(dev),
                                                   &amtp_input_handler);
    qemu_input_handler_activate(s->input_handler);
}

static void usb_apple_magic_trackpad_unrealize(USBDevice *dev)
{
    USBAppleMagicTrackpadState *s = USB_APPLE_MAGIC_TRACKPAD(dev);

    if (s->input_handler) {
        qemu_input_handler_unregister(s->input_handler);
        s->input_handler = NULL;
    }
}

static const VMStateDescription vmstate_apple_magic_trackpad = {
    .name = "apple-magic-trackpad",
    .unmigratable = 1,
};

static void usb_apple_magic_trackpad_class_initfn(ObjectClass *klass,
                                                  const void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_apple_magic_trackpad_realize;
    uc->product_desc   = "Apple Magic Trackpad 2 (USB-mode emulator, multitouch)";
    uc->usb_desc       = &desc_amtp;
    uc->handle_reset   = usb_apple_magic_trackpad_handle_reset;
    uc->handle_control = usb_apple_magic_trackpad_handle_control;
    uc->handle_data    = usb_apple_magic_trackpad_handle_data;
    uc->unrealize      = usb_apple_magic_trackpad_unrealize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc           = "Apple Magic Trackpad 2 (USB HID multitouch, "
                         "VID 0x05ac PID 0x0265)";
    dc->vmsd           = &vmstate_apple_magic_trackpad;
}

static const TypeInfo usb_apple_magic_trackpad_info = {
    .name          = TYPE_USB_APPLE_MAGIC_TRACKPAD,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBAppleMagicTrackpadState),
    .class_init    = usb_apple_magic_trackpad_class_initfn,
};

static void usb_apple_magic_trackpad_register_types(void)
{
    type_register_static(&usb_apple_magic_trackpad_info);
}

type_init(usb_apple_magic_trackpad_register_types)
