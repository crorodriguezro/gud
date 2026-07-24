# Linux 4.9 GUD Backport Backlog

## P0 — Establish the exact target build

- [ ] Identify the exact OnePlus 6 Ubuntu Touch kernel repository and commit.
- [ ] Capture `uname -a` from the phone.
- [ ] Capture `/proc/config.gz` from the phone.
- [ ] Reproduce the matching `.config`.
- [ ] Determine the compiler/toolchain used by the target build.
- [ ] Generate or obtain matching `Module.symvers` and generated headers.
- [ ] Build and load a trivial out-of-tree arm64 module as a sanity check.

**Done when:** a trivial `.ko` loads without `Invalid module format`.

## P0 — Audit GUD against Linux 4.9 DRM

- [ ] Inventory every modern GUD API dependency.
- [ ] Classify each dependency as available, wrapper-compatible, replacement-required, or deferred.
- [ ] Confirm which required Linux 4.9 DRM/USB/GEM symbols are exported to modules.
- [ ] Identify any unavoidable DRM core modifications.

**Done when:** we can state whether a standalone `gud.ko` is possible with the target kernel.

## P0 — Create standalone module skeleton

- [ ] Add external-module Makefile.
- [ ] Add the GUD protocol definitions needed by the host driver.
- [ ] Implement USB ID matching and `usb_driver` registration.
- [ ] Implement probe/disconnect skeleton.
- [ ] Read and validate the GUD display descriptor.

**Done when:** the module loads and recognizes a GUD USB device without registering DRM yet.

## P0 — Implement Linux 4.9 GEM/framebuffer layer

- [x] Define `struct gud_gem_object` around `struct drm_gem_object`.
- [x] Implement allocation and destruction.
- [x] Implement dumb-buffer creation.
- [x] Implement CPU mapping using 4.9-era GEM/page APIs and `vmap()`.
- [x] Implement mmap support required by DRM userspace.
- [x] Handle imported buffers conservatively or reject unsupported imports in the MVP.

Use the Linux 4.9 `udl` DisplayLink driver as the main reference for CPU-readable USB-display framebuffer memory.

**Done when:** the page-backed local GEM layer builds using exported target-kernel symbols and provides the callback contract Ticket 4 needs to expose userspace buffers. The first userspace allocation/map/write test is Ticket 4 acceptance after `/dev/dri/cardX` exists.

## P0 — Register DRM simple display pipe

- [x] Initialize `drm_device` using 4.9 APIs.
- [x] Initialize `drm_mode_config`.
- [x] Register one `drm_simple_display_pipe`.
- [x] Support XRGB8888 first.
- [x] Replace modern format helpers with a minimal local format layer.
- [x] Implement atomic check/update callbacks compatible with 4.9.

**Source/basic-ioctl evidence:** the Pi hardware test creates `/dev/dri/card1`;
the GUD card passes caps, dumb-buffer map/write/destroy, XRGB8888 framebuffer,
KMS enumeration, atomic-request build, and atomic test-only validation. The
real atomic commit remains blocked on Linux 4.9 `flip_done` completion timeout;
Ticket 4 is not hardware-complete until that path is fixed without warnings.

## P0 — Connector and mode enumeration

- [ ] Read GUD connector descriptor(s).
- [ ] Support one connector in the MVP.
- [ ] Read EDID when supplied by the GUD device.
- [ ] Fall back to GUD mode enumeration when needed.
- [ ] Register connector and attach it to the simple-pipe encoder.
- [ ] Implement 4.9-compatible detect/hotplug behavior.

**Done when:** `modetest` or equivalent reports the external connector and valid modes.

## P0 — First framebuffer transfer

- [ ] Implement GUD state check/commit requests.
- [ ] Implement display/controller enable requests.
- [ ] Map the current framebuffer for CPU access.
- [ ] Send a full framebuffer with `GUD_REQ_SET_BUFFER` plus USB bulk transfer.
- [ ] Split transfers when the GUD device's maximum buffer size requires it.
- [ ] Skip DRM damage helpers initially.

**Done when:** a static test pattern appears on the external display.

## P1 — Lifetime and hot-unplug safety

- [ ] Replace modern `drm_dev_enter()` / `drm_dev_exit()` semantics with safe 4.9 handling.
- [ ] Cancel pending work before freeing device state.
- [ ] Test unplug during framebuffer transfer.
- [ ] Test repeated disconnect/reconnect.
- [ ] Test module unload after device removal.

**Done when:** stress unplug/replug does not crash or use freed memory.

## P1 — Ubuntu Touch hardware validation

- [ ] `adb push` the module to the OnePlus 6.
- [ ] Load with `insmod`.
- [ ] Verify USB host/OTG mode.
- [ ] Verify GUD device probe in `dmesg`.
- [ ] Verify `/dev/dri/cardX` creation.
- [ ] Run an independent KMS test before involving Lomiri.

## P1 — Mir/Lomiri external-display integration

- [ ] Determine whether Mir enumerates the new DRM device automatically.
- [ ] Inspect Mir/Lomiri logs when the GUD connector appears.
- [ ] Test whether the output can be enabled as a second display.
- [ ] If not, identify the multi-GPU/multi-DRM limitation in the Ubuntu Touch Mir stack.
- [ ] Keep this work separate from the kernel backport where possible.

**Done when:** Lomiri can use the GUD monitor as a true external output, or the exact userspace limitation is documented.

## P2 — Performance improvements

- [ ] Add damage tracking / partial framebuffer transfers.
- [ ] Add RGB565 if beneficial for USB bandwidth.
- [ ] Add LZ4 compression.
- [ ] Benchmark CPU usage and frame rate on OnePlus 6.
- [ ] Investigate asynchronous USB transfers if synchronous bulk transfer becomes a bottleneck.

## P2 — Feature parity

- [ ] PRIME/dma-buf support.
- [ ] Rotation.
- [ ] Backlight.
- [ ] Multiple connectors.
- [ ] TV properties.
- [ ] Suspend/resume hardening.
- [ ] Generalize beyond the OnePlus 6 vendor 4.9 kernel.
