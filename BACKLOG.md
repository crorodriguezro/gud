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
- [x] Support RGB565 for the active USB-transfer MVP.
- [x] Replace modern format helpers with a minimal local format layer.
- [x] Implement atomic check/update callbacks compatible with 4.9.

**Source/basic-ioctl evidence:** the Pi hardware test creates `/dev/dri/card1`;
the GUD card passes caps, dumb-buffer map/write/destroy, XRGB8888 framebuffer,
KMS enumeration, atomic-request build, atomic test-only validation, and a real
state-applying atomic commit. The synchronous no-transfer completion path now
passes on Linux 4.9 without `WARNING:` or `flip_done` timeout evidence.
`modetest -M gud -c -p` reports one connected virtual connector, one CRTC and
primary plane, `XR24` (XRGB8888), and preferred 1280x720@60; the full
`gud-kms-smoke` run completes its atomic modeset. The matching `dmesg` interval
contains no `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free` record.
Ticket 4 hardware acceptance is complete for the earlier XRGB8888 build. The
active Ticket 5 source has since changed the buffer and wire format to RGB565;
that format change still needs its own phone runtime evidence.

## P0 — Connector and mode enumeration

- [ ] Read GUD connector descriptor(s).
- [ ] Support one connector in the MVP.
- [ ] Read EDID when supplied by the GUD device.
- [ ] Fall back to GUD mode enumeration when needed.
- [ ] Register connector and attach it to the simple-pipe encoder.
- [ ] Implement 4.9-compatible detect/hotplug behavior.

**Done when:** `modetest` or equivalent reports the external connector and valid modes.

## P0 — First framebuffer transfer (Ticket 5)

- [x] Implement GUD state check/commit requests.
- [x] Implement display/controller enable requests.
- [x] Map the current framebuffer for CPU access.
- [x] Send a full framebuffer with `GUD_REQ_SET_BUFFER` plus USB bulk transfer.
- [x] Split transfers on complete-row rectangles when the GUD device's maximum buffer size requires it.
- [ ] Skip DRM damage helpers initially.

**Current evidence:** the active RGB565 source builds and contract tests pass.
The OnePlus host path uses an explicit DMA-mapped URB with
`URB_NO_TRANSFER_DMA_MAP`; this replaced the earlier `usb_bulk_msg()` path,
which discarded the DMA address of its coherent bounce buffer and failed with
`-EAGAIN` on the phone xHCI controller.

**2026-07-24 hardware acceptance:** after rebooting the Pi, deploying the
FunctionFS fix, and forcing a clean OnePlus device-to-host transition, the
phone freshly probed `1d50:614d` and created `/dev/dri/card1`. The GUD KMS
fill test transferred the complete 1280x720 RGB565 update as complete-row
rectangles. The Pi received each 512-byte bulk packet through FunctionFS,
logged 5--11 ms receives for representative 64,000-byte tiles, copied/scaled
every tile, and presented the back-buffer swap. The phone reported no new
`GUD atomic update failed: -110` or bulk-transfer error. This is end-to-end
host-driver, USB, FunctionFS, and Pi-DRM evidence; the FunctionFS incident is
documented in the Pi gadget repository.

**Done:** full RGB565 framebuffer transfer and presentation are hardware
validated. Damage tracking remains deferred as a performance improvement.

## P1 — Lifetime and hot-unplug safety

- [ ] Replace modern `drm_dev_enter()` / `drm_dev_exit()` semantics with safe 4.9 handling.
- [ ] Cancel pending work before freeing device state.
- [ ] Test unplug during framebuffer transfer.
- [ ] Test repeated disconnect/reconnect.
- [ ] Test module unload after device removal.

**Done when:** stress unplug/replug does not crash or use freed memory.

## P1 — Ubuntu Touch hardware validation

- [x] Deploy the module to the OnePlus 6 and load with `insmod`.
- [x] Verify USB host/OTG mode.
- [x] Verify GUD device probe in `dmesg`.
- [x] Verify `/dev/dri/cardX` creation.
- [x] Run an independent KMS test before involving Lomiri.

## P1 — Mir/Lomiri external-display integration

