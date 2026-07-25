# Project Status

## Objective

Backport the host-side Generic USB Display (GUD) DRM driver to the OnePlus 6 Ubuntu Touch / Halium 9 Linux 4.9 kernel as a standalone `gud.ko` module. The phone is the USB host; the Raspberry Pi Zero 2 W is the GUD USB gadget and HDMI endpoint.

The MVP is one DRM/KMS external output using RGB565, full-frame USB transfers,
and a static 1280x720 test pattern. It must not require a replacement kernel
image or DRM core changes unless a documented blocker proves them unavoidable.

## Current State

- Branch: `linux-4.9-backport`
- Latest integrated implementation commit: `15f2a6e` (includes Ticket 5
  first-pixels commit `99b353f`).
- Ticket 1 build environment is implemented and its practical ABI gate is surpassed: the Ticket 2 driver built for and loaded on the phone's exact kernel ABI.
- Ticket 2 USB probe and disconnect implementation is complete and hardware-validated.
- Ticket 3, Linux 4.9 GEM and dumb-buffer support, is implemented and build-validated against the exact target kernel tree. Its userspace validation is deferred to Ticket 4 because no DRM node exists yet.
- Ticket 4 DRM/KMS registration was hardware-validated with the earlier
  XRGB8888 build. Ticket 5 RGB565 transfer and Pi presentation are now
  hardware-validated end to end on the OnePlus 6 and Raspberry Pi Zero 2 W.

## Cross-repository external-display priorities

`PROJECT-STATUS.md` is the canonical board for work that crosses the host
driver, Pi gadget, and Mir platform repositories.  Detailed implementation
notes stay in the owning repository, but every cross-repository item keeps the
same ID here and there.  This prevents a successful one-off hardware result
from being mistaken for a reliable end-user feature.

Use these states consistently:

- **planned**: scoped but not started;
- **in progress**: active implementation or investigation;
- **blocked**: waiting on a named dependency or reproducible evidence;
- **verified**: acceptance test passed with retained evidence; and
- **rolled back**: a useful experiment that is not enabled in the normal phone
  image.

| ID | Priority | Owner | State | Required acceptance evidence |
| --- | --- | --- | --- | --- |
| `XDISP-P0.1` | Make the Pi FunctionFS first bulk transfer reliable after a gadget rebind or phone reconnect. See `docs/superpowers/specs/2026-07-25-xdisp-p0-1-functionfs-rebind-design.md` and its implementation plan. | `gud-gadget` | blocked | Ten fresh rebind/reconnect cycles complete the first 64 KiB payload without host `-110` timeout. |
| `XDISP-P0.2` | Move GUD presentation off Mir's compositor commit path; retain only the newest pending frame on overload. | `mir-android2-platform-gud` | planned | Phone input and internal display remain responsive while the Pi is slow, absent, or returns an I/O error. |
| `XDISP-P0.3` | Discover the live GUD DRM card and handle remove/re-add; do not hard-code `card1` or use a symlink. | `mir-android2-platform-gud`, `gud` | planned | Reconnect succeeds when the card number changes, with no manual node changes or compositor restart. |
| `XDISP-P1.1` | Validate external-output geometry and Lomiri placement, including the intermittent narrow/cropped image. | `mir-android2-platform-gud`, `gud-gadget` | planned | A 1280x720 extended desktop fills the selected output correctly across repeated enable/disable cycles. |
| `XDISP-P2.1` | Improve usable performance with damage-aware updates, mode matching, measurement, and optional compression. | all three | planned | Recorded end-to-end FPS, latency, CPU use, and frame-drop behavior at the chosen mode. |

`XDISP-P0.1` diagnostic evidence (2026-07-25) narrows the active failure to
the Pi: the OnePlus submitted and successfully completed the first 64 KiB bulk
URB, while Pi `gud-drm` entered its 512-byte FunctionFS receive loop without
reporting aggregate payload completion. The next `SET_BUFFER` control request
then timed out. DWC2 subsequently timed out while stopping the OUT endpoint,
and the Pi Oopsed with `0x5a` corruption visible in allocator state. Raw logs
are ignored under `backport-4.9/env/local/evidence/`.

**Decision (2026-07-25):** take the userspace-only repair path first. Disable
automatic Pi service restart, implement controlled UDC/FunctionFS shutdown and
signal cancellation, then test larger receive requests through staged
mini-cycles. Defer a custom Pi kernel (`g_dma=0` or a DWC2 patch) unless those
changes still reproduce host `-110`, DWC2 endpoint-stop timeouts, or a Pi
Oops. Keep the OnePlus module and Mir unchanged. The item remains **blocked**;
do not start `XDISP-P0.2` or mark verification.

Containment step 1 is deployed on the Pi as
`10-xdisp-p0.1-containment.conf` (SHA-256
`e1d23477b23e3aff1647262f0f9637129d07e968a1336910c393c71db3879430`).
After `daemon-reload`, systemd reported `Restart=no`; the service remained
active with the same PID and restart count. No service restart, UDC rebind, or
payload test was used to verify this containment setting.

