# OnePlus 6 to Raspberry Pi GUD External Display

## Product roadmap

Status: core display path is working; MVP focus has moved to user experience, automatic activation, installation, and recovery.

Updated: 2026-08-28

Coordination repository: `gud`
Component repositories: `gud`, `gud-gadget`, `mir-android2-platform-gud`
Canonical integration branch in all repositories: `development`

`PROJECT-STATUS.md` is the detailed evidence journal. This roadmap is intentionally concise and records current product priorities rather than repeating historical experiment detail. Historical acceptance data remains available in `PROJECT-STATUS.md`, component evidence directories, and git history.

## 1. MVP product goal

Ship an installable external-display stack where a OnePlus 6 running Ubuntu Touch/Lomiri uses a Raspberry Pi Zero 2 W as a USB GUD-to-HDMI bridge.

The MVP user journey is:

1. Install the phone bundle.
2. Install the Pi bundle or provision the supported Pi image.
3. Reboot when the installer requires it.
4. Connect HDMI and the documented USB topology.
5. The Pi exposes GUD automatically.
6. The phone discovers the GUD device automatically.
7. The Mir/Lomiri external-display path starts automatically.
8. The external desktop appears without running enumeration commands, selecting `/dev/dri/cardX`, or manually starting development tools.
9. Disconnect/reconnect is recovered automatically when the platform allows it, with a clear bounded failure state otherwise.
10. Update, diagnostics, uninstall, and rollback are documented and repeatable.

A developer checkout and manual benchmark workflow are not acceptable as the normal MVP installation or activation path.

## 2. Current architecture

```text
Lomiri / Mir
    ↓ public Mir/MirClient APIs
xdispd / mirgud
    ↓ LatestFramePresenter
GUD DRM host driver on OnePlus 6
    ↓ USB 2.0 High Speed
Pi Zero 2 W userspace FunctionFS GUD gadget
    ↓ VC4 DRM
HDMI monitor
```

Production invariants:

- direct packed RGB565 from Mir;
- LZ4 with same-frame RAW fallback;
- `LatestFramePresenter` with max pending = 1 and max in-flight = 1;
- one logical accepted/in-flight GUD transaction on the gadget;
- no hard-coded `/dev/dri/card1` in production discovery;
- no Mir core patch;
- no GUD-specific Android2 backend patch while public Mir APIs remain sufficient;
- slow or failed external presentation must never freeze the phone UI.

## 3. Verified baseline

The following are no longer active research questions for the MVP:

| Area | State | Current conclusion |
| --- | --- | --- |
| E1 transport safety | verified | Long-lived single-transaction FunctionFS transport and containment model are accepted for v1. |
| E2 bounded presenter | verified | `LatestFramePresenter` protects the Mir producer; pending/in-flight depth remains 1/1. |
| Mir integration | verified | Standalone `xdispd` + `mirgud` through public Mir APIs is sufficient. |
| Full-frame GUD semantics | verified | Full logical payload transport replaced the obsolete project-specific 12,800-byte logical planner. |
| Pixel correctness | verified | 1280x720 RGB565 geometry, stride, row order, channel order, and physical output are correct. |
| Physical modes | verified | Exact physical HDMI timings route DirectExact at 1280x720 and 1920x1080; synthetic modes use ScaledFallback. |
| 720p performance | characterized | Stable live desktop observations are approximately 21–28 FPS; incompressible content is USB-bound around 17 FPS. |
| 1080p performance | characterized | Stable live desktop is approximately 19–20 FPS with ~645 KB LZ4 payloads; host LZ4 plus GUD/USB presentation dominates. Raw/incompressible 1080p is USB-bound around 8 FPS. |
| Synthetic scaling | characterized | Current Pi CPU scaler is the dominant cost around 152 ms/frame and is post-MVP unless synthetic modes become required. |

The last production-path performance run remains formally PARTIAL because the live desktop scene was not a deterministic injected workload. That does not block the present UX/productization phase. Re-open deep performance work only for a measured product need or regression.

## 4. MVP scope

### In scope

- OnePlus 6 on the current Ubuntu Touch/Halium Linux 4.9 target.
- Raspberry Pi Zero 2 W GUD-to-HDMI appliance.
- One external display.
- Automatic GUD discovery and activation.
- Automatic start/stop of the userspace display bridge.
- Correct 720p and 1080p DirectExact physical modes.
- Bounded presentation and transport failure containment.
- Normal connect/disconnect/reconnect lifecycle.
- Phone installer/update/rollback.
- Pi installer/image/update/rollback.
- Version/compatibility manifest.
- Simple health/diagnostic command.
- Short operator documentation.
- Fresh-install MVP qualification from supported base images.

