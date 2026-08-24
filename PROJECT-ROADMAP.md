# OnePlus 6 to Raspberry Pi GUD External Display

## Product scope, epics, and delivery roadmap

Status: local planning baseline

Date: 2026-08-17

Coordination repository: `gud`
Component repositories: `gud`, `gud-gadget`, `mir-android2-platform-gud`

This document is the product-level source of truth for scope, priorities,
epics, and delivery order. `PROJECT-STATUS.md` remains the evidence journal
and cross-repository status board. Component backlogs and runbooks retain
their implementation detail.

No GitHub issues have been created from this roadmap yet.

## 1. Final goal

Deliver an installable and recoverable external-display stack in which a
OnePlus 6 Ubuntu Touch phone, retaining its existing Linux 4.9 kernel, uses a
Raspberry Pi Zero 2 W as a USB GUD peripheral and HDMI bridge, and Lomiri
exposes the attached monitor as a stable independent desktop output.

The intended user experience is:

1. Boot the phone and Pi normally.
2. Connect the documented USB topology.
3. The phone discovers the live GUD device without assuming a DRM card number.
4. Lomiri exposes a correctly sized external desktop, not a mirrored capture.
5. Windows and the pointer can move between the phone and external monitor.
6. Phone input and the internal display remain responsive when the Pi is slow,
   disconnected, or reports an error.
7. Disconnect and reconnect recover without rebooting the phone or editing
   device paths.
8. Failures are contained, observable, and recoverable without filesystem or
   kernel damage.

### Product completion criteria

The first usable release is complete only when all of the following pass with
retained evidence:

- **Independent output:** Lomiri exposes and uses a real extended desktop at
  1280x720 or another explicitly selected common mode.
- **Correct presentation:** the output fills the monitor with correct channel
  order, geometry, stride, orientation, and window placement.
- **Responsive UI:** GUD copy, KMS, and USB waits never run on Mir's compositor
  commit path; slow, missing, or failed GUD output does not freeze phone input
  or the internal display.
- **Bounded resources:** worker depth, buffers, fences, file descriptors, and
  memory remain bounded during a 30-minute mixed desktop workload.
- **Reliable transport:** all actual OnePlus-to-Pi bulk payloads remain within
  the qualified safe operating envelope, every accepted receive completes or
  enters explicit containment, and normal operation creates no kernel Oops,
  allocator corruption, DWC2 stop timeout, or pstore record.
- **Reconnect:** ten consecutive detach/reconnect cycles recover the output
  even when the DRM card number changes, without a compositor or phone reboot.
- **Usable performance:** the measured desktop workload meets the release SLO
  selected in Epic E5. Until the baseline is measured, the draft target is at
  least 20 presented frames/s with p95 end-to-end presentation latency no more
  than 150 ms at 1280x720, while preserving phone responsiveness.
- **Operable release:** a fresh operator can install, validate, roll back, and
  collect diagnostics using versioned instructions and hashed artifacts.

The performance figures are product targets, not claims about the current
implementation. E5 may revise them once an apples-to-apples full-pipeline
baseline exists, but it must record the decision explicitly.

## 2. Scope boundaries

### In scope for the first usable release

- OnePlus 6 running the installed Ubuntu Touch / Halium Linux 4.9 kernel.
- A standalone out-of-tree host `gud.ko`; no phone DRM-core patch.
- Raspberry Pi Zero 2 W userspace GUD gadget over FunctionFS and DWC2.
- The verified actual bulk-payload ceiling of 12,800 bytes unless stronger
  hardware evidence safely replaces it.
- One independent Lomiri external output.
- Dynamic GUD DRM discovery, hot removal, and re-add.
- Bounded asynchronous presentation with newest-frame coalescing.
- XRGB8888 and/or RGB565, with the default selected from measurements.
- Correct mode selection, geometry, scaling fallback, and presentation.
- Safe lifecycle, diagnostics, installation, rollback, and recovery.

### Explicitly out of scope for the first usable release

- Implementing the full or reference GUD gadget feature set. For v1, the Pi
  implements only the GUD behavior required by the OnePlus 6 → Pi Zero 2 W →
  HDMI product path.
- USB-C DisplayPort Alt Mode; the transport is USB GUD.
- Screen mirroring as the final product. It may be used only as a diagnostic
  or performance control.
- Replacing or flashing the OnePlus kernel.
- Patching Linux 4.9 DRM core without a separately proven hard blocker.
- Arbitrary Android phones, arbitrary vendor 4.9 kernels, or generic distro
  support.
