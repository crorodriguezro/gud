# Project Status

## Objective

Backport the host-side Generic USB Display (GUD) DRM driver to the OnePlus 6 Ubuntu Touch / Halium 9 Linux 4.9 kernel as a standalone `gud.ko` module. The phone is the USB host; the Raspberry Pi Zero 2 W is the GUD USB gadget and HDMI endpoint.

The MVP is one DRM/KMS external output using RGB565, full-frame USB transfers,
and a static 1280x720 test pattern. It must not require a replacement kernel
image or DRM core changes unless a documented blocker proves them unavoidable.

## Current State

- Branch: `linux-4.9-backport`
- Latest implementation commit: `26edd28` (`feat: add Linux 4.9 GUD DRM/KMS registration`)
- Ticket 1 build environment is implemented and its practical ABI gate is surpassed: the Ticket 2 driver built for and loaded on the phone's exact kernel ABI.
- Ticket 2 USB probe and disconnect implementation is complete and hardware-validated.
- Ticket 3, Linux 4.9 GEM and dumb-buffer support, is implemented and build-validated against the exact target kernel tree. Its userspace validation is deferred to Ticket 4 because no DRM node exists yet.
- Ticket 4 DRM/KMS registration was hardware-validated with the earlier
  XRGB8888 build. The active RGB565 format contract is implemented for Ticket
  5 but still requires phone runtime validation.

Read first:

- `AGENTS.md`: project constraints and verification expectations.
- `LINUX-4.9-BACKPORT.md`: architecture, MVP, and compatibility strategy.
- `BACKLOG.md`: ordered milestones; update only with evidence-backed completion.
- `docs/superpowers/specs/2026-07-23-oneplus6-gud-usb-probe-design.md`: Ticket 2 requirements.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-usb-probe.md`: Ticket 2 implementation and acceptance procedure.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-host-backport.md`: full driver ticket sequence.
- `docs/oneplus6-usb-host-gud-troubleshooting.md`: mandatory start-of-session USB enumeration gate, validated host-mode procedure, and Ticket 4 atomic diagnostics.

Every new phone-test session must run that troubleshooting document's
start-of-session enumeration gate before loading or testing `gud.ko`. Match the
Pi by `1d50:614d`; its sysfs path is dynamic and has appeared as both `1-1.2`
and `1-1.4`. Do not conclude that enumeration failed from one empty scan.

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
- A Linux 4.9-native local GEM layer with page-backed objects, lazy page pinning, cached CPU `vmap()`, 16-bpp RGB565 dumb-buffer creation, mmap-offset and fault callbacks, and explicit teardown.
- Rejection of imported dma-buf-backed objects with `-EOPNOTSUPP`; no PRIME import/export callbacks are present.
- Ticket 3 shell contract coverage and an ABI/symbol-audit workflow. Kbuild `.cmd` metadata is ignored rather than versioned.
- Ticket 4/5 DRM registration: one `DRIVER_ATOMIC` GUD card, one virtual connector, one simple display pipe, RGB565 only, and one preferred 1280x720@60 mode.
- A local GEM-backed framebuffer path that validates RGB565 layout and backing-object bounds before an atomic modeset can reference it.
- Linux 4.9 USB unplug lifetime handling through `drm_unplug_dev()`, retaining private state until the final DRM file release.
- Ticket 4 contract coverage and a libdrm phone smoke utility for dumb-buffer create/map/write/destroy plus atomic modeset validation.

Ticket 4's earlier XRGB8888 result intentionally contained no GUD
connector/EDID query, display-state request, framebuffer upload, USB bulk
transfer, workqueue, asynchronous USB transfer, or visible output. Ticket 5
now implements the state and synchronous RGB565 transfer path, but still makes
no visible-output claim.

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

## Ticket 3 Acceptance

Ticket 3 is complete at the source/build boundary:

- `gud.ko` builds against the captured `4.9.112-g6b190d86b` target tree with `gud_gem_4_9.o` linked.
- The GEM and USB contract tests pass, along with the existing environment/probe test suite.
- The module's new GEM and VM dependencies are audited against the matching target build and captured phone symbols.
- The local-only GEM callbacks are ready for Ticket 4 registration: `gem_free_object_unlocked`, `dumb_create`, `dumb_map_offset`, file `mmap`, and VM operations.

No phone runtime result is claimed for Ticket 3. It cannot expose or map a userspace buffer until Ticket 4 registers `/dev/dri/cardX`.

## Ticket 4 Status

Ticket 4 is complete at the source/build boundary:

- `gud.ko` builds against the captured `4.9.112-g6b190d86b` target tree with the DRM/KMS objects linked.
- The USB, GEM, DRM/KMS, and environment/probe contract suites pass.
- New DRM/KMS and helper symbol references are exported by the matching target kernel build.
- The phone smoke utility compiles with `-Wall -Wextra -Werror` and libdrm.
- Review findings were corrected: the driver advertises `DRIVER_ATOMIC`, USB disconnect uses `drm_unplug_dev()` to defer final private-state release, and framebuffer creation verifies its memory extent.
- The Linux 4.9 atomic state initialization gap is corrected with `drm_mode_config_reset()` after simple-pipe construction and before DRM registration.

