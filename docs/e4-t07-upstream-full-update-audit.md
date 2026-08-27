# E4-T07 upstream full-update and async-flush audit

Status: PASS-B. Configuration B is implemented and hardware-qualified. The kernel half
of configuration C is implemented, defaults off, and passed bounded-latency,
slow-worker, and detach qualification. The Mir direct-presentation option is
implemented and locally component-tested, but its established AArch64 build and
live Mir gate remain pending.

Date: 2026-08-26

## Pinned inputs

- Linux upstream `master`: `502d45774af09f1c681c754c4b7cdfb5d7f72fd9`
- Upstream files audited: `drivers/gpu/drm/gud/gud_pipe.c`, `gud_drv.c`, and
  `gud_internal.h`
- Local `gud`: branch `pixel-format-benchmark`, starting HEAD
  `639a57e75bcdca666ac9610fa4ff9d057dc4bc2f`
- Local `gud-gadget`: branch `pixel-format-benchmark`, starting HEAD
  `001a5da9e39ecdd3200bbe3a68ed313ff26adb2f`
- Local `mir-android2-platform-gud`: branch `pixel-format-benchmark`, starting
  HEAD `1fbc01d7c8ea5e5da61efb1e11f8e874e23e49c1`

All three worktrees contained pre-existing modified or untracked files. They
were preserved; no reset, clean, push, or unrelated edit was performed.

## Why 12,800 existed

The limit was not a GUD protocol requirement and was not an xHCI maximum. It
was an empirical safe envelope for the old Pi DWC2/FunctionFS receive path:
12,800-byte high-speed transfers repeatedly completed, while larger exact
reads exposed impossible residual/short-read behavior. The host-side adaptive
planner introduced by `2a8f59b` enforced that receiver envelope by fitting
complete compressed rows beneath a project policy cap.

The later FunctionFS large-AIO change removed the contiguous `kmalloc()`
bottleneck for SG-unavailable DWC2. It retains one logical FunctionFS/GUD
payload while executing it internally as sequential 16 KiB DMA-compatible USB
requests. Evidence under
`../gud-gadget/evidence/xdisp-e1-functionfs-large-aio-20260823T200646Z/`
qualifies exact raw logical transfers through 1,843,200 bytes, including a
10/10 repeat gate at that largest size, without timeout, short completion, or
Poisoned transition.

The live Pi snapshot advertises LZ4, RGB565, and `max_buffer_size=1843200`.
That is sufficient for one 1280x720 RGB565 frame and insufficient for one
1280x720 XRGB8888 frame (3,686,400 bytes). Current runtime state therefore
contradicts the task's provisional claim that both formats presently fit the
advertised capacity. XRGB8888 would be split into two negotiated-capacity
rectangles unless the gadget is deliberately reconfigured and re-enumerated
with a capacity of at least 3,686,400 bytes.

## Current-upstream synchronous update behavior

Upstream stores the advertised maximum in `gdrm->bulk_len`, capped at 64 MiB.
`gud_flush_damage()` computes the raw pitch and splits only when the damage
height times pitch exceeds that negotiated capacity. Each rectangle is then
prepared once. `LZ4_compress_default()` receives the raw length as both input
length and output capacity; failure falls back to copying and sending that same
rectangle raw. There is no compression-ratio prediction or post-compression
row fitting.

The Linux 4.9 implementation now follows that control flow. Its intentional
transport difference is a DMA-coherent bounce buffer and explicit URB with
`URB_NO_TRANSFER_DMA_MAP`, because the target OnePlus xHCI rejected the normal
helper's remapping of coherent memory. Explicit pre-bulk `SET_BUFFER -EBUSY`
retry remains; accepted/ambiguous bulk I/O is not resubmitted as a new GUD
transaction.

## Code classification