Mir's Android-HWC platform does not enumerate the separately registered GUD
card automatically. The selected approach is a separate
`mir-android2-platform-gud` fork that advertises a software DisplayPort-like
HWC output and presents that output through GUD. The feasibility POC exposed a
real independent `DisplayPort-2` output in Lomiri, but it is rolled back and
not usable yet: it performs a blocking USB update on the compositor commit
path and hard-codes the DRM node. See `PROJECT-STATUS.md` (`XDISP-*`) for the
cross-repository board and `docs/lomiri-gud-integration-options.md` for the
architecture decision.

- [ ] `XDISP-P0.1` — Pi FunctionFS first-transfer reliability is verified over
  ten fresh rebind/reconnect cycles. Owner: `gud-gadget`.
  - Laptop Gate A passed with LZ4; Gate B reproduced the impossible DWC2
    residual on the first upstream-host uncompressed 61,440-byte URB. Gate C
    then hung the Pi read at 15,360 bytes even though the host completed the
    entire URB successfully. The OnePlus backport is not a necessary trigger,
    and this is not a simple large-transfer threshold.
  - Gate D passed 1,440/1,440 uncompressed transfers and a clean stop,
    including 1,080 aligned 10,240-byte transfers with no ZLP or
    `URB_ZERO_PACKET`. Exact maxpacket termination alone is not the trigger;
    cancel the proposed OnePlus ZLP diagnostic.
  - Gate E and two identical fresh-boot repeats each passed six full 1280
    target frames at 12,800 bytes/25 packets and a clean stop: 2,592 target
    transfers with zero error. The observed aligned boundary is
    12,800 clean versus 15,360 failed.
  - Gate F proved that a 12,800-byte userspace prefix read does not safely
    consume a larger host transfer under `g_dma=1`. The one-shot `g_dma=0`
    Pi kernel then failed its first 16,274-byte compressed payload: the host
    completed the full URB, but FunctionFS returned 3,986 bytes and left an
    exact 12,288-byte DWC2 residual. Do not advance to OnePlus or the matrix.
  - Review a separately preserved OnePlus module variant that dynamically
    splits after compression and enforces an actual payload ceiling no larger
    than 12,800 bytes. If that cannot retain usable cadence, targeted Pi DWC2
    kernel work is required; do not ship the `g_dma=0` diagnostic.
  - The host-only variant is now implemented as a separate build under
    `variants/xdisp-lz4-12800/`. Offline LZ4 round-trip, payload-cap, row
    coverage, and exact-kernel build gates passed. Its first OnePlus hardware
    frame used four LZ4 payloads totaling 45,988 bytes, with a 12,380-byte
    maximum under the 12,800-byte cap. All four Pi reads completed, physical
    detach was clean, and the post-payload service restart exited zero without
    DWC2/vc4 failure.
  - Three fresh adaptive mini-cycles then passed. Their complete RGB565 frames
    used 4, 3, and 4 compressed complete-row rectangles; maximum actual
    payloads were 12,728, 12,718, and 12,347 bytes. Every payload completed in
    one 16 KiB FunctionFS read and returned to `Idle`; all three detached
    separate stop/start gates exited zero without host `-110`, DWC2/vc4
    failure, or Pi Oops. Start a new ten-cycle matrix from cycle 1 with the
    adaptive module. This does not yet unblock XDISP-P0.1.
  - The first adaptive matrix attempt passed cycle 1 (Pi rebind, five
    rectangles, 12,790-byte maximum) but failed cycle 2 before payload. On a
    reconnect-only reset, the Pi safely tore down the detached gadget and
    intentionally exited 1; containment's `Restart=no` left it failed, so the
    phone could not re-enumerate.
  - The userspace clean-detach repair and dedicated reconnect gate passed.
    Only a proven-idle detach exits zero under `Restart=on-success`; nonzero,
    poisoned, and crash outcomes remain contained. The service automatically
    changed from PID 2739 to PID 2836 on the same Pi boot, the OnePlus
    re-enumerated, and a fresh five-rectangle frame completed with a
    12,735-byte maximum and every receive returned to `Idle`. Restart the
    ten-cycle matrix from cycle 1; do not credit the old cycle-1 pass.
  - The replacement matrix passed 10/10: five Pi rebind and five OnePlus
    reconnect cycles, ten complete RGB565 frames, 45 matching payload
    completions and `Idle` returns, and a 12,799-byte matrix maximum. All five
    reconnects recreated the service automatically. No host `-110`, receive
    anomaly, DWC2/vc4 fault, Oops, pstore record, watchdog event, or Pi reboot
    occurred. This meets the adaptive diagnostic's technical matrix criteria,
    but retain the blocked status and do not start P0.2 until the standing
    no-verification instruction is explicitly lifted.
