# GUD Linux 4.9 Backport

## Goal

Backport the Linux host-side Generic USB Display (GUD) DRM driver to Linux 4.9 as a standalone loadable kernel module (`gud.ko`), initially targeting the OnePlus 6 Ubuntu Touch / Halium kernel.

The first milestone is deliberately narrow: get a GUD USB display detected, register a DRM/KMS device, expose a connector/mode, and display pixels without modifying DRM core or flashing a replacement kernel.

## Target environment

- Device: OnePlus 6 (enchilada)
- OS: Ubuntu Touch / Halium 9
- Kernel family: downstream Linux 4.9
- Architecture: arm64
- Driver type: out-of-tree module where possible
- Display goal: true DRM/KMS external display, not screen capture/mirroring

## Design principles

1. Keep the backport self-contained in `gud.ko` wherever possible.
2. Do not attempt to make Linux 4.9 look like modern DRM wholesale.
3. Reuse Linux 4.9 DRM primitives that already exist, especially `drm_simple_display_pipe` and atomic modesetting.
4. Model framebuffer/GEM handling after the Linux 4.9 `udl` DisplayLink driver where modern GEM shmem helpers are unavailable.
5. Skip optional modern features until basic output works.

## MVP scope

### Required

- USB device probe and GUD descriptor handling
- GUD protocol v1 control requests
- One DRM device
- One connector
- EDID / display mode enumeration
- One simple display pipe
- XRGB8888 support
- CPU-readable framebuffer storage
- Full-frame USB transfers
- Correct hot-unplug cleanup
- Build as `gud.ko` against the exact target kernel

### Deferred

- Damage tracking / partial updates
- LZ4 compression
- PRIME / dma-buf import optimization
- Multiple connectors
- Rotation
- TV properties
- Backlight integration
- Advanced suspend/resume behavior
- General compatibility with arbitrary 4.9 vendor kernels

## Compatibility strategy

### Already available in Linux 4.9

- DRM atomic modesetting
- `drm_simple_display_pipe`
- DRM connector/encoder/CRTC primitives
- USB control and bulk transfer APIs
- GEM core
- EDID helpers

### Adapt locally

Modern managed DRM helpers such as `drmm_*` should be replaced with explicit allocation and cleanup in the driver.

Modern format helpers should be replaced with a small local format layer for the formats actually needed by the MVP.

Modern unplug/lifetime helpers such as `drm_dev_enter()` / `drm_dev_exit()` should be mapped to safe Linux 4.9 lifetime handling.

### Reimplement

Modern `drm_gem_shmem_*` helpers are not available in 4.9. Implement a small GUD-specific GEM object using Linux 4.9 GEM APIs, following the approach used by the 4.9 `udl` driver:

- `drm_gem_object_init()`
- page allocation / `drm_gem_get_pages()` where appropriate
- `vmap()` for CPU access
- dumb-buffer creation and mmap support
- explicit cleanup

### Skip initially

Modern DRM damage helpers. The MVP should send the entire framebuffer on updates. Partial damage can be added after basic output is stable.

## Proposed source layout

```text
backport-4.9/
├── Makefile
├── gud_drv.c
├── gud_pipe.c
├── gud_connector.c
├── gud_internal.h
├── gud_protocol.h
├── gud_compat_4_9.h
└── gud_gem_4_9.c
```

The exact layout may change as implementation proceeds.

## Development workflow

### Cloud / host development

Most work does not require the phone:

1. Obtain the exact OnePlus 6 Ubuntu Touch kernel source and build configuration.
2. Prepare the arm64 cross compiler/toolchain.
3. Build the kernel once if required to generate matching build artifacts and `Module.symvers`.
4. Build the GUD backport as an external module.
5. Resolve compile errors and undefined symbols until `gud.ko` builds cleanly.

### Hardware testing

The OnePlus 6 is required only for runtime validation:

```bash
adb push gud.ko /home/phablet/
adb shell
sudo insmod /home/phablet/gud.ko
dmesg -w
```

Then connect the GUD device in USB host/OTG mode and verify:

- USB probe succeeds
- GUD descriptor is read
- DRM device is registered
- `/dev/dri/cardX` appears
- connector reports valid modes
- a DRM test program can modeset and display a test pattern

Only after standalone DRM testing works should Lomiri/Mir integration be investigated.

## Milestones

### M1 — Buildable module

`gud.ko` builds against the target 4.9 kernel with no unresolved required symbols.

### M2 — Probe and enumerate

A connected GUD device probes successfully and exposes a DRM device and connector.

### M3 — First pixels

A DRM/KMS test program can modeset the GUD display and show a static test image.

### M4 — Stable updates

Framebuffer updates work reliably, including disconnect/reconnect without crashes.

### M5 — Ubuntu Touch integration

Determine whether Mir/Lomiri detects the second DRM device and, if necessary, implement the integration needed for true external-display/convergence behavior.

## Definition of success

The initial project succeeds when an unmodified OnePlus 6 Ubuntu Touch Linux 4.9 kernel can load `gud.ko` and drive an external GUD display as a DRM/KMS output.