Phone runtime acceptance covers the DRM node, dumb buffer, framebuffer,
topology/property enumeration, atomic validation, and a successful
state-applying atomic commit. The final captured package confirms:

1. The Pi enumerated as `1d50:614d` at the dynamic path `1-1.2`; `gud.ko`
   probed successfully and created `/dev/dri/card1`.
2. `modetest -M gud -c -p` reported one connected `Virtual-2` connector, one
   CRTC and primary plane, `XR24` (XRGB8888), and preferred 1280x720@60.
3. `gud-kms-smoke /dev/dri/card1` completed a dumb-buffer create/map/write and
   printed `atomic modeset succeeded`.
4. The matching kernel-log interval contains no `BUG:`, `Oops`, `WARNING:`,
   `lockdep`, or `use-after-free` record.

The raw captures are retained under `backport-4.9/env/local/evidence/` as
`drm-kms-modetest.txt`, `drm-kms-smoke.txt`, and `drm-kms-dmesg.txt`.

## Ticket 5 Status

Ticket 5 is in progress. The active source uses RGB565 end-to-end: it advertises
an RGB565 DRM format, creates 16-bpp dumb buffers, validates the tight
two-bytes-per-pixel layout, sends GUD v1 RGB565 state-check/state-commit and
controller/display-enable requests, then uploads each complete-row rectangle
after `GUD_REQ_SET_BUFFER`.

The transfer path uses a DMA-coherent USB bounce buffer because the target 4.9
host warned that a local GEM `vmap()` address was not DMA-capable
(`usb_hcd_map_urb_for_dma`). The initial arbitrary 64 KiB chunking was also
replaced: the FunctionFS gadget queues one bulk read for the exact length in
each `SET_BUFFER`, so splitting is only permitted on complete-row rectangles.

Fresh Pi service/debug evidence confirms that the userspace FunctionFS gadget
accepts RGB565 state-check, state-commit, controller/display-enable, and a
1280x720 `SET_BUFFER` of 1,843,200 bytes, then waits in `recv_payload`. The
remaining host-side diagnosis found that `usb_bulk_msg()` discards the DMA
address returned by `usb_alloc_coherent()` and causes core USB to remap the
buffer; the target xHCI returns `-EAGAIN` from that path. The source now uses a
synchronous explicit URB with `URB_NO_TRANSFER_DMA_MAP` and the coherent DMA
address. It must be rebuilt and retested on the OnePlus 6. No first-pixels
claim is made, and Ticket 5 remains incomplete until color bars appear and a
clean matching kernel-log interval is captured.

The initial 2026-07-24 retest could not reach the framebuffer stage: both a
fresh load and one clean unload/reload timed out while reading the Pi GUD
display descriptor (`-110`), so no `/dev/dri/card1` was registered. A phone
reboot restored Pi enumeration at `1-1.2`, a successful GUD descriptor probe,
and `/dev/dri/card1`. The RGB565 color-bar commit then reached the explicit DMA
bulk URB but timed out after three seconds (`GUD bulk transfer failed after 0
retries: -110`). This supersedes the `-EAGAIN` mapping failure: direct DMA
submission works, while the Pi gadget is not completing the payload read. The
Pi remains USB-enumerated and the GUD DRM node remains present, but Pi SSH
became unavailable after the transfer attempt, preventing live gadget-log
collection.

## Ticket 4 Atomic Diagnostic

The initial connector query reset the phone because the simple pipe's atomic
connector, CRTC, and plane state had not been initialized. Adding
`drm_mode_config_reset()` after simple-pipe construction corrected that Linux
4.9 initialization gap.

Fresh phone evidence now passes these stages without a kernel failure record:

- `caps`: open the GUD DRM card and enable universal-plane and atomic clients.
- `dumb`: create, map, write, unmap, and destroy a 1280x720 32-bpp dumb buffer.
- `fb`: create and remove an XRGB8888 framebuffer.
- `resources`, `connector`, `encoder-crtc`, `planes`, and `properties`: complete
  DRM/KMS object, fixed-mode, primary-plane, and property enumeration.
- `atomic-build`: create the mode blob and build the atomic request.
- `atomic-test`: execute Linux 4.9 atomic property validation without state
  application.
- `atomic-commit`: apply the real state change and return successfully.

The no-transfer pipe now consumes the pending DRM completion event
synchronously from `gud_pipe_update()`. Fresh ordered phone evidence passes
`atomic-commit` with exit code zero and contains no `WARNING:`, `flip_done timed
out`, `BUG:`, `Oops`, `lockdep`, or `use-after-free` record.

Do not confuse USB enumeration with DRM validation. At the beginning of every
session, force controller host mode and poll all USB device paths for
`1d50:614d`; only then run the KMS stages. A path such as `1-1.4` is historical,
not an invariant.

## Constraints

- Do not patch DRM core unless a concrete, documented standalone-module blocker proves it necessary.
- Do not replace the installed kernel or flash a boot image.
- Preserve unrelated working-tree changes, including `opencode.json` and untracked plan files.
- Keep `BACKLOG.md` evidence-backed. Runtime logs are required for hardware milestones; first pixels and hot-unplug hardening also require hardware observation.