- 4K, HDR, multiple external connectors, rotation, TV properties, backlight,
  PRIME/dma-buf zero-copy, or suspend feature parity unless required by a P0
  acceptance gate.
- Removing the 12,800-byte limit merely for elegance. Its root cause is a
  research item unless the verified limit prevents the release SLO.
- Upstreaming before the product path is stable and measured.

## 3. Verified baseline as of 2026-08-17

| Capability | State | Evidence-backed conclusion |
| --- | --- | --- |
| Exact OnePlus 6 module build and ABI | verified | The standalone module builds for and loads on `4.9.112-g6b190d86b`. |
| USB probe and DRM/KMS registration | verified | `1d50:614d` probes and creates a GUD DRM card with a connector, CRTC, plane, and mode. |
| Direct KMS first pixels | verified | State check/commit and framebuffer transfer have produced complete physical output. |
| Safe actual payload envelope | verified | Repeated adaptive tests qualify actual bulk payloads at or below 12,800 bytes. |
| Native FunctionFS STATUS_ON_SET transaction | verified for two guarded diagnostic transactions | Two ordered, uncompressed 12,800-byte XRGB8888 transactions each accepted exact AIO before GET_STATUS=OK, completed through DWC2, FunctionFS, userspace, and framebuffer processing, and returned both guards to Idle before the next transaction. |
| Production sequential native-AIO transport | planned | The bounded two-transaction diagnostic intentionally detaches and has not proven a long-lived multi-frame lifecycle. |
| Independent Lomiri external output | feasibility proven, rolled back | The POC exposed `DisplayPort-2` and an extended desktop, but the implementation is not stable or deployable. |
| Nonblocking Mir presentation | in progress | A newest-frame worker exists at source/component-test level; the synthetic render target and hardware resource lifecycle remain unresolved. |
| Dynamic DRM card discovery/re-add | planned | Current experiments can scan by driver, but complete remove/re-add and Mir output recreation are not verified. |
| Pixel-format capability/default | E2 v1 candidate selected; release default undecided | The deployed Mir 1.8.3 Android2 screencast accepts true packed RGB565 and RGB888, rejects XRGB8888, and supplies ABGR8888 under `auto`. Direct Mir RGB565 + LZ4 achieved 22.62 presented fps at 1280x720 against the >=20 target; RGB565 RAW achieved 18.16 fps and remains the fallback. E2-T04 is the next gate; the final release default remains open in E5-T03. |
| Persistent phone SSH | verified on current system image | The lower-root `ssh.service` boot link survives reboot and key-only SSH starts without manual activation. |

The one-transaction STATUS_ON_SET foundation evidence is retained at
`../gud-gadget/evidence/functionfs-status-on-set-hs-20260817T165632Z/`. The
guarded sequential gate is retained at
`../gud-gadget/evidence/functionfs-status-on-set-e1-t02-hs-corrected-20260817T192939Z/`.

### Pixel-format capability audit — VERIFIED

Deployed Mir 1.8.3 Android2 screencast on the OnePlus 6:

- XRGB8888: not advertised; request rejected.
- RGB888: true packed 24-bpp buffer supported.
- RGB565: true packed 16-bpp buffer supported.
- RGB565 can be consumed directly by `mirgud`.

Decision: **Direct Mir RGB565 + LZ4 is the selected E2 v1 transport candidate
for sustained qualification.** At 1280x720 it achieved 22.62 presented fps,
exceeding the >=20 fps target, while preserving E1 transport safety. RGB565
RAW achieved 18.16 fps and remains the simpler fallback. The deployed Mir
1.8.3 screencast path supplies true packed RGB565 directly, so no
XRGB8888-to-RGB565 conversion is required in mirgud. Do not treat this as the
final production default before T04 and T05.

Canonical evidence:
`../gud-gadget/evidence/xdisp-mir-format-capability-20260824T003458Z/`.

This verifies deployed source capability and records the selected E2 v1
candidate. E2-T04 is now unpaused and is the next gate. This does not close
E5-T03 or select the final release default before sustained qualification and
full-pipeline image-quality comparison.

### E2 v1 transport selection — VERIFIED FOR NEXT GATE

The four measured paths are recorded in the canonical evidence bundles under
`../gud-gadget/evidence/`:

- `xdisp-mir-format-capability-20260824T003458Z/`
- `xdisp-e1-fullframe-managed-scaling-20260823T223143Z/`
- `xdisp-e2-bandwidth-damage-20260823T232018Z/`
- `xdisp-e2-direct-mir-rgb565-20260824T010040Z/`

The selected architecture is:

```text
Lomiri -> unmodified Mir 1.8.3 -> direct packed RGB565 screencast
  -> mirgud (no XRGB8888 conversion) -> GUD RGB565 -> LZ4 -> USB
  -> Pi Zero 2 W FunctionFS GUD gadget -> RGB565 framebuffer/VC4 -> HDMI
```

This is a selected E2 v1 transport candidate pending E2-T04 and E2-T05; it is
not yet the final production default. Preserve one logical accepted/inflight
GUD payload, no payload pipelining, explicit retry only for explicit
pre-bulk SET_BUFFER BUSY, and Poisoned containment for ambiguous accepted I/O.

## 4. Priority and lifecycle model

### Priorities

- **P0 — release blocker:** required for a stable external display; work on
  these before feature or performance expansion.
- **P1 — release quality:** required before calling the result usable, but can
  begin after its P0 dependencies have stable interfaces.
- **P2 — post-release improvement:** valuable optimization, portability, or
  deeper diagnosis that is not required by the current product envelope.
- **P3 — research/optional:** upstreaming and feature parity after the product
  is stable.

### Lifecycle states

Use only `planned`, `in progress`, `blocked`, `verified`, and `rolled back`, as
defined in `docs/superpowers/CROSS-REPOSITORY-WORKFLOW.md`.

### Focus and work-in-progress rules

- Keep one primary P0 implementation ticket active per repository.
- Run at most one hardware experiment at a time across the project.
- Every experiment must map to a ticket and predeclared acceptance gate.
- A failure opens or updates a ticket; it does not silently expand the active
  experiment.
- Do not start a P1 hardware gate while a prerequisite P0 gate is unsafe or
  ambiguous.
- Preserve prior evidence. New runs always use new evidence directories.
- A visible frame, compile, or simulation is not hardware verification unless
  the ticket's acceptance criteria explicitly say so.

## 5. Milestones and critical path

| Milestone | Outcome | Required epics | State |
| --- | --- | --- | --- |
| M0 — Technical foundation | Build, probe, direct KMS, first pixels, and safe payload envelope | E0 | verified |
| M1 — Production-safe transport | Sequential protocol-native receives and lifecycle are safe within the qualified envelope | E1 | verified |
| M2 — Responsive external-output alpha | Lomiri external output presents through a bounded worker without freezing or leaking resources | E2 | planned/in progress |
| M3 — Reconnectable and correct beta | Dynamic discovery, reconnect, mode, geometry, and window placement pass repeated gates | E3, E4 | planned |
| M4 — Usable v1 | Performance SLO, soak, installer, rollback, diagnostics, and operator docs pass | E5, E6 | planned |
| M5 — Portability and upstream work | Generalization and upstream-quality improvements begin from a stable baseline | E7 | planned |

Critical path:

`E1 safe transport -> E2 bounded presentation -> E3 reconnect -> E4 correctness -> E5 release SLO -> E6 v1 qualification`

E6 documentation and packaging work may proceed offline in parallel, but its
release gate depends on E1 through E5. E7 must not distract from this path.

## 6. Epics and story-sized tickets

### E0 — Preserve the verified technical foundation

Priority: P0

State: verified, with ongoing evidence maintenance
Owners: all repositories

Outcome: keep the exact build, first-pixels, safe-payload, and diagnostic AIO
results reproducible while the product layer evolves.

User stories:

- As a developer, I can reproduce the exact phone module and Pi binary from
  pinned inputs.
- As an operator, I can distinguish simulation, a diagnostic result, and a
  production-qualified result.
- As an investigator, I can compare a new failure with immutable raw evidence.

| Ticket | P | State | Owner | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E0-T01` Reproducible OnePlus module build | P0 | verified | `gud` | Exact ABI module builds, metadata and unresolved symbols audit cleanly, and the module loads on the phone. |
| `E0-T02` Direct DRM/KMS first-pixels baseline | P0 | verified | `gud` | Independent KMS tool completes state application and physical framebuffer presentation with retained phone/Pi logs. |
| `E0-T03` Qualified 12,800-byte operating envelope | P0 | verified | `gud`, `gud-gadget` | Ten-cycle adaptive matrix stays within the cap and shows matching receive completion/Idle evidence without kernel faults. |
| `E0-T04` One protocol-native STATUS_ON_SET transaction | P0 | verified | `gud-gadget`, `gud` | One XRGB8888 12,800-byte AIO request is accepted before status success and completes exactly through host, DWC2, FunctionFS, and framebuffer processing. |
| `E0-T05` Evidence index and stale-backlog reconciliation | P1 | planned | coordination | Index canonical evidence, mark superseded historical statements, and link this roadmap from the existing status/backlog documents without deleting history. |

### E1 — Make the GUD transport production-safe

Priority: P0

State: in progress
Owners: `gud-gadget`, `gud`

Outcome: a long-lived service handles sequential protocol-valid frames inside
the qualified payload envelope without control-path deadlock, unsafe teardown,
or hidden in-flight work.

User stories:

- As a user, repeated screen updates do not freeze the display or phone.
- As an operator, disconnecting an Idle session recovers normally, while an
  unsafe session is contained instead of torn down.
- As a developer, every accepted SET_BUFFER has one observable request,
  status, bulk completion, and final state.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E1-T01` Gracefully drain the one-shot trailing status | P0 | verified | E0-T04 | After payload completion, answer the host's already-pending control status before diagnostic detach; host logs no post-success `-71`, no second SET_BUFFER is accepted, and both guards remain Idle. Verified by commit `cea9942` and `../gud-gadget/evidence/functionfs-status-on-set-e1-t01-hs-20260817T185502Z/`. |