### Explicitly post-MVP

- Lomiri UI for choosing resolution.
- User-controlled monitor placement.
- Persisting custom Lomiri placement/layout policy.
- Full multi-monitor geometry/placement matrix beyond the automatic MVP policy.
- Damage-aware updates unless needed to fix an MVP performance regression.
- Alternative compression codecs.
- VC4/HVS scaling optimization.
- USB3 transport work.
- Generic Android/macOS/Windows host support.
- Multiple external connectors, rotation, HDR, 4K, PRIME/dma-buf zero-copy, and generic GUD parity.
- Upstreaming work that does not directly unblock the MVP.

For MVP, accept Lomiri's automatic mode/layout behavior as long as the external output is usable and correct. Resolution/placement configuration is deliberately deferred.

## 5. Milestones

| Milestone | Outcome | State |
| --- | --- | --- |
| M0 — Technical foundation | Module build, probe, first pixels, safe transport | verified |
| M1 — Responsive production path | Bounded Mir presenter + long-lived GUD path | verified |
| M2 — Correct physical display | RGB565 correctness + 720p/1080p DirectExact modes | verified |
| M3 — Performance characterization | 720p/1080p bottlenecks understood well enough to proceed | verified for MVP planning |
| M4 — Plug-and-display beta | Connect cable and external output appears without manual enumeration/start commands | planned — current focus |
| M5 — Installable MVP | Phone + Pi installation/update/rollback and clean-device qualification | planned |
| M6 — Post-MVP polish | User layout controls, scaling/performance expansion, portability/upstreaming | planned |

Current critical path:

```text
M4 automatic activation
    ↓
M5 phone/Pi packaging + fresh-install qualification
    ↓
MVP release
```

## 6. Active epics and tickets

### E3 — Automatic discovery, activation, and lifecycle

Priority: P0
State: partially hardware-verified; reconnect observer proof and full matrix remain
Owners: `mir-android2-platform-gud`, `gud`, coordination

Outcome: the user connects the supported Pi and the external output appears without manual DRM/Mir enumeration or developer commands.

| Ticket | P | State | Deliverable and acceptance |
| --- | --- | --- | --- |
| `E3-T01` Discover GUD DRM by identity | P0 | verified | Production selects the live GUD DRM device dynamically rather than assuming a card number. |
| `E3-T02` Event-driven device presence | P0 | verified* | Boot-time presence and later add/remove events drive bounded discovery; no manual enumeration command is required. |
| `E3-T03` Automatic bridge lifecycle | P0 | verified* | The supported session automatically starts/stops the required `xdispd`/`mirgud` path when GUD becomes usable or disappears. No operator shell command is required in the normal path. |
| `E3-T04` Automatic Lomiri output activation | P0 | partial* | First activation reports the virtual output connected and presents a frame; the reconnect topology snapshot needs fresh qtmir/Lomiri observer evidence. |
| `E3-T05` Connect/disconnect/reconnect UX matrix | P0 | partial* | Startup, add, remove, and one same-boot reconnect passed; ten cycles and a forced card-number change remain outstanding. |

`*` Hardware qualification is recorded in
`gud-gadget/evidence/xdisp-auto-lifecycle-20260829T1418Z/`; see
`gud/docs/xdisp-automatic-lifecycle.md` for the exact boundaries.

Do not hide the manual workflow behind a single undocumented script and call it complete. The product service must own ordering, retries, instance identity, stale-resource cleanup, and bounded failure behavior.

### E4 — Display correctness and mode policy

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

State: verified — COMPLETE FOR V1
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
| `E2-T04` Eliminate unbounded fence/FD retention | P0 | verified | E2-T01 | A full 30-minute managed Direct Mir RGB565 + LZ4 soak passed at 24.474 FPS with no observed RSS, FD, thread, or sync-file leak, zero poisoned/ambiguous I/O, and owner-confirmed correct physical HDMI output. Canonical evidence: `../gud-gadget/evidence/xdisp-e2-t04-rgb565-lz4-soak-20260824T033632Z/`. |
| `E2-T05` Contain worker and KMS errors | P0 | verified | E2-T02 | A 10/10 bounded healthy-shutdown disposition plus B 3/3, C 2/2 real physical detach, D 3/3, E 3/3, and F verified. Canonical evidence: `../gud-gadget/evidence/xdisp-e2-t05-shutdown-containment-20260824T133342Z/` and `../gud-gadget/evidence/xdisp-e2-t05-bce-completion-20260824T135743Z/`. |
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

