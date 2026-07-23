# Project Status

## Objective

Backport the host-side Generic USB Display (GUD) DRM driver to the OnePlus 6 Ubuntu Touch / Halium 9 Linux 4.9 kernel as a standalone `gud.ko` module. The phone is the USB host; the Raspberry Pi Zero 2 W is the GUD USB gadget and HDMI endpoint.

The MVP is one DRM/KMS external output using XRGB8888, full-frame USB transfers, and a static 1280x720 test pattern. It must not require a replacement kernel image or DRM core changes unless a documented blocker proves them unavoidable.

## Current State

- Branch: `linux-4.9-backport`
- Latest implementation/documentation commit: `c318005` (`docs: record GUD USB probe evidence`)
- Ticket 1 build environment is implemented and its practical ABI gate is surpassed: the Ticket 2 driver built for and loaded on the phone's exact kernel ABI.
- Ticket 2 USB probe and disconnect implementation is complete and hardware-validated.
- Ticket 3, Linux 4.9 GEM and dumb-buffer support, has not started.

Read first:

- `AGENTS.md`: project constraints and verification expectations.
- `LINUX-4.9-BACKPORT.md`: architecture, MVP, and compatibility strategy.
- `BACKLOG.md`: ordered milestones; update only with evidence-backed completion.
- `docs/superpowers/specs/2026-07-23-oneplus6-gud-usb-probe-design.md`: Ticket 2 requirements.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-usb-probe.md`: Ticket 2 implementation and acceptance procedure.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-host-backport.md`: full driver ticket sequence.

## Implemented

`backport-4.9/` now contains:

- Ticket 1 scripts for target capture, pinned kernel preparation, external-module build, and guarded deployment.
- `gud.ko` composed from `gud_drv.c`, not the removed no-op stub.
- Fixed USB matching for the validated Pi GUD gadget, `1d50:614d`.
- Packed GUD v1 display-descriptor definitions and explicit little-endian conversion.
- USB probe that locates bulk-out endpoint `0x01`, reads and validates the GUD display descriptor, and logs its capability limits.
- Disconnect that clears USB interface data before marking the private state unavailable and releasing it.
- Laptop-side Pi USB capture and phone-side probe deployment/evidence scripts, with hermetic shell tests.
- A Wi-Fi SSH workflow for phone testing when the Pi occupies the OnePlus 6's only USB-C port.

Ticket 2 intentionally contains no DRM/KMS/GEM objects, framebuffers, display modes, workqueues, asynchronous USB transfers, or framebuffer uploads.

## Hardware Evidence

The following selected raw evidence is intentionally versioned at its existing `env/local/` paths for future debugging. Other captures, kernel trees, build artifacts, modules, and unrelated runtime logs remain ignored.

- `backport-4.9/env/local/pi-usb/identity.env`: captured Pi USB identity `1d50:614d`.
- `backport-4.9/env/local/pi-usb/lsusb-v.txt`, `usb-devices.txt`, and `device-descriptors.bin`: raw laptop-side Pi descriptor capture.
- `backport-4.9/env/local/evidence/module-metadata.txt`: `gud.ko` metadata and unresolved-symbol list.
- `backport-4.9/env/local/evidence/probe-cycle-dmesg.txt`: full phone dmesg capture for the cable cycle.
- `backport-4.9/env/local/evidence/probe-unload-dmesg.txt`: focused GUD probe/disconnect and unload record.

The module was built for and loaded on:

```text
4.9.112-g6b190d86b SMP preempt mod_unload modversions aarch64
```

The focused GUD record shows:

- Six successful probes of `1d50:614d`.
- Five cable-removal disconnects followed by successful re-probes.
- A final disconnect during `usbcore: deregistering interface driver gud`.
- Descriptor values: GUD v1, bulk-out endpoint `0x01`, maximum buffer `8294400`, width `640-1920`, height `400-1080`.

The full dmesg capture has one `WARNING: CPU` in `dwc3_send_gadget_ep_cmd` at timestamp `70261`, before `gud` registered at `70940` and before the first Pi probe at `72066`. It is an unrelated pre-test USB-gadget warning, not a GUD driver warning. The GUD test interval from driver registration through unload has no `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free` entry.

## Ticket 2 Acceptance

Ticket 2 is complete: the OnePlus 6 recognizes the Pi gadget, validates its GUD v1 descriptor, and survives five remove/reconnect cycles plus final module unload without a GUD-related failure.

Do not overstate this result: Ticket 2 proves USB enumeration and lifetime handling only. It does not expose `/dev/dri/cardX`, create a framebuffer, enumerate a display mode, or display pixels.

## Next: Ticket 3

Implement Linux 4.9-native GEM and dumb-buffer support before any DRM/KMS registration:

1. Model CPU-readable backing storage on Linux 4.9 `udl`.
2. Define `struct gud_gem_object` around `struct drm_gem_object`.
3. Implement allocation, destruction, dumb-buffer creation, mmap, and a CPU mapping helper.
4. Reject unsupported imported dma-buf objects in the MVP.
5. Build against the exact phone kernel ABI and use only exported Linux 4.9 GEM interfaces.

Ticket 4 may not begin until Ticket 3 exposes usable GUD-owned buffers to DRM userspace.

## Constraints

- Do not patch DRM core unless a concrete, documented standalone-module blocker proves it necessary.
- Do not replace the installed kernel or flash a boot image.
- Preserve unrelated working-tree changes, including `opencode.json` and untracked plan files.
- Keep `BACKLOG.md` evidence-backed. Runtime logs are required for hardware milestones; first pixels and hot-unplug hardening also require hardware observation.