- [ ] `XDISP-P0.2` — the Mir presentation path is asynchronous and protects
  phone responsiveness when GUD stalls. Owner: `mir-android2-platform-gud`.
- [ ] `XDISP-P0.3` — host/GUD card discovery and reconnect do not assume
  `/dev/dri/card1`. Owners: `mir-android2-platform-gud`, `gud`.
- [ ] `XDISP-P1.1` — the external desktop has correct geometry and window
  placement at the selected mode. Owners: `mir-android2-platform-gud`,
  `gud-gadget`.

**Done when:** Lomiri uses the GUD monitor as a stable independent output with
the acceptance evidence recorded for every `XDISP-P0.*` item. A POC that merely
creates the output is not sufficient.

## P2 — Performance improvements

- [ ] `XDISP-P2.1` — record end-to-end frame rate, latency, CPU use, and
  frame-drop behavior for the extended-display path before claiming it is
  usable.
  - Run a controlled 512-byte versus 16 KiB FunctionFS read benchmark only
    after `XDISP-P0.1` is stable; the user currently reports no noticeable
    subjective difference.
  - Hold host, mode, compression, content, and duration constant; record
    presented FPS, dropped frames, median/tail latency, Pi and host CPU, USB
    throughput, and errors.
  - Quantify the actual performance gain from fewer rectangles/transfers.
    Compare safe fixed-row splitting against adaptive splitting while keeping
    every submitted payload at or below 12,800 bytes. Record rectangles and
    SET_BUFFER/bulk pairs per frame alongside FPS, latency, CPU, and USB
    throughput; do not infer causality from the earlier subjective
    one-frame-per-five-seconds observation.
  - Instrument the adaptive planner with per-frame compression-attempt,
    rejected-attempt, source-bytes-compressed, and compression-time counters.
    Before promoting this into the normal driver, target 90--95% of the
    payload cap instead of doubling the previous row count, reuse the recent
    measured compression ratio to predict the next rectangle, and benchmark
    video, scrolling, desktop activity, and incompressible patterns. Compare
    that recent-ratio/target-margin policy with the current doubling row hint.
    Report both the CPU cost of discarded compression attempts and whether
    fewer retries change end-to-end frame cadence.
  - A short 2026-07-26 direct-KMS hardware characterization passed without a
    transport or kernel fault: desktop reached 9.015 synchronous updates/s,
    scrolling 4.671 updates/s, and two incompressible frames 0.165 updates/s.
    The Pi matched all 367 `InFlight` entries with 367 `Idle` returns. Each
    noise frame required 144 rectangles and 287 compression attempts; one
    rejected all 287 attempts. Treat these as characterization numbers, not a
    completed P2.1 benchmark.
  - Replace or supplement the rate-limited per-frame diagnostic summaries
    with non-rate-limited cumulative counters before the comparison benchmark.
    The short run retained only 22 of 26 host frame summaries (353 of 367
    transfers), so its sampled attempt/rejection totals are lower bounds.
    Evidence:
    `backport-4.9/env/local/evidence/xdisp-p2.1-oneplus-motion-2026-07-26T1611COT/RESULTS.md`.
  - The same gate played ten predecoded 1280x720 RGB565 video frames through
    direct KMS at 2.015 synchronous updates/s. The frames produced 113 matching
    Pi transfers, 918,595 payload bytes, 246 compression attempts, and 133
    rejected attempts, with a 12,793-byte maximum and every receive returned
    to `Idle`. This proves raw-frame playback through GUD, not phone decoding
    or Lomiri/Mir video presentation.
  - Implement any 512-byte comparison with the current poison/teardown
    containment; do not redeploy the old artifact or treat it as the
    reliability fallback or normal default.
- [ ] Add damage tracking / partial framebuffer transfers.
- [x] Use RGB565 for the active MVP to reduce USB-transfer bandwidth.
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