#### Historical E2-T04 blocked-run record

The original E2-T04 attempt was **blocked**, not passed. The direct Mir RGB565 + LZ4 session was
activated with the declared one-admission/one-in-flight safety model and
reached 13,920 accepted and completed transactions with zero poisoned,
timed-out, processing-failed, or ambiguous-accepted I/O in the captured
telemetry. The operator then reported that the desktop occupied only about one
third of the monitor, so the 30-minute soak was stopped before its acceptance
interval.

The connected Pi HDMI connector advertises 1920x1080 as its preferred mode,
while the T04 configuration forced the test-only physical mode to 1280x720.
A short native-mode check selected 1920x1080 and invoked scaling from the
1280x720 source, but observed about 203 ms of scaling work even for one-row
updates; it was therefore not accepted as a qualification fix. The temporary
diagnostic configuration was removed and the known-safe production state was
restored. A fresh T04 run requires a documented full-screen geometry/source
configuration that does not introduce this scaling regression.

That historical blocked-run evidence is retained at
`../gud-gadget/evidence/xdisp-e2-t04-rgb565-lz4-soak-20260824T013813Z/`.

The subsequent owner disposition and completed soak supersede the blocked
attempt for the roadmap acceptance. The canonical passing soak is
`../gud-gadget/evidence/xdisp-e2-t04-rgb565-lz4-soak-20260824T033632Z/`.

Epic acceptance: slow, absent, failed, and physically disappearing GUD output
cannot block the phone UI; the full hardware alpha passes without resource
growth or manual rollback.

#### E2 v1 closure

**E2 COMPLETE FOR V1.** Direct Mir RGB565 + LZ4 is the selected transport
candidate at 1280x720. It exceeded the >=20 FPS qualification target at
22.62 FPS, and the completed T04 soak measured 24.474 FPS over 30 minutes.
T01, T02, and T03 are passed; T04 is verified by project-owner disposition;
T05 is PASS. The bounded asynchronous presenter retains at most one pending
and one in-flight frame, and E1's one-logical-payload ownership rules remain
unchanged.

The known v1 caveat is explicit: deployed Mir 1.8.3 may stall in
`mir_connection_release` after presenter/KMS cleanup. xdispd applies its
existing bounded containment, force-terminates and reaps the managed child,
and leaves the phone/compositor healthy. This is bounded containment at the
known Mir disconnect boundary, not graceful shutdown. Reconnect/reappearance
is outside E2 and belongs to **E3 — Discover and recover the live GUD output
dynamically**, which remains next and has not started.

Detailed closure, evidence references, and the unpushed-change audit are in
`docs/e2-v1-closure.md`.

### E3 — Discover and recover the live GUD output dynamically

Priority: P0

State: partially hardware-verified; see the dated qualification note below
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
| `E3-T01` Discover DRM devices by driver identity | P0 | verified | E2-T02 | Bounded enumeration selects an accessible `gud` DRM device and validates connector/mode capabilities without a hard-coded node. |
| `E3-T02` Subscribe to DRM/udev add and remove | P0 | verified | E3-T01 | Device removal invalidates the exact live instance; re-add triggers discovery without polling forever or reusing stale fds. |
| `E3-T03` Recreate worker and KMS resources | P0 | verified | E3-T02 | Removal cancels pending work in order, and re-add creates fresh fd, buffers, mode blob, and atomic state. |
| `E3-T04` Propagate output hotplug to Lomiri | P0 | partial | E3-T03 | First activation propagated a connected virtual output; reconnect reached active presentation but needs fresh qtmir/Lomiri observer capture. |
| `E3-T05` Dynamic-card reconnect matrix | P0 | partial | E3-T04 | One same-boot reconnect passed with a dynamic USB path; ten reconnects and forced card-number changes remain. |

#### 2026-08-29 automatic lifecycle implementation

