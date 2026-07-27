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

## 2. Keep discovery minimal and P0.3 explicit

- [x] Replace the POC's fixed `card1` checks with a bounded accessible-card
  scan that verifies libdrm's `gud` driver name.
- [x] Document absent-at-startup, failure, retry, reappearance, and shutdown
  behavior.
- [ ] Implement DRM remove/add subscriptions and external-output
  reconfiguration only under `XDISP-P0.3`; do not silently claim it here.

## 3. Verify offline behavior

- [x] Add focused component tests for coalescing, non-blocking submit,
  exception containment, and shutdown lifecycle.
- [x] Build the Android2 platform and run the focused tests in the tracked
  Ubuntu 20.04/UBports Focal ARM64 container. Commit `1bf8d53` adds the
  reproducible Dockerfile/helper and `c34344b` fetches the signed UBports
  archive/key over HTTPS; it builds the graphics module and the test binary,
  then runs `GudPresentationWorker.*` directly.
- [x] Syntax-check the standalone worker with C++14 and pthreads.

The retained source evidence is
`backport-4.9/env/local/evidence/xdisp-p0.2-source-2026-07-27T0000COT/`.
Its AddressSanitizer/UBSan harness passes coalescing, lifetime, non-blocking,
error, and shutdown checks. LeakSanitizer cannot run under this traced session,
and ThreadSanitizer cannot link because this host lacks `libtsan`; neither is
reported as a clean race/leak result.

The supported-project build evidence is
`backport-4.9/env/local/evidence/xdisp-p0.2-container-build-2026-07-27T0000COT/`.
On the source content committed as `mir-android2-platform-gud` `1bf8d53` and
`c34344b` (with async implementation `3fffb05`), the Focal container
`mir-android2-platform-gud-p02-build:ubuntu20.04-focal` used CMake 3.16.3 and
GCC 9.4.0. It built
`graphics-android2.so.16` (SHA-256
`77725859db7ac5149f20cd59ec8f56da0562e0250a1588bbab4dfd30fc729bd6`), resolved
its dependencies in that same container, and passed all five focused GTests.
The normal Fedora host remains unsuitable for directly mixing its libraries
with the UBports Mir/libhybris ABI; the container is the documented laptop
build environment.

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

**Stop conditions:** a failure in the worker must leave P0.2 **in progress**
until logs establish whether it is a buffer-lifetime, queue, KMS, or transport
fault. A card-number change or need to remove/re-add output redirects to P0.3;
no symlink/manual card path is an acceptable result. Any required payload,
kernel, or normal-module change is out of scope and stops this plan.
