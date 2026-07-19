# The libapplegfx-vulkan dependency — RESOLVED (external-library model)

Historical note: earlier drafts of this document (see git history)
treated the library dependency as the series' hard blocker, weighing
distro packaging vs meson wrap vs vendored submodule, complicated by
the library's then-AGPL license and unreleased state.

That is resolved as of 2026-07-19. The v2 series consumes the library
exactly the way QEMU consumes virglrenderer:

  * **Released:** libapplegfx-vulkan **v0.1.0**, GitHub
    `MattJackson/libapplegfx-vulkan`, with a dist tarball on the
    release.
  * **License:** MIT (GPL-2.0-or-later-compatible; no relicensing
    contortions needed).
  * **ABI:** stable 20-symbol C ABI, semver from v0.1.0. Public
    headers: `include/libapplegfx-vulkan.h` +
    `include/applegfx/{export,version}.h`.
  * **Discovery:** `pkg-config` manifest `libapplegfx-vulkan.pc`
    (version 0.1.0), detected by the series' new meson feature option
    `libapplegfx` (`auto` by default; device silently skipped when the
    library is absent; `--enable-libapplegfx` hard-fails if missing).

Distro packaging remains desirable but is no longer a submission
blocker — upstream QEMU accepted virglrenderer on the same terms
before broad packaging existed. Whether maintainers additionally want
a meson wrap/subproject fallback is one of the two RFC questions in
`RFC-COVER.md`.
