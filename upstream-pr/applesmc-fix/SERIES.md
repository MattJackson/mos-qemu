# applesmc: fix GET_KEY_BY_INDEX iteration and populate Apple SMC key set

**Status: SUBMITTED to qemu-devel 2026-05-06.**
Thread Message-ID: `20260507040153.14565-1-matthew@pq.io`.
Lore archive: https://lore.kernel.org/qemu-devel/20260507040153.14565-1-matthew@pq.io/

## Bug

The QEMU applesmc device implements just enough of the Apple SMC PMIO
protocol to satisfy the OSK boot check on older macOS versions. On
modern macOS guests (x86 10.14+, all of the 15.x series) the real
AppleSMC kext enumerates the SMC key space at boot via
`APPLESMC_GET_KEY_BY_INDEX_CMD` (0x12). The current device only
acknowledges `READ_CMD` (0x10) at the command port; every other
command falls through to the default arm of the switch and sets
`ST_1E_BAD_CMD`.

The macOS driver interprets the resulting 0x82 reply as "spurious
data" and enters a retry loop that floods the kernel log with
`kSMCSpuriousData (0x81) / kSMCKeyNotFound` errors at roughly 1800
events per second, pegging `kernel_task` at ~70% CPU and
`WindowServer` at ~509% CPU.

## What was sent

Two-patch series + cover letter, generated against current
qemu-project/qemu master (post v11.0.0 2026-04-21).

| Patch | Subject |
|-------|---------|
| 0/2   | cover letter |
| 1/2   | `hw/misc/applesmc: fix GET_KEY_BY_INDEX to return real keys, accept WRITE/TYPE commands` |
| 2/2   | `hw/misc/applesmc: populate Apple SMC key table` |

### Patch 1 — protocol-level fix

- Accept `WRITE_CMD`, `GET_KEY_BY_INDEX_CMD`, `GET_KEY_TYPE_CMD`
  at the command port (in addition to `READ_CMD`).
- Implement the indexed-iteration walker — returns real key names
  from `s->data_def`, or `APPLESMC_ST_1E_BAD_INDEX` (0xb8) once
  the index is past the end so the guest stops iterating.
- Implement `GET_KEY_TYPE` returning a 6-byte `type[4] + size[1] +
  attr[1]` response matching VirtualSMC's `kern_pmio.cpp` behaviour.
- Accept and log `WRITE_CMD` silently.
- Replace the unknown-key NOEXIST (0x84) reply with a zeroed
  payload of the requested length, logged at `LOG_UNIMP`.
- Route the BAD_CMD path through `qemu_log_mask(LOG_GUEST_ERROR)`.
- Fix `MSSD` initialiser typo (`"\0x3"` → `"\x03"`). The original
  literal was three bytes (`'\0'`, `'x'`, `'3'`) truncated to one
  (`'\0'`) by the size argument, so MSSD has been silently
  returning 0 since the device was introduced; the corrected value
  matches what a real iMac20,1 SMC reports.

### Patch 2 — populate the key table

Adds 94 keys plus the canonical `#KEY` total-count, sourced from
public SMC documentation, t2linux's applesmc kernel module,
VirtualSMC's `SMCDatabase`, and boot-log analysis.

Sensor values match a real iMac20,1 idle probe:
https://linux-hardware.org/?probe=999fc708a4&log=sensors

Categories:

- 28 temperature sensors (sp78). CPU 40 °C (TC0P) / 45 °C (TC0F)
  / 51 °C (TCXc). GPU 36 °C (TG0P) / 42 °C (TG0F/TG1F/TGDD).
  HDD 41 °C (TH0P/TH1C/TH1F). LCD 28-29 °C (TL0V/TL1V). Memory
  34-36 °C (TM0V/TM0P). Ambient 24 °C (TA0V). PSU 33 °C
  (Tp00/Tp2F). Generic sensors 33 °C (Ts*S/TS0V). VRM 50 °C
  (TVMD/TVmS/TVSL/TVSR; not in the probe, conservative
  estimate). Battery (TB0T/TB1T/TB2T) zero — no battery on a
  desktop iMac.
- 4 fan keys (FNum/F0Ac/F0Mn/F0Mx). 1 fan, current 1200 RPM
  (= F0Mn idle), min 1200, max 3600. fpe2 encoding
  (raw = RPM × 4, big-endian).
- 12 power-rail keys (PC0R/PCPC/PCPG/PCPT/PfCP/PfCT/PfGT/
  PfHT/PfM0/PfST/PSTR/PHDC) — present-with-zero.
