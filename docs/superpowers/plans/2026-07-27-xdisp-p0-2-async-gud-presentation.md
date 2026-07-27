# XDISP-P0.2 Asynchronous GUD Presentation Plan

**Goal:** remove GUD CPU-copy/KMS/USB waiting from Mir's compositor commit
path while retaining exactly the newest queued external frame.

**Preconditions:** `XDISP-P0.1` is verified at commit `e1ee89f`; all hardware
work uses its payload ceiling of 12,800 bytes or less. Read the paired design
and the component procedure before a phone/Pi session.

## 1. Implement the bounded presentation boundary

- [x] Trace the blocking path from `HwcDevice::commit()` through
  `GudOutput::present_external()`, `Buffer::read()`, and
  `drmModeAtomicCommit()`.
- [x] Add a single-slot latest-frame worker. Submit replaces the pending frame
  and returns without waiting for the worker's presentation callback.
- [x] Retain the Android buffer with `shared_ptr` until the worker's CPU copy
  and atomic submission return; make the worker the sole owner of KMS state.
- [x] Catch worker-side exceptions, log them, discard stale KMS state, and
  retry only after a one-second cooldown.
- [x] Discard pending work and join active work on HWC/Mir teardown.
- [x] Keep the synthetic GUD external entry out of Android HWC `prepare()` and
  `set()` while leaving primary and virtual entries unchanged; render it in Mir
  and submit its resulting Android buffer only to the worker.

## 2. Keep discovery minimal and P0.3 explicit

- [x] Replace the POC's fixed `card1` checks with a bounded accessible-card
  scan that verifies libdrm's `gud` driver name.
- [x] Document absent-at-startup, failure, retry, reappearance, and shutdown
  behavior.
- [ ] Implement DRM remove/add subscriptions and external-output
  reconfiguration only under `XDISP-P0.3`; do not silently claim it here.

## 3. Verify offline behavior

- [x] Add focused component tests for coalescing, non-blocking submit,
  exception containment, shutdown lifecycle, and the synthetic HWC boundary.
- [x] Build the Android2 platform and run the focused tests in the tracked
  phone-matched Ubuntu 24.04/UBports Noble ARM64 container. Commit `1bf8d53`
  adds the reproducible container helper, `c34344b` fetches the signed UBports
  archive/key over HTTPS, and `d47b771` selects the versioned Mir 1 ABI the
  phone actually provides; it builds the graphics module and the test binary,
  then runs `GudPresentationWorker.*:GudHwcBoundary.*` directly.
- [x] Syntax-check the standalone worker with C++14 and pthreads.

The retained source evidence is
`backport-4.9/env/local/evidence/xdisp-p0.2-source-2026-07-27T0000COT/`.
Its AddressSanitizer/UBSan harness passes coalescing, lifetime, non-blocking,
error, and shutdown checks. LeakSanitizer cannot run under this traced session,
and ThreadSanitizer cannot link because this host lacks `libtsan`; neither is
reported as a clean race/leak result.

The supported-project build evidence is
`backport-4.9/env/local/evidence/xdisp-p0.2-container-build-2026-07-27T0000COT/`.
The original Focal artifact
`77725859db7ac5149f20cd59ec8f56da0562e0250a1588bbab4dfd30fc729bd6` passed
offline testing but required unversioned Mir sonames and was rejected by the
phone loader. The Noble container
`mir-android2-platform-gud-p02-build:ubuntu24.04-noble` at
`mir-android2-platform-gud` `d47b771` (async implementation `3fffb05`) built
the compatible `graphics-android2.so.16` (SHA-256
`cbcf648f26174413df718e5b5c71e41c6cf338dfe3e17dac32c52ef82581dac0`) and
passed all five focused GTests. The normal Fedora host remains unsuitable for
directly mixing its libraries with the UBports Mir/libhybris ABI; the container
is the documented laptop build environment.

### Boundary follow-up (2026-07-27)