| `E1-T02` Guarded sequential STATUS_ON_SET diagnostic | P0 | verified | E1-T01 | A separately guarded two-transaction build completes two exact, uncompressed 12,800-byte payloads in order, returning Idle between them, with no warm-up, retry, overlap, or teardown race. Verified by gadget commit `93a6364`, host-probe commit `fcde453`, evidence commit `df87701`, and `../gud-gadget/evidence/functionfs-status-on-set-e1-t02-hs-corrected-20260817T192939Z/`. |
| `E1-T03` Select the production receive architecture | P0 | verified | E1-T02 | Protocol-gated exact native AIO is the production default, with the qualified blocking receiver retained as a separate detached/Idle rollback through E1-T06. The accepted design serializes one transaction through `Idle -> Arming -> InFlight -> Processing -> Idle`, uses depth one, and forbids overlap, speculative prearming, chunked ownership, automatic fallback, and an `auto` mode. Spec: `docs/superpowers/specs/2026-08-17-e1-t03-production-receive-architecture-design.md`. |
| `E1-T04` Implement long-lived sequential multi-frame receive | P0 | verified | E1-T03 | Clean-source production `status-on-set-aio` passed the unchanged 100-frame OnePlus 6/Pi Zero 2 W gate at 12,800 bytes with one exact AIO receive per transaction, ordered sequence IDs 1-100, aggregate `Processing -> Idle` before readmission, final Idle ownership, and zero poison, timeout, host failure, DWC2 anomaly, or kernel fault. Canonical evidence: `../gud-gadget/evidence/functionfs-status-on-set-e1-t04-clean-source-passing-rerun-20260818T030651Z/`. The earlier failed clean rerun is retained as the host RGB565/XRGB8888 stage-format diagnosis. |
| `E1-T05` Disconnect, suspend, timeout, and failure matrix | P0 | verified for v1 | E1-T04 | N1 and F1-F5 passed; F7 is satisfied by F2's accepted-InFlight FunctionFS Suspend containment. F6 is deferred P2: the isolated PM-test variant delivered real Idle Suspend/Resume, then this OnePlus 6 target runtime-PM path re-enumerated the GUD device (`devnum 5 -> 6`), producing Pi Disable -> Enable and a new activation rather than same-activation persistence. No PM behavior is enabled in production. Final evidence: `/tmp/opencode/e1-t05-f6-final-pm-diagnostic-20260819T051300Z`; matrix: `../gud-gadget/docs/e1-t05-lifecycle-matrix-runbook.md`. |
| `E1-T06` Transport soak within the qualified envelope | P0 | verified | E1-T05 | E1-T06 is accepted for v1 by project-owner decision. The retained soak evidence does not independently reconstruct every originally specified reconnect/count criterion, but no further E1-T06 qualification is required for the v1 critical path. Evidence: `../gud-gadget/evidence/functionfs-status-on-set-e1-t06-soak-20260820T012100Z/`. |
| `E1-T07` Explain or safely raise the >12,800-byte boundary | P2 | planned | E1-T06, release SLO need | Kernel/host matrix identifies the cause or qualifies a larger limit. This is not a v1 blocker while 12,800 bytes meets the product SLO. |
| `E1-T08` Post-v1 upstream or replace vendored FunctionFS extensions | P2 | planned | v1, E1-T04 | Production no longer depends on an unexplained local endpoint/AIO extension, or the extension has an upstream-quality design and test suite. |

Epic acceptance: E1-T01 through E1-T06 are verified. The retained T06 evidence
is preserved with its historical caveat, but the project-owner decision accepts
it for v1, and the chosen production
path has a reproducible package and rollback, and no unsafe recovery is needed
in the passing matrix.