Lifecycle-repair step 2 is implementation-complete in `gud-gadget` and its
vendored `usb-gadget`: systemd `SIGTERM` now actively unbinds the UDC,
failed unbinds remain retryable, gadget removal closes FunctionFS after
unbind, and endpoint owners are dropped before DRM cleanup. The affected
tests pass (9 `gud-drm`, 30 serialized `gud-gadget`, and the vendored library
target). The release artifact SHA-256 is
`5aae726497cb6ea7b21336fae49528a9b69e6592d6871bc1068e11f0aebf7a06`.
It is staged on the Pi as
`/home/cristian/gud-drm.xdisp-p0.1-step2-new` and is now active after explicit
authorization. The prior `7c990347...` binary is retained at
`/home/cristian/gud-drm.pre-xdisp-p0.1-step2-7c99034`.

The no-payload runtime teardown passed: SIGTERM unbound the UDC, FunctionFS
removal and endpoint-owner closure completed before DRM release, the service
restarted successfully, and the Pi kernel logged no DWC2 timeout or Oops.
Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-runtime-gate-2026-07-25/`.

The first post-payload Step 2 runtime test also passed. After forcing the
documented OnePlus host-mode node, the dynamic gate found `1d50:614d` at
`/sys/bus/usb/devices/1-1.2`, and the unchanged normal module created
`/dev/dri/card1`. One isolated 1280x720 RGB565 submission returned
`PAYLOAD_RC=0`; all 29 tiles completed. The subsequent controlled Pi service
restart completed UDC unbind, FunctionFS removal, and endpoint-owner release
before DRM release. The Pi retained its boot ID with no DWC2 timeout, Oops,
`5a5a`, or paging fault. The phone re-enumerated the gadget, recreated
`card1`, and logged no GUD `-110`. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-post-payload-runtime-2026-07-25/`.

Two non-fatal follow-ups were exposed: the stopping process briefly reported
status 1 after treating the shutdown-induced detach as a restart request, and
the repaired service's automatic Pi boot start exhausted `set_crtc` retries
with `EACCES` before a later manual start succeeded. Neither reproduced the
unsafe cleanup, but both remain separate service-lifecycle/reporting issues.
This one positive run determines the Step 2 result; it does not satisfy the
ten-cycle acceptance matrix. `XDISP-P0.1` therefore remains **blocked**, and
no later step or `XDISP-P0.2` is authorized by this result.

The outstanding post-payload previous-boot journal is now retained under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-predeploy-2026-07-25/`.
On the old shutdown path, DWC2 logged both endpoint-stop timeouts immediately
before `0x5a` allocator corruption caused repeated Oopses. The new Step 2
binary is now active and passed the single controlled post-payload restart
described above. The old crash evidence remains the comparison baseline; the
single passing run does not change the **blocked** status or authorize
`XDISP-P0.2`.

The proof of concept verified that Lomiri can expose an independent
`DisplayPort-2` output backed by GUD. It also froze or severely slowed the
phone because it did synchronous USB work in Mir's commit path, and it has
been rolled back from the phone. The detailed POC record and its constraints
are in `../mir-android2-platform-gud/GUD-EXTERNAL-DISPLAY-POC.md`.

Read first:

- `AGENTS.md`: project constraints and verification expectations.
- `LINUX-4.9-BACKPORT.md`: architecture, MVP, and compatibility strategy.
- `BACKLOG.md`: ordered milestones; update only with evidence-backed completion.
- The cross-repository priority table above: the source of truth for the
  extended-display work spanning all three repositories.
- `docs/superpowers/CROSS-REPOSITORY-WORKFLOW.md`: placement, lifecycle, and
  evidence rules for future cross-repository specifications and plans.
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
adds the state and synchronous RGB565 transfer path and has now completed a
full hardware framebuffer update to the Pi display pipeline.

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

Ticket 5 is complete for the first-pixels/full-frame milestone. The active
source uses RGB565 end to end: it advertises the RGB565 DRM format, creates
16-bpp dumb buffers, validates the two-bytes-per-pixel layout, sends GUD v1
state-check/state-commit and controller/display-enable requests, then uploads
complete-row rectangles after `GUD_REQ_SET_BUFFER`.

On the OnePlus 6, the host transfer path uses a coherent DMA bounce buffer and
an explicit URB with `URB_NO_TRANSFER_DMA_MAP`. This replaced `usb_bulk_msg()`,
which caused the phone xHCI controller to remap the coherent buffer and return
`-EAGAIN`.

The initial fresh test reached the host bulk URB but timed out with `-110`.
Pi diagnostics isolated two FunctionFS failures: native AIO returned an invalid
completion despite the DWC2 controller receiving packets, and reopening the
already-enabled bulk endpoint could block indefinitely. The Pi now reuses the
endpoint file opened during FunctionFS initialization and performs synchronous
512-byte reads. It also binds USB only after DRM CRTC/framebuffer initialization
has succeeded, avoiding a transient half-started gadget during host enumeration.

The final clean retest rebooted the Pi, deployed the fix, forced the OnePlus
through device then host mode, and obtained a fresh `1d50:614d` probe and
`/dev/dri/card1`. `gud-kms-fill` uploaded the complete 1280x720 RGB565 frame.
Pi logs recorded every payload read, including representative 64,000-byte tiles
in 125 packets taking 5--11 ms; every tile was copied/scaled and the back
buffer was presented. The phone kernel log had no new GUD `-110` atomic-update
or bulk-transfer failure. This is the required end-to-end hardware evidence.

The remaining work is performance and robustness: damage tracking, LZ4,
throughput measurement, unplug-during-payload behavior, and upstreaming or
replacing the local `usb-gadget` endpoint-file extension.

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