| File | Function/symbol | Purpose | Category | Decision |
| --- | --- | --- | --- | --- |
| `gud_pipe.c` | `gud_xdisp_plan_chunk*` | Fit compressed rows beneath project cap | `OBSOLETE_12800_POLICY` | Deleted from production and source |
| `gud_pipe.c` | ratio cache, row hints, predictive rows, target-95 policy | Reduce planner retries/cost | `BENCHMARK_ONLY` / `OBSOLETE_12800_POLICY` | Deleted |
| `gud_drv.c` | `xdisp_payload_limit` | Override the project bulk cap | `OBSOLETE_12800_POLICY` | Deleted |
| `gud_pipe.c` | negotiated complete-line split | Respect device bulk capacity | `GENERIC_GUD_BEHAVIOR` | Rewritten to upstream control flow |
| `gud_pipe.c` | exact LZ4 attempt and same-rectangle raw fallback | Upstream compression behavior | `GENERIC_GUD_BEHAVIOR` | Retained |
| `gud_pipe.c` | coherent bounce allocation, explicit URB and `URB_NO_TRANSFER_DMA_MAP` | Avoid invalid OnePlus xHCI DMA remap | `ONEPLUS_4_9_COMPAT` | Retained |
| `gud_pipe.c` | completion/timeout and exact `actual_length` check | Linux 4.9 USB completion boundary | `ONEPLUS_4_9_COMPAT` / `E1_SAFETY` | Retained pending hardware requalification |
| `gud_transfer_retry.h` | bounded explicit `SET_BUFFER -EBUSY` retry | Retry only before bulk ownership is accepted | `E1_SAFETY` | Retained |
| `gud_device.lock` | serialize transfer, disconnect, and PM state | One logical host payload at a time | `E1_SAFETY` | Retained |
| trace/timing/row-CRC and probe parameters | Hardware evidence and failure injection | `BENCHMARK_ONLY` / `E1_SAFETY` | Retained for T07 qualification; not update policy |
| PM-test variant | Suspend/resume failure injection | `BENCHMARK_ONLY` | Retained outside production behavior |

## Upstream async-flush audit

At the pinned upstream commit, `async_flush` is an optional boolean module
parameter and defaults off. It has not been superseded by a different update
architecture.

- Shadow storage: `gdrm->shadow_buf`, allocated lazily with
  `vcalloc(fb->pitches[0], fb->height)`.
- Pending state: one framebuffer reference (`gdrm->fb`) and one bounding
  damage rectangle (`gdrm->damage`).
- Synchronization: `gdrm->damage_lock` protects the shadow buffer, framebuffer
  reference, and pending damage.
- Merge: new pixels are copied into the shadow buffer and damage is expanded
  with min/max bounds. A newer framebuffer replaces the retained reference.
- Queue: one `work_struct` is submitted to `system_long_wq`; there is no
  per-frame work item or userspace-frame backlog.
- Worker: under `damage_lock`, it takes the single framebuffer reference and
  accumulated damage, clears pending state, unlocks, and performs the normal
  synchronous GUD flush from the shadow buffer.
- Commit semantics: atomic update returns after the shadow copy and workqueue
  submission. DRM commit completion no longer waits for the USB transfer.
- Slow-link semantics: pending writes coalesce in the shadow image. For
  full-frame updates, the intended visible result is latest copied pixel state,
  not replay of every submitted framebuffer. Exact 100→103 behavior still
  requires instrumentation because a worker may already have snapshotted 100
  while later state accumulates for a following run.
- Mode change/disable: `gud_plane_atomic_update()` calls
  `cancel_work_sync()`, drops the retained framebuffer, clears damage, and
  frees the shadow buffer before proceeding.
- Disconnect: upstream uses `drm_dev_unplug()` followed by
  `drm_atomic_helper_shutdown()`; worker entry is guarded by `drm_dev_enter()`.
  The 4.9 backport lacks those exact lifetime helpers, so any C implementation
  must explicitly quiesce work before freeing `gud_device`, then reproduce the
  same enter/exit lifetime guarantee with the existing disconnect lock/state.

The 4.9 implementation deliberately keeps the upstream shape: one lazy shadow
buffer, one retained framebuffer, one bounding damage rectangle, one work item
on `system_long_wq`, and a default-off `async_flush` parameter. The unavoidable
4.9 lifetime delta is explicit worker quiescing during disable and disconnect,
because this tree lacks upstream's `drm_dev_enter()`/`drm_dev_exit()` lifetime
guard. Framebuffer-only flips also take upstream's early atomic-check return;
USB state validation and state commit occur only for mode, connector, active,
or format transitions. Initial modeset order is controller enable, state
commit, then display enable, matching the pinned upstream implementation.

`LatestFramePresenter` has not been removed. A `direct` presenter mode now
executes KMS submission on the capture/submit thread without a userspace pending
queue or presentation worker; the existing `latest-frame` mode remains the
default. Direct mode passed a standalone same-thread/balanced-accounting
component check. The repository's established AArch64 Mir container build was
not run because execution approval was denied, so direct mode has not been
deployed into the live Mir path and cannot yet replace the qualified presenter.

## Configuration-B results

- Exact target-kernel module build: pass.
- Module metadata: `e4-t07-full-update-v1`, driver
  `gud_xdisp_full_update`.
- Unresolved-symbol inspection: no unresolved LZ4 symbol; expected target DRM,
  USB, VM, and synchronization symbols only.
- Full 1,843,200-byte RGB565 LZ4 round trip: pass for zero and structured
  content.
- Full 1,843,200-byte incompressible frame: one attempted compression followed
  by raw fallback in the offline contract test.
