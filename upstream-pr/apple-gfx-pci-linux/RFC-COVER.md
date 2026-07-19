# RFC cover draft — apple-gfx-pci on Linux hosts (SEND after lagfx v0.1.0 tags)

Subject: [RFC PATCH 0/9] hw/display: Linux-host support for apple-gfx via libapplegfx-vulkan

To: qemu-devel@nongnu.org
Cc: phil@philjordan.eu (apple-gfx maintainer), hw/display maintainers per get_maintainer.pl

Body sketch:

QEMU gained apple-gfx (hw/display/apple-gfx.m) for macOS hosts, backed by
Apple's ParavirtualizedGraphics.framework. This series adds the Linux-host
counterpart: the same guest-visible PCI device, backed by libapplegfx-vulkan —
an MIT-licensed, clean-room reimplementation of the host side of the PVG
protocol with a Vulkan rendering backend (works on lavapipe; no GPU required).

Consumption model mirrors virglrenderer: external library, pkg-config
detection, new meson option 'libapplegfx' (auto/enabled/disabled), device
compiled only when the library is present. Library: semver ABI from v0.1.0,
pkg-config file, release tarballs. https://github.com/MattJackson/libapplegfx-vulkan

Unmodified macOS guests (stock AppleParavirtGPU.kext) attach, create a Metal
device, and render through the translated Vulkan path.

Question for maintainers before the non-RFC respin: preferred packaging shape —
(a) hard external dep as proposed, (b) meson subproject/wrap fallback, or
(c) both. Also happy to align device naming/props with the macOS-host device
where that helps libvirt.

9 patches: device split into PCI shim + common helpers; meson/Kconfig wiring;
trace-events; docs; qtest probe smoke test.
