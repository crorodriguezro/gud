# XDISP-P0.2 Asynchronous GUD Presentation Design

## Purpose

Keep Mir's Android-HWC compositor path independent of a slow, absent, or
failing GUD USB/KMS output. This work follows verified `XDISP-P0.1`; all real
OnePlus-to-Pi payloads remain at or below 12,800 bytes. It changes neither
kernel nor `gud.ko`, and it is not a performance-optimization task.

## Blocking path found in the POC

The POC's `HwcDevice::commit()` calls
`GudOutput::present_external(contents)` before it calls `hwc_wrapper->set()`.
That function previously called `Kms::present()` synchronously. `Kms::present`
CPU-read the Android gralloc buffer, converted all 1280x720 pixels to RGB565,
and then called `drmModeAtomicCommit()`. The GUD 4.9 driver's atomic update is
the path that submits USB/KMS work, so a USB transfer or I/O timeout held the
same commit path needed for normal phone composition. This is the source-level
explanation for the observed frozen internal display and input.

## Ownership and interface

- **Owner:** `mir-android2-platform-gud`.
- **Producer:** `HwcDevice::commit()` on Mir's compositor path.
- **Consumer:** one `LatestPresentationWorker<std::shared_ptr<Buffer>>`
  thread and its `GudPresenter`/`Kms` state.
- **Transport:** the existing GUD DRM driver and Pi gadget; this item does not
  alter either interface, payload shape, or payload limit.
- **Canonical status:** `../../PROJECT-STATUS.md`, `XDISP-P0.2`.
- **Component procedure:**
  `../../../../mir-android2-platform-gud/doc/XDISP-P0.2-GUD-PRESENTATION-TEST.md`.

`commit()` finds only the synthetic external buffer, retains its `shared_ptr`,
and replaces the one pending slot. It does not open a DRM device, map a GUD
buffer, read gralloc memory, convert pixels, or issue a KMS ioctl. The worker
removes that one frame from the slot and is the only thread that constructs,
uses, and destroys `Kms` and its DRM fd.

The synthetic GUD output is a Mir-rendered sink, not an Android physical HWC
display. `HwcDevice` therefore omits only that external `DisplayContents`
entry from its Android HWC `prepare()` and `set()` lists; primary and virtual
entries retain their ordinary HWC path. Mir still renders the synthetic entry
and gives its resulting Android buffer to the worker. This makes the boundary
explicit rather than relying on an unsupported HWC external slot being ignored.

## Invariants

1. There is at most one active frame and one pending frame. Replacing the
   pending `shared_ptr` immediately releases the superseded frame; queue growth
   is therefore bounded.
2. The active `shared_ptr<Buffer>` remains owned by the worker through
   `Buffer::read()`, RGB565 copy, and KMS submission. It cannot be freed while
   those operations use it.
3. Only the worker calls the serial KMS setup, CPU copy, atomic commit, and
   teardown paths. The producer mutex is never held while any of them run.
4. Presentation exceptions are caught inside the worker and reported by the
   Mir log callback. They do not unwind into `HwcDevice::commit()` or stop the
   worker.
5. Shutdown discards the pending frame, then joins the active worker before
   its KMS state is destroyed. This intentionally waits for an in-progress
   kernel ioctl to return, rather than risking an fd/framebuffer use-after-free.

## Device lifecycle

The small P0.2 discovery step scans accessible `/dev/dri/cardN` nodes and
accepts only a libdrm driver name of `gud`; no `card1` path is embedded in the
new presentation path. It is a safe startup/present-time lookup, not P0.3
hotplug management.

- **Absent at startup:** the HWC configuration sees no accessible GUD card, so
  it does not synthesize the external output and does not start a worker. The
  internal display follows its ordinary HWC configuration.
- **Disconnect during copy/commit:** the worker catches the failing operation,
  logs a dropped frame, destroys the KMS state/fd, and drops further frames for
  a one-second retry cooldown. The compositor continues.
- **Reappearance:** if the existing synthetic external Mir output remains
  alive, the next retry can scan and open the current GUD card. If GUD was
  absent at server startup, or if the output must be removed/re-added, P0.3
  must issue the configuration/hotplug change; P0.2 does not claim that
  lifecycle.
- **Mir shutdown:** `HwcDevice` calls `GudOutput::shutdown()`. The worker
  removes queued work and joins before KMS buffers and the fd are released.

## Out of scope

- Full DRM remove/add event handling, automatic external-output configuration,
  and reconnect acceptance are `XDISP-P0.3`.
- Damage tracking, compression, mode changes, cadence/FPS changes, and payload
  changes are P2 work. The `<= 12,800`-byte transport ceiling stays unchanged.
- Rebuilding either kernel, changing normal `/home/phablet/gud.ko`, or
  stopping/restarting a Pi process in a poisoned receive state are prohibited.

## Offline validation (2026-07-27)