### E2 — Make Mir/Lomiri presentation asynchronous and bounded

Priority: P0

State: in progress
Owner: `mir-android2-platform-gud`

Legacy mapping: `XDISP-P0.2`.

Outcome: the external output never performs GUD copy, KMS, or USB waits on
Mir's compositor commit path, and resource ownership stays bounded.

User stories:

- As a phone user, touch, apps, and the internal panel remain responsive even
  when the external device stalls or disappears.
- As a compositor maintainer, buffers and fences have explicit bounded
  ownership and shutdown order.
- As an operator, a GUD error is logged and contained without taking down
  LightDM or the phone UI.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E2-T01` Reconcile the source/render capture gate | P0 | verified | clean packaged-phone baseline | Prove the selected xdispd-driven Lomiri source/render/capture gate can repeatedly create and release a bounded capture source with real content, while the older Android2 synthetic/offscreen output remains dormant unless separately required. Focused source-only cycles and resource snapshots must show bounded FDs/fences. Canonical clean evidence: `fresh-extension-20260820T035520Z-fast`. Earlier Lomiri greeter restart evidence was investigated and classified as unrelated device/session contamination; the clean rerun passed without restart. |
| `E2-T02` Complete newest-frame worker integration | P0 | verified | E2-T01, stable E1 interface | **Verified for v1 by project-owner decision.** The real wrapper-free managed Lomiri -> Mir Virtual -> mirgud -> GUD path presented frames with `max_pending_observed=1` and `max_in_flight_observed=1`; focused tests passed for newest-frame replacement, nonblocking source submission, ownership retention, and worker join semantics. The current source includes the presenter/KMS lifetime guard that closes the early Mir-era ownership hole. Graceful shutdown while GUD transport is stalled or failing is deferred to E2-T03/E2-T05. |
| `E2-T03` Prove compositor nonblocking behavior | P0 | verified | E2-T02 | **Verified for v1 by project-owner decision.** Managed presentation naturally observed a 3.143 s worker-side GUD stall while producer enqueue remained p95 10 us / max 177 us, with `max_pending_observed=1` and `max_in_flight_observed=1`; healthy presentation, bounded absence, and real USB-detach containment preserved phone/compositor responsiveness. The synthetic slow injector is waived for v1. Failed/stalled teardown remains E2-T05; long-duration FD/fence stability remains E2-T04. Canonical evidence: `../gud-gadget/evidence/xdisp-e2-t03-20260820T190445Z/`. |
| `E2-T04` Eliminate unbounded fence/FD retention | P0 | planned (unpaused; next gate) | E2-T01 | A 30-minute direct Mir RGB565 + LZ4 render/present soak reaches a stable plateau with accounting for every buffer, fence, and descriptor; no binder `-12` or KGSL `-24` occurs. |
| `E2-T05` Contain worker and KMS errors | P0 | planned | E2-T02 | Inject open, allocation, modeset, submit, disconnect, and shutdown failures; no exception crosses the compositor boundary and recovery remains possible. |
| `E2-T06` Hardware alpha qualification | P0 | planned | E2-T03..T05 | Real Lomiri content presents for 30 minutes with bounded resources, newest-frame coalescing, no phone freeze, and retained phone/Pi evidence. |

#### E2-T02 closure decision

E2-T02 is accepted for v1 as **verified by project-owner decision**. The
production managed path demonstrated real Lomiri presentation through
`LatestFramePresenter` and GUD with one pending frame and one in-flight frame
maximum. Focused tests verify newest-frame replacement, source-submit
nonblocking behavior, ownership retention, and worker join semantics. The
early Mir-era presenter/KMS ownership hole is fixed by Mir commit `e7250c8`.

Supporting bounded-presentation evidence is retained at
`../gud-gadget/evidence/xdisp-e2-t02-final-20260820T103740Z/`. The current
artifact, timeout-classification, recovery, and recovered-presentation evidence
is retained at
`../gud-gadget/evidence/xdisp-e2-t02-current-modeset-timeout-20260820T173402Z/`.
The latter also records that, after the GUD transport degraded before
`Deactivate`, xdispd used bounded forced containment. This decision does not
assert that graceful shutdown under stalled/failing transport passed, that
`KMS_TEARDOWN_COMPLETE` was observed in that degraded shutdown, or that final
hardware accounting after containment was proven. Those failure-mode questions
are deferred to E2-T03/E2-T05.

#### E2-T03 closure decision

E2-T03 is accepted for v1 as **verified by project-owner decision**. Production
managed presentation naturally reached a 3.143 s worker-side GUD presentation
stall while producer enqueue remained microsecond-scale (p95 10 us, max 177
us), with one pending and one in-flight frame maximum. The phone remained
responsive and compositor/LightDM continuity was preserved. The absent-device
path remained bounded without a managed child, and a real USB data detach was
contained without destabilizing the phone. A separate synthetic slow-GUD
injector is waived for v1 because the natural stall already demonstrates
producer/worker decoupling.

This closure does not claim graceful teardown under failed or stalled transport,
which remains E2-T05, or long-duration FD/fence stability, which remains
E2-T04. Canonical closure evidence is retained at
`../gud-gadget/evidence/xdisp-e2-t03-20260820T190445Z/`; its original partial
matrix classification remains an accurate record of the pre-acceptance state.

Epic acceptance: slow, absent, and failed GUD output cannot block the phone UI;
the full hardware alpha passes without resource growth or manual rollback.

### E3 — Discover and recover the live GUD output dynamically

Priority: P0

State: planned
Owners: `mir-android2-platform-gud`, `gud`

Legacy mapping: `XDISP-P0.3`.

Outcome: no component assumes `/dev/dri/card1`; remove/re-add recreates the
worker and Lomiri output safely.

User stories:

- As a user, reconnect works even when Linux assigns a different card number.
- As an operator, no symlink or compositor restart is required after normal
  detach/reconnect.
- As a developer, stale file descriptors can never target a removed GUD card.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E3-T01` Discover DRM devices by driver identity | P0 | planned | E2-T02 | Bounded enumeration selects an accessible `gud` DRM device and validates connector/mode capabilities without a hard-coded node. |
| `E3-T02` Subscribe to DRM/udev add and remove | P0 | planned | E3-T01 | Device removal invalidates the exact live instance; re-add triggers discovery without polling forever or reusing stale fds. |
| `E3-T03` Recreate worker and KMS resources | P0 | planned | E3-T02 | Removal cancels pending work in order, and re-add creates fresh fd, buffers, mode blob, and atomic state. |
| `E3-T04` Propagate output hotplug to Lomiri | P0 | planned | E3-T03 | The external output becomes disconnected/connected accurately without disturbing the internal output. |
| `E3-T05` Dynamic-card reconnect matrix | P0 | planned | E3-T04 | Ten reconnects, including forced card-number changes, recover automatically with no stale descriptor, compositor restart, or phone reboot. |