- ASan/UBSan round-trip gate: pass.
- Full-update source contract: zero failures.
- PM-test contract and exact-kernel build: pass.
- Existing unrelated baseline contract failures remain in
  `test-gud-kms-stage-contract.sh` (it expects RGB565 while its source is
  XRGB8888) and `test-usb-probe-contract.sh` (it expects
  `module_usb_driver()` while this backport uses explicit init/exit for
  reconnect handling).

Live OnePlus 6 / Pi Zero 2 W qualification used the gadget-advertised
1,843,200-byte capacity:

- A 1280x720 RGB565 compressible update was one 1,843,200-byte rectangle, one
  LZ4 attempt, and one 50,696-byte compressed logical payload.
- Incompressible updates remained one rectangle and one attempt; observed
  results included a 1,809,449-byte compressed payload and an exact
  1,843,200-byte raw fallback.
- A 100-frame incompressible synchronous run achieved 11.438 producer FPS with
  76.092 ms average commit latency (53.801--86.853 ms) and no host fault.
- Detach during an in-flight update returned `-ENODEV`, disconnected cleanly,
  emitted no warning/oops, and re-enumerated.
- A controlled 3,000 ms pre-bulk pause bounded the caller at 3.50 s. The
  induced endpoint halt was contained and recovered by resetting diagnostic
  parameters and cycling the USB role.
- A bounded 15-second live Mir RGB565 run with `async_flush=N` received and
  submitted 906 frames, presented 332 at 22 FPS, retained exactly one pending
  and one in-flight userspace frame, and reported zero capture, conversion,
  release, or GUD submission failures. Final accounting balanced after one
  pending frame was cancelled at shutdown. The known Mir 1.8.3 connection
  release stall required the already-qualified timeout containment after KMS,
  presenter, and screencast cleanup had completed; no kernel fault or Poisoned
  transport state occurred.

The live contract is RGB565. The legacy stage helper's XRGB8888 atomic-test
case correctly receives protocol status `0x04`; all preceding KMS resource and
atomic-build stages pass. This is a test-helper format mismatch, not evidence
that a 3,686,400-byte XRGB8888 framebuffer fits the advertised capacity.

## Configuration-C kernel results

- `async_flush` defaults to `N` and can be enabled at runtime. A 100-frame
  incompressible run returned producer commits at 395.587 FPS with 0.271 ms
  average latency (0.200--1.177 ms). One hundred submissions coalesced into
  five worker transfers; the final submitted framebuffer was the final worker
  framebuffer and pending depth never exceeded one.
- With a controlled 3,500 ms pre-bulk pause, 100 producer commits still
  completed at 409.615 FPS with 0.285 ms average latency
  (0.204--1.280 ms). The blocked worker completed after 3.727 s while the
  single pending slot retained newest state. The test-induced endpoint halt was
  contained and recovered by the documented role cycle.
- During a 1,000-frame incompressible run, a physical host-to-device role
  switch occurred while work was active. Producers completed at 439.973 FPS
  with 0.221 ms average latency (0.194--1.423 ms); 42 workers completed before
  clean disconnect, with no UAF, warning, oops, or hung task.
- After the final upstream-order adjustment, a fresh module deployment and
  20-frame RGB565 run completed the initial modeset in 6.488 ms and returned
  updates at 532.447 FPS with 1.100 ms average commit latency. Twenty
  submissions coalesced into six successful workers, with no transfer error or
  kernel fault. Protocol status `0x01` was handled by the retained bounded
  pre-bulk BUSY retry.
- A final 100-frame incompressible measurement with percentile instrumentation
  returned at 380.688 FPS: producer commit p50 0.212 ms, p95 0.842 ms,
  average 0.297 ms, and maximum 1.245 ms. Pending depth remained one and the
  worker drained without a kernel fault.
- Exact target-kernel builds plus LZ4, async source-contract, PM-test, and
  `git diff --check` gates pass.

## Decision

Configuration B is the selected upstream-style synchronous baseline. The
kernel async mechanism is qualified as an optional, default-off configuration
and is structurally equivalent to upstream within the documented Linux 4.9
lifetime and OnePlus DMA deltas. It is not yet the production default.

Do not remove `LatestFramePresenter` or switch xdispd's default until the
repository's established AArch64 build succeeds and direct mode passes the live
Mir controlled-stall, newest-frame, accounting, and detach gates. This keeps
the already-qualified E2 fallback available while the remaining build/deploy
evidence is obtained.

E4-T07 is therefore PASS-B: delete the obsolete planner and select full-payload
synchronous GUD plus `LatestFramePresenter` for production. The qualified
kernel async path remains available for continued configuration-C evaluation;
it does not need to be forced into the production decision for T07 to pass.