The original tracked Ubuntu 20.04/UBports Focal build environment in
`mir-android2-platform-gud` commits `1bf8d53` and `c34344b` built the module and
passed the focused tests, but its artifact required unversioned Mir sonames and
was rejected by the Ubuntu Touch 24.04 phone loader. The supported laptop
environment is therefore the phone-matched Ubuntu 24.04/UBports Noble ARM64
container added by `d47b771`. It built the actual
`graphics-android2.so.16` module from worker implementation `3fffb05`, SHA-256
`cbcf648f26174413df718e5b5c71e41c6cf338dfe3e17dac32c52ef82581dac0`.
`LD_TRACE_LOADED_OBJECTS` on the phone resolved its versioned Mir 1 and Boost
1.83 dependencies before staging. The direct GTest filter
`GudPresentationWorker.*` passed five checks: newest-pending-frame coalescing,
non-blocking submit while the presenter is held, active-buffer
lifetime/superseded release, error containment with subsequent presentation,
and shutdown discard/join. Exact commands and raw output are retained in
`backport-4.9/env/local/evidence/xdisp-p0.2-container-build-2026-07-27T0000COT/`.
This validates the source/build boundary only; it is not phone/Pi responsiveness
or reconnect acceptance evidence.

### Follow-up boundary validation (2026-07-27)

After the rollback, source review established that the old POC logged `GUD POC
output enabled` during eager presenter construction, while P0.2 starts its
worker only after it obtains an external Android `mga::Buffer`. The failed
hardware session therefore proves that no P0.2 worker/KMS transfer happened;
it does not identify why the output had no submitted Android buffer. The
reported binder/KGSL faults remain a phone-health correlation, not a confirmed
worker cause.

The scoped follow-up makes the synthetic-output/HWC boundary explicit and adds
one-time worker-idle/start logs for the next hardware attempt. In a fresh
Noble build tree it produced `graphics-android2.so.16` SHA-256
`2ce05a584bcea36b5138e2b53e4849e511671a79a91f03a212881bba3fba2d40` from
`mir-android2-platform-gud` commit `01d1f23` and passed six focused checks under
`GudPresentationWorker.*:GudHwcBoundary.*`: the original five worker tests
plus the policy that only a synthetic external entry is excluded from Android
HWC. This is offline evidence only; it neither deploys the uncommitted module
nor claims resolution of the previous compositor health failure.

### Guarded hardware retries (2026-07-27)

The `01d1f23` boundary build started the worker and connected the synthetic
output without the prior binder/KGSL failure, but did not reach KMS setup or a
Pi receive. The follow-up `0b09f77` build (module SHA-256
`c7f8659faeb3646462248c0bc492328ab6fa42d4a844cdccddb56e74af1ae843`) retained
the six focused test passes and added worker/KMS stage markers. Its guarded
hardware session proved that the worker processes an external frame and opens
GUD DRM, then fails during atomic KMS resource setup before allocation,
modeset, or USB. Both sessions restored the packaged plugin and left the Pi
active/configured without a service action. This is contained pre-transfer
evidence only, not acceptance of responsiveness, slow output, I/O error,
shutdown, or reconnect behavior.

## Hardware deployment result (2026-07-27)

The mandatory OnePlus host/enumeration gate found `1d50:614d` before the
commit-qualified Noble module was bind-mounted for the test. LightDM started
and Mir reported a synthetic 1280x720 DisplayPort output as connected and
used. No `GUD POC output enabled` log line appeared, and the Pi recorded no new
FunctionFS receive session; the worker/KMS transfer path consequently did not
run. In the same 52-second window, the phone kernel emitted sustained binder
`-12` allocation failures and KGSL `-24` file-descriptor exhaustion. That
temporal correlation is not a causal diagnosis, but it is a phone-health
failure and blocks further test progression.

The test module was immediately unmounted and LightDM restarted on the
packaged plugin. The stock plugin hash was restored, normal
`/home/phablet/gud.ko` was not changed, and the Pi service remained active with
its UDC configured; it was not stopped or restarted. Full raw evidence is at
`backport-4.9/env/local/evidence/xdisp-p0.2-hardware-2026-07-27T1125COT/`.
This attempt provides no slow-output, absent-startup, GUD-I/O-error,
reappearance, shutdown, or responsiveness acceptance evidence.

## Acceptance

`XDISP-P0.2` remains **in progress** until all of these are retained:

1. focused unit/component results cover one-slot coalescing, non-blocking
   submit under a deliberately blocked present callback, presentation error
   containment/recovery, and shutdown dropping pending work while joining the
   active frame;
2. an affected-project build and those tests pass in a supported Mir build
   environment;
3. the documented hardware gate first records OnePlus `FOUND:` for
   `1d50:614d`, then captures a deliberately slow and a disconnect/I/O-error
   GUD scenario while phone input and the internal display remain responsive;
   and
4. retained Mir, phone kernel, and Pi service logs show the failure/containment
   result, no unsafe Pi-service action, and no unrelated module/kernel change.

The unexplained larger-payload Pi boundary remains a non-blocking reliability
investigation and supplies no reason to exceed the verified 12,800-byte cap.