### E4 — Make geometry, modes, and desktop placement correct

Priority: P1

State: planned
Owners: `mir-android2-platform-gud`, `gud-gadget`, `gud`

Legacy mapping: `XDISP-P1.1` plus the mode-matching portion of
`XDISP-P2.1`.

Outcome: the external desktop consistently fills the selected monitor mode
with correct pixels and predictable Lomiri placement.

User stories:

- As a user, the external desktop is full-width, correctly oriented, and not
  cropped or squeezed.
- As a user, enabling or reconnecting the monitor restores the same layout.
- As a developer, advertised mode, Mir buffer, GUD state, and Pi scanout are
  traceably consistent.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E4-T01` Define the end-to-end mode contract | P1 | planned | E2-T06 | Record selected timing, logical size, source size/stride/format, GUD state, and Pi physical mode under one correlation ID. |
| `E4-T02` Fix full-width content and channel correctness | P1 | planned | E4-T01 | Deterministic grids, ramps, and color patterns prove geometry, crop, stride, row order, and channel order on the physical monitor. |
| `E4-T03` Implement dynamic Pi physical mode matching | P1 | in progress | E1 stable | Exact timing activates native scanout once; scaling remains a safe fallback when exact activation is unavailable. |
| `E4-T04` Persist Lomiri placement policy | P1 | planned | E3-T04 | External position, primary/internal roles, and pointer transitions remain correct across enable/disable and reconnect. |
| `E4-T05` Repeated geometry and placement matrix | P1 | planned | E4-T02..T04 | Ten enable/disable and ten reconnect cycles retain full-screen correct output and window placement. |

### E5 — Meet a measured performance and quality SLO

Priority: P1

State: in progress at instrumentation level
Owners: all repositories

Legacy mapping: `XDISP-P2.1` and the pixel-format benchmark.

Outcome: choose the simplest transport, format, mode, and update policy that
meets a documented full-pipeline responsiveness and visual-quality target.

User stories:

- As a user, desktop motion feels responsive and text/gradients have acceptable
  quality.
- As a maintainer, format and compression defaults come from comparable data,
  not a single visual trial.
- As an investigator, latency and CPU time are attributed to capture,
  conversion, planning, USB, Pi processing, and presentation.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E5-T01` Freeze benchmark semantics and accounting | P1 | in progress | E2 stable interfaces | Final reports account for received, submitted, presented, dropped, cancelled, failed, and in-flight frames with non-overlapping timing fields. |
