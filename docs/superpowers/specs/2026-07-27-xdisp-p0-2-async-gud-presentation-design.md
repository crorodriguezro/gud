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
