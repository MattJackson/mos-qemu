# The libapplegfx-vulkan dependency

This patch series cannot land in upstream QEMU until the
`libapplegfx-vulkan` dependency is either:

1. **Accepted as a distribution-shipped system library**
   (Debian / Fedora / Alpine / Arch / Gentoo), or
2. **Bundled as a submodule under `subprojects/`**, or
3. **Inlined** (vendored into `hw/display/` or
   `subprojects/libapplegfx-vulkan/`) so QEMU builds the
   library itself with no external fetch.

The library reimplements the PGDevice / PGShellCallbacks
subset of Apple's ParavirtualizedGraphics.framework on top
of Mesa lavapipe. It is currently a single-maintainer project
and has not been packaged for any distribution.

## Current library state

As of this patch series draft the library has reached a
stable public API:

  * **HEAD:** commit `8edc43c` (Phase 2.B complete:
    Vulkan clear-color render target + readback — first-pixel
    path closes at the library level).
  * **Version pinnable:** the library ships `lagfx_version_*`
    accessors and a `pkg-config` manifest. Pinning by commit
    hash works today; semver tagging lands at 1.0.
  * **ABI-stable descriptor:** `lagfx_device_descriptor_t`,
    `lagfx_display_descriptor_t`, and `lagfx_shell_callbacks_t`
    have not broken for the entire Phase 1 + 2 development
    arc (reserved-field tail for forward extension, no
    renames, no silent-semantics changes). The last
    descriptor breakage predates Phase 1.A.1.
  * **License:** AGPL-3.0-or-later on the library; this
    patch series is GPL-2.0-or-later (standard QEMU). The
    two are compatible via the "or-later" clauses when
    QEMU links against the library as a system dep; for
    vendoring (Option 3 below) the library maintainer
    (the author of this patch series) can re-license the
    vendored tree to GPL-2.0-or-later on request.

## What the library provides

Headers and symbols (public API, `<libapplegfx-vulkan.h>`):

  * Opaque handles: `lagfx_device_t`, `lagfx_display_t`,
    `lagfx_task_t`
  * Device lifecycle: `lagfx_device_new`, `lagfx_device_free`,
    `lagfx_device_reset`
  * Task memory: `lagfx_task_create`, `lagfx_task_destroy`,
    `lagfx_task_map_host_memory`, `lagfx_task_unmap`
  * Display: `lagfx_display_new`, `lagfx_display_free`,
    `lagfx_display_cursor_position`,
    `lagfx_display_read_frame`
  * MMIO dispatch: `lagfx_mmio_read`, `lagfx_mmio_write`
  * Descriptor types: `lagfx_device_descriptor_t`,
    `lagfx_display_descriptor_t`,
    `lagfx_shell_callbacks_t`
  * Coord + range types: `lagfx_physical_range_t`,
    `lagfx_coord_t`
  * Introspection: `lagfx_version_{major,minor,patch}`,
    `lagfx_build_info`

All of these appear in the patch-5 realize path. The
guest-side protocol (MMIO register layout, IRQ semantics) is
authored inside the library so the QEMU side can stay
transport-agnostic.

## What upstream needs to decide

Three feasible shapes to merge:

### Option 1 — system package

`libapplegfx-vulkan` becomes a packaged system library in at
least one major distribution, and QEMU's
`hw/display/meson.build` uses `dependency('libapplegfx-vulkan',
required: false)` (as currently in the patch). Builds with
the package installed get the device; builds without skip it
silently. Zero new code in QEMU, matches how `virgl` /
`rutabaga` are handled.

**Prereq:** distro packaging. Blocker.

### Option 2 — meson subproject (wrap)

Add `libapplegfx-vulkan` as a meson subproject via a wrap
file under `subprojects/libapplegfx-vulkan.wrap` pointing at
an upstream git tag. Meson's subproject machinery handles
both the "system library found" and "subproject fetched"
cases transparently.

**Prereq:** library must have a stable ABI and a QEMU-
acceptable license (GPL-2.0-or-later; library relicensing
can be arranged, see "Current library state" above).

### Option 3 — vendored submodule (recommended for initial PR)

Add `libapplegfx-vulkan` as a git submodule under
`subprojects/libapplegfx-vulkan/`, built in-tree as part of
the device module. No external fetch at configure time; the
library source ships with QEMU and is covered by the same
review process as QEMU proper.

**Prereq:** license compatibility (library maintainer
consents to GPL-2.0-or-later relicense of the vendored
tree). The rest of the library is small enough (~5 kloc)
that a vendored subtree is reviewable in one pass.

## Recommendation

Pursue **Option 3** first for the initial upstream PR: it
keeps the review scope self-contained (reviewers do not
have to track an external repo) and puts the library
under QEMU's own code-review discipline. Fall back to
Option 2 once upstream is comfortable pulling the library
out to its own wrap, and to Option 1 once a distro picks
up packaging. The three paths are not mutually exclusive;
Option 3 can transition to Option 2 or 1 as the library
matures without further QEMU churn beyond the meson
subproject wrap file.

## Status check

As of this submission the library is:

  * Developed in its own repository under AGPL-3.0-or-later.
  * Not packaged in any distribution.
  * API-stable at the surface this patch series uses
    (Phase 2.B-complete at commit `8edc43c`); pre-1.0
    but not pre-alpha.
  * Ready to be re-licensed to GPL-2.0-or-later if vendored
    into QEMU (Option 3).

Upstream submission is therefore **blocked on Option 1 / 2 / 3**
packaging-path selection. The library itself is ready
whenever a path is chosen.