| `E5-T02` Establish full-pipeline baseline | P1 | planned | E2-T06, E4-T03 | Measure FPS, p50/p95 latency, drops, phone/Pi CPU, USB throughput, memory, FDs, and fences for controlled desktop, scroll, video, and noise workloads. |
| `E5-T03` Decide RGB565 versus XRGB8888 | P1 | planned | E5-T02 | Apples-to-apples hardware data records conversion path, payload count, latency, CPU, throughput, and objective image-quality metrics; decision and rollback are documented. |
| `E5-T04` Evaluate damage-aware updates | P1 | planned | E5-T02, release-SLO need | Add changed-region updates only if the measured simple path misses the SLO; otherwise record that they are unnecessary. Any adopted path must have no stale pixels, rectangle gaps, or payload-cap violations. |
| `E5-T05` Evaluate the compression planner | P1 | planned | E5-T02, release-SLO need | Compare bounded, ratio-cache, backoff, and raw policies only if measurements show compression is needed; otherwise retain the simplest policy. Any adopted path retains the 12,800-byte final submission guard. |
| `E5-T06` Set and verify the release SLO | P1 | planned | E5-T03, any required E5-T04/T05 | Record final FPS/latency/drop/responsiveness targets and pass them in a repeated 30-minute mixed workload. |

Optimization stops when the release SLO is met. Raising the transport cap,
zero-copy, speculative planners, and other transport complexity remain P2
unless measurements show they are necessary. The selected simplest
implementation becomes the v1 default; further optimization requires a new
measured release-SLO need.

### E6 — Productize installation, operation, and recovery

Priority: P1

State: planned, with some verified building blocks
Owners: all repositories

Outcome: the stack can be installed and operated repeatedly without relying
on session memory, unsafe commands, or untracked artifacts.

User stories:

- As an operator, I can install or roll back each component independently.
- As a user, the phone retains network recovery access after reboot.
- As a support engineer, one command bundle captures enough state to classify
  discovery, compositor, host-driver, transport, or Pi failures.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E6-T01` Versioned artifact manifest and compatibility matrix | P1 | planned | chosen E1/E2 candidates | Record source commits, hashes, ABI, config, cross-component compatibility, and rollback artifact for every release candidate. |
| `E6-T02` Guarded installers and rollback | P1 | planned | E6-T01 | Phone module, Mir plugin, and Pi service install atomically, preserve prior versions, refuse mismatched inputs, and roll back without data loss. |
| `E6-T03` Service and boot orchestration | P1 | planned | E1, E2, E3 | Normal boot ordering, SSH persistence, USB role, Pi service policy, and compositor/plugin startup recover without manual races. |
| `E6-T04` Unified health and evidence collector | P1 | planned | stable log schema | Collect hashes, versions, states, counters, kernel excerpts, pstore, and lifecycle markers into a new immutable evidence directory. |
| `E6-T05` Operator runbook | P1 | planned | E6-T02..T04 | A fresh operator completes install, connection, validation, common recovery, and rollback from the document alone. |
| `E6-T06` Release qualification matrix | P1 | planned | E1..E5 | Cold boot, connect, 30-minute use, disconnect, reconnect, error injection, and rollback all pass with retained evidence. |

### E7 — Portability, upstreaming, and optional features

Priority: P2/P3

State: planned
Owners: component-specific

Outcome: own generic GUD feature parity, portability, upstreaming, and other
optional complexity after v1, converting proven local behavior into
maintainable generic work without delaying the OnePlus/Pi product.

User stories:

- As a maintainer, local compatibility code has clear deletion or upstream
  paths.
- As another device owner, the design can be adapted without inheriting
  OnePlus- or Pi-specific constants as fake protocol rules.

| Ticket | P | State | Depends on | Deliverable and acceptance |
| --- | --- | --- | --- | --- |
| `E7-T01` Generalize the Linux 4.9 host compatibility layer | P2 | planned | v1 | Separate target ABI data from reusable 4.9 DRM/USB compatibility code and validate a second kernel only when available. |
| `E7-T02` Propose generic bounded GUD compression planning | P2 | planned | v1, E5 decision | Upstream-facing design uses a capability/quirk model rather than hard-coding the Pi's 12,800-byte observation. |
| `E7-T03` Upstream FunctionFS/AIO improvements | P2 | planned | v1, E1 decision | Produce minimal kernel/userspace reproducer, documented semantics, and upstream-quality tests for any required AIO change. |
| `E7-T04` Generic GUD feature parity and optional complexity | P3 | planned | v1 | Evaluate the full/reference GUD gadget feature set, rotation, backlight, connector properties, multiple connectors, PRIME/dma-buf, deeper suspend/resume, larger transfers, zero-copy, and receive concurrency individually. |

## 7. Prioritized execution queue

This is the order to use when choosing the next ticket. A lower item may do
offline preparation in parallel, but must not bypass its dependency gate.

1. `E1-T01` — gracefully drain the trailing one-shot status.
2. `E1-T02` — prove two sequential native-AIO transactions.
3. `E1-T03` — select the production receive architecture.
4. `E2-T01` — finish the bounded synthetic offscreen render target.
5. `E1-T04` — implement the selected long-lived receive path.
6. `E2-T02` — integrate the bounded newest-frame worker with that path.
7. `E1-T05` and `E2-T03` — lifecycle matrix and compositor responsiveness.
8. `E2-T04` and `E2-T05` — resource plateau and error containment.
9. `E2-T06` — hardware alpha qualification.
10. `E3-T01` through `E3-T05` — dynamic discovery and reconnect.
11. `E4-T01` through `E4-T05` — mode, geometry, and placement correctness.
12. `E5-T01` through `E5-T06` — measurement, format/default decision, and SLO.
13. `E6-T01` through `E6-T06` — product packaging and v1 qualification.
14. `E1-T07`, `E1-T08`, and E7 — research/upstream work after v1 needs are
    known.

### Immediate focus

The current focus is **E1 production-safe transport**, starting E1-T04 from the
verified E1-T03 architecture decision.
The only parallel implementation work that should proceed is offline E2-T01
in `mir-android2-platform-gud`; it must not trigger a phone/Pi transfer until
the E1 hardware gate is safe and scheduled.

## 8. GitHub-ready issue model

When this local roadmap is accepted:

- Create one GitHub issue per epic and one issue per ticket.
- Use the ticket ID at the start of every title, for example
  `[E1-T01] Gracefully drain trailing STATUS_ON_SET status`.
- Link tickets to their epic and list explicit dependencies in the issue body.
- Keep cross-repository IDs identical in all repositories.
- Close historical foundation tickets as `verified`; do not reopen them to
  hold new work.
- Put raw hardware evidence in the owning local evidence tree and link only
  the safe summary/hash from GitHub.

Recommended labels:

- `type:epic`, `type:story`, `type:investigation`;
- `priority:P0`, `priority:P1`, `priority:P2`, `priority:P3`;
- `component:host-driver`, `component:pi-gadget`, `component:mir-lomiri`,
  `component:operations`;
- `state:planned`, `state:in-progress`, `state:blocked`, `state:verified`,
  `state:rolled-back`;
- `needs:hardware`, `needs:design`, `needs:evidence`, `safety-critical`.

Every ticket body should contain:

1. outcome/user story;
2. context and evidence links;
3. in-scope and out-of-scope boundaries;
4. dependencies;
5. implementation tasks;
6. tests and exact hardware acceptance evidence;
7. stop/containment conditions;
8. rollback plan;
9. definition of done.

## 9. Decision rules that protect the horizon

- **v1 architecture rule:** GUD remains the wire/protocol compatibility
  boundary, and FunctionFS remains the Pi implementation boundary. The Pi is
  a purpose-built, minimal GUD appliance for the OnePlus 6 → Pi Zero 2 W →
  HDMI path, not a generic GUD framework: it accepts one `SET_BUFFER` and
  owns one transaction at a time, using explicit `Idle`, `Arming`, `InFlight`,
  `Processing`, and `Poisoned` states. Generic GUD parity, transport
  flexibility, extra connectors, larger transfers, zero-copy, and additional
  concurrency are post-v1 unless measurements show they are necessary to meet
  the release SLO.
- The product is a usable independent external desktop, not a transport
  research program.
- The verified 12,800-byte envelope is a valid product constraint until data
  proves it prevents the SLO.
- A deeper kernel investigation competes for priority only when a P0/P1 ticket
  demonstrates that the current envelope cannot deliver the product.
- Mir responsiveness and bounded ownership are release blockers even when the
  USB transport is perfect.
- Reconnect and geometry are product behavior, not polish.
- Performance work begins with measurement and stops when the agreed SLO is
  met.
- Optional features and upstreaming cannot displace the P0 critical path.