The standalone `xdispd`/`mirgud` path now has one lifecycle owner, a bounded
startup DRM scan followed by libudev add/remove monitoring, durable GUD card
identity checks, automatic activation after first install, idempotent
remove/add handling, and a lightweight `xdisp-status` command. The service
remains idle without a GUD card and does not poll or create a Mir source.
Explicit `Disable` remains persistent through a dedicated state marker.

The 2026-08-29 qualification deployed a container-built ARM64 artifact by
staging it on the immutable phone image. Pi-present startup, automatic add,
automatic remove, and one same-boot reconnect passed without manually starting
`xdispd`/`mirgud`, selecting a DRM node, or restarting Lomiri. The service
remained resident and idle while GUD was absent. The reconnect reached an
active presenter, but its second Mir topology snapshot did not report the
virtual output as connected, so qtmir/Lomiri propagation is conservatively
partial until a fresh observer capture is collected. The ten-cycle and forced
card-number matrix is also still open. No Mir core, Android2 display platform,
or HWC2 GUD changes were made.

### E4 — Make geometry, modes, and desktop placement correct

Priority: P1

State: planned
Priority: P1 for verified basics; P2 for user configuration
State: MVP basics verified
Owners: `mir-android2-platform-gud`, `gud-gadget`, `gud`

| Ticket | P | State | Deliverable and acceptance |
| --- | --- | --- | --- |
| `E4-T01` End-to-end mode contract | P1 | verified | Selected timing, source geometry/format, GUD state, and Pi physical mode are traceable. |
| `E4-T02` Pixel/geometry correctness | P1 | verified | Deterministic RGB565 reference pattern proves full geometry and channel correctness. |
| `E4-T03` Physical-mode routing | P1 | verified | Exact physical timings map to DirectExact; synthetic timings use ScaledFallback. |
| `E4-T06` Public-Mir integration guardrail | P1 | verified | Production stays outside Mir core/Android2 modifications while public APIs suffice. |
| `E4-T07` Full-update GUD semantics | P1 | verified | Full logical payload path preserves E1 safety and presenter backpressure. |
| `E4-T04` Lomiri resolution/placement policy and persistence | P2 | planned — post-MVP | Investigate and implement user-visible configuration/persistence only after MVP. |
| `E4-T05` Full geometry/placement matrix | P2 | planned — post-MVP | Validate configurable placement/orientation/reconnect policy after E4-T04. |

### E5 — Performance and quality

Priority: P1 baseline, P2 optimization
State: baseline characterized; optimization paused for UX work
Owners: all repositories

| Ticket | P | State | Deliverable and acceptance |
| --- | --- | --- | --- |
| `E5-T01` Performance accounting | P1 | verified | Distinguish source, presenter, USB, Pi receive/process, and page-flip submission rates. |
| `E5-T02` 720p/1080p baseline | P1 | verified for MVP planning | Raw USB ceiling, LZ4 cost, bulk timing, and stable live-desktop production rates are measured. |
| `E5-T03` RGB565 production default | P1 | verified | Direct Mir RGB565 + LZ4 remains the MVP default. |
| `E5-T04` Damage-aware updates | P2 | planned — post-MVP | Evaluate only if UX testing shows current desktop performance is insufficient. |
| `E5-T05` Compression/transport optimization | P2 | planned — post-MVP | Optimize LZ4/update policy only from measured need. |
| `E5-T06` Deterministic workload/release performance expansion | P2 | planned — post-MVP | Add controlled workload qualification if needed for a later performance SLO. |

### E6 — MVP installation, operation, and recovery

Priority: P0/P1
State: planned — current productization phase
Owners: all repositories

Outcome: shipping the project means shipping a supported bundle, not a development checkout plus remembered commands.

Required shipped pieces include, as applicable:

- OnePlus `gud.ko` built for the supported kernel ABI;
- `mirgud` / `xdispd` runtime pieces;
- automatic activation/service integration;
- Pi gadget binary and service configuration;
- FunctionFS/configuration/bootstrap pieces needed by the supported Pi image;
- waiting/error presentation assets needed for normal appliance behavior;
- version/compatibility manifest and hashes;
- installer, updater, uninstall/rollback path;
- health/diagnostic command and concise runbook.

Do not prematurely mandate `.deb`, Click, system image, or shell-script packaging. First satisfy the user journey and atomic/idempotent installation requirements; choose the smallest maintainable packaging mechanism compatible with Ubuntu Touch and Raspberry Pi OS.