- 6 DIMM keys (DM0P/DM0S/DM1P/DM1S/MD1R/MD1W) —
  present-with-zero.
- 11 SMC-internal bookkeeping (CLKH/DICT/RPlt/SBFL/VRTC/WKTP +
  cePn/cmDU/maNN/mxT1/zEPD) — present-with-zero.
- 13 motion-sensor / wireless (MSAc/MSAf/MSAg/MSAi/MSGA/MSHP/
  MSPA/MTLV/QCLV/QENA/WIr0/WIw0/WIz0) — present-with-zero;
  correct desktop-class answer.
- 3 write targets (HE0N/MSDW/NTOK).
- 2 power-management gates (HE2N for dGPU, WDTC for watchdog).
- 8 platform-identity / boot-probe keys (MPRO/MPRD/LGPB/BSLN/
  EPCI/BEMB/$Adr/RGEN/DPLM/MSSW/OSWD).
- 2 GPU temperature sensors (TGDD/TG0P), the `#KEY` total
  count (computed by walking `data_def` at realize), and a
  VirtualSMC-compatible `$Num`.

## Measured impact (macOS 15.7.5 guest, iMac20,1 profile)

| Metric              | Before     | After    |
|---------------------|-----------:|---------:|
| SMC errors / 5 s    |     9,225  |      2   |
| `kernel_task` CPU   |     70 %   |    ~2 %  |
| `WindowServer` CPU  |    509 %   |    ~6 %  |

## Backwards compatibility

Legacy macOS guests (10.11–10.13) that do not iterate the key space
boot unchanged. The original six keys (REV/OSK0/OSK1/NATJ/MSSP/MSSD)
are still present and respond with the same values, modulo the
MSSD typo fix in patch 1.

## Build

```sh
git clone https://gitlab.com/qemu-project/qemu.git
cd qemu
git am 0001-hw-misc-applesmc-fix-GET_KEY_BY_INDEX-to-return-real.patch
git am 0002-hw-misc-applesmc-populate-Apple-SMC-key-table.patch

mkdir build && cd build
../configure --target-list=x86_64-softmmu --enable-werror
ninja qemu-system-x86_64
```

Compile-tested macOS arm64 + clang 17 and Linux x86_64 + gcc 13
with `--enable-werror`.

## Recipients (per `scripts/get_maintainer.pl`)

- `qemu-devel@nongnu.org` (To, canonical mailing list)
- `stefanha@redhat.com` (Cc; recent contributor — `hw/misc/applesmc.c`
  is unmaintained per MAINTAINERS)

## Send mechanics (for future re-rolls)

Local laptop has no SMTP credentials configured. Send was via
SSH tunnel to `devtools-postfix-1` container on docker host:

```sh
ssh -fN -L 1587:172.20.0.3:587 docker
git send-email \
    --smtp-server=127.0.0.1 \
    --smtp-server-port=1587 \
    --smtp-encryption=none \
    --from="Matthew Jackson <matthew@pq.io>" \
    --to=qemu-devel@nongnu.org \
    --cc=stefanha@redhat.com \
    --no-chain-reply-to --suppress-cc=all --confirm=never \
    *.patch
pkill -f "ssh -fN -L 1587"
```

The postfix container is `boky/postfix` with
`RELAYHOST=smtp.protonmail.ch:587` + `matthew@pq.io` credentials,
`ALLOWED_SENDER_DOMAINS=pq.io`. All 3 messages got SMTP `250 OK`
from the relay (accepted for delivery; postfix forwards to
Protonmail upstream).

## Re-roll (v2) workflow

After applying review feedback locally:

```sh
ssh -fN -L 1587:172.20.0.3:587 docker
git -C <qemu-checkout> send-email \
    --smtp-server=127.0.0.1 --smtp-server-port=1587 --smtp-encryption=none \
    --from="Matthew Jackson <matthew@pq.io>" \
    --to=qemu-devel@nongnu.org --cc=stefanha@redhat.com \
    --in-reply-to='<20260507040153.14565-1-matthew@pq.io>' \
    --reroll-count=2 \
    --no-chain-reply-to --suppress-cc=all --confirm=never \
    v2/*.patch
pkill -f "ssh -fN -L 1587"
```

`--in-reply-to` thread-anchors v2 under v1's cover letter so
reviewers see the iteration history.

## Audience

`hw/misc/` maintainers and anyone running macOS guests on QEMU/KVM.
Fully backwards compatible: only visible guest-side change is that
GET_KEY_BY_INDEX_CMD now returns real key names instead of zeros.