The stopped deployment did not construct the worker: the old eager
`GUD POC output enabled` log and P0.2's buffer-gated worker lifecycle are not
equivalent. Source review consequently added worker-idle/start diagnostics and
the explicit synthetic-output/HWC separation above. The fresh Noble build
artifact from `mir-android2-platform-gud` commit `01d1f23` was
`2ce05a584bcea36b5138e2b53e4849e511671a79a91f03a212881bba3fba2d40`; the
focused `GudPresentationWorker.*:GudHwcBoundary.*` filter passed six tests.
This is not a hardware retry and does not attribute the previous binder/KGSL
health failure to HWC or the worker.

### Guarded retry result (2026-07-27)

- [x] Retest the commit-qualified boundary module after the mandatory dynamic
  VID/PID gate, then restore the packaged plugin without a Pi service action.
- [x] Add and exercise worker/KMS-stage observability: `0b09f77` proves that
  the worker processes a frame and opens GUD DRM, but fails before allocation,
  modeset, or USB in atomic KMS resource setup.
- [x] Expose the exact setup exception in retained Mir logs before attempting
  a transfer/error scenario; `7185800` reported the fixed 1280x720 mismatch.
  Commit `406b464` then selected the connector's advertised startup mode and
  crossed the KMS/transfer boundary, but its phone-health regression remains a
  stop condition rather than P0.2 acceptance.

## 4. Hardware acceptance (only after a commit-qualified plugin build)

- [ ] Follow the owning-repo procedure's mandatory OnePlus host/enumeration
  gate before loading the already-approved diagnostic module or starting Mir.
- [ ] Confirm the normal `/home/phablet/gud.ko` is untouched and no payload can
  exceed 12,800 bytes. Do not rebuild a kernel or resume P2 tuning.
- [ ] Capture a normal slow-output animation, an absent-at-startup launch, and
  a physical-disconnect/I/O-error case. Record phone input/internal-display
  responsiveness, Mir worker logs, phone dmesg, Pi journal/UDC state, commit
  ids, and artifact hashes.
- [ ] If the Pi service says `Poisoned` or a FunctionFS receive is in flight,
  do not stop, restart, reboot, shut down, or retry it. Preserve evidence and
  recover only by the documented physical reset path.

### 2026-07-27 stopped hardware attempt

The enumeration gate passed and the compatible Noble module loaded, but the
synthetic output appeared without a worker start or Pi receive session. In its
52-second test window the phone logged sustained binder `-12` allocation
failures and KGSL `-24` file-descriptor exhaustion. The test was immediately
rolled back to the packaged plugin. The normal OnePlus module was untouched and
the Pi service stayed active/configured without a restart. This failed
compositor-health gate leaves every hardware checkbox above unchecked; exact
logs are retained in
`backport-4.9/env/local/evidence/xdisp-p0.2-hardware-2026-07-27T1125COT/`.

**Stop conditions:** a failure in the worker must leave P0.2 **in progress**
until logs establish whether it is a buffer-lifetime, queue, KMS, or transport
fault. A card-number change or need to remove/re-add output redirects to P0.3;
no symlink/manual card path is an acceptable result. Any required payload,
kernel, or normal-module change is out of scope and stops this plan.

### Advertised-startup-mode retry (2026-07-27)

- [x] Replace the fixed synthetic 1280x720 requirement with the connected
  GUD connector's preferred advertised startup mode, falling back only to its
  first usable mode; test the selection rule without changing Pi modes.
- [x] Build commit `406b464` in the supported Noble/UBports container and run
  eight focused tests (`GudPresentationWorker.*:GudHwcBoundary.*:GudModeSelection.*`).
- [x] Run one bounded, commit-qualified hardware attempt after the VID/PID
  gate. It selected 1920x1080, transferred with maximum observed payload
  12,157 bytes, and left Pi receives Idle.
- [ ] Do not continue hardware acceptance from that run: repeated binder
  `-12` and KGSL `-24` failures require rollback and leave the slow-output,
  disconnect/I/O-error, reappearance, shutdown, and manual responsiveness
  rows unverified.