| Ticket | P | State | Deliverable and acceptance |
| --- | --- | --- | --- |
| `E6-T01` Define shipped bundle and compatibility manifest | P0 | planned | One release manifest pins all source commits, artifacts, kernel ABI, Pi requirements, hashes, and cross-component compatibility. |
| `E6-T02` Phone install/update/rollback | P0 | planned | A fresh supported phone can install all phone-side runtime pieces through one documented entry point; rerun is idempotent; update and rollback preserve recovery access. |
| `E6-T03` Pi install/image/update/rollback | P0 | planned | A fresh supported Pi can become the GUD-HDMI appliance through one documented entry point or supported image; boot brings up the gadget automatically. |
| `E6-T04` Boot/session orchestration | P0 | planned | After installation, phone and Pi services come up in the correct order and E3 activation works without manual races or shell commands. |
| `E6-T05` Health/doctor and diagnostics | P1 | planned | One short command/report identifies versions, GUD presence, active mode, bridge state, Pi gadget state, safety counters, and common failure class. |
| `E6-T06` Fresh-install MVP qualification | P0 | planned | Starting from supported clean phone/Pi images, follow only release instructions: install, reboot as required, connect, obtain external desktop, use it, disconnect/reconnect, collect diagnostics, update/rollback. No source checkout or manual enumeration is needed. |
| `E6-T07` Operator runbook and release bundle | P1 | planned | Concise install/use/recovery/uninstall documentation ships with hashed artifacts and known limitations. |

## 7. Prioritized execution queue

Use this order unless a concrete blocker requires a local detour:

1. `E3-T02` + `E3-T03` — event-driven discovery and automatic bridge lifecycle.
2. `E3-T04` — remove the manual sequence currently required for Lomiri to see/use the display.
3. `E3-T05` — connect/disconnect/reconnect UX matrix.
4. `E6-T01` — freeze the exact set of artifacts/configuration we ship.
5. `E6-T02` + `E6-T03` — one-entry-point phone and Pi installation/update/rollback.
6. `E6-T04` — boot/session orchestration tying installation to automatic activation.
7. `E6-T05` — health/doctor command.
8. `E6-T06` — fresh-install MVP qualification.
9. `E6-T07` — final concise operator/release documentation.
10. Post-MVP: `E4-T04/T05`, `E5-T04/T05/T06`, HVS scaling, USB3, portability, upstreaming.

### Immediate focus

**Next job: E3-T02/E3-T03 — make GUD presence automatically start the usable external-display path.**

The acceptance test is intentionally user-facing:

```text
supported phone + installed runtime
supported Pi + installed runtime
        ↓
normal boot
        ↓
connect the documented USB cable
        ↓
external desktop appears
```

No `ls /dev/dri`, manual card selection, enumeration helper, manual `xdispd`/`mirgud` start, or remembered recovery command is allowed in the successful normal path.

Once that works, package exactly that known-good path instead of designing an installer around the current manual development workflow.

## 8. MVP release gate

Call the first release MVP only when all are true:

- supported clean phone and Pi can be installed from versioned release artifacts;
- normal boot does not require a development checkout;
- connecting the supported Pi causes automatic GUD discovery and display activation;
- 720p and 1080p DirectExact remain correct;
- phone remains responsive during slow/failing external presentation;
- presenter remains bounded at pending <= 1 and in-flight <= 1;
- transport safety counters remain clean in normal operation;
- normal disconnect/reconnect requires no manual enumeration or compositor restart;
- `doctor`/diagnostic output can classify a failed activation;
- update/uninstall/rollback are documented and tested;
- known post-MVP limitations, including user-controlled resolution/placement, are explicit.

## 9. Decision rules

- Product usability now outranks additional transport research.
- Do not reopen codec, full-frame, mode-routing, or raw-USB investigations without a measured regression or release need.
- Do not add queues or logical GUD pipelining; preserve E1 ownership semantics.
- Do not sacrifice phone responsiveness for higher external FPS.
- Do not hard-code DRM card numbers in an installer or service.
- Do not make the operator reproduce internal enumeration or benchmark commands.
- Prefer boring, observable service orchestration over clever implicit state.
- Installation must be idempotent and recoverable before it is made more sophisticated.
- Resolution/layout UI and custom placement persistence are post-MVP.
- Keep evidence for release gates compact: version manifest, result summary, safety state, and only failure-relevant raw logs.
