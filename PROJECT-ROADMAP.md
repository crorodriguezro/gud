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
State: partially verified; activation UX remains incomplete
Owners: `mir-android2-platform-gud`, `gud`, coordination

Outcome: the user connects the supported Pi and the external output appears without manual DRM/Mir enumeration or developer commands.

| Ticket | P | State | Deliverable and acceptance |
| --- | --- | --- | --- |
| `E3-T01` Discover GUD DRM by identity | P0 | verified | Production selects the live GUD DRM device dynamically rather than assuming a card number. |
| `E3-T02` Event-driven device presence | P0 | planned | Boot-time presence and later add/remove events drive bounded discovery; no manual enumeration command is required. |
| `E3-T03` Automatic bridge lifecycle | P0 | planned | The supported session automatically starts/stops the required `xdispd`/`mirgud` path when GUD becomes usable or disappears. No operator shell command is required in the normal path. |
| `E3-T04` Automatic Lomiri output activation | P0 | planned | The sequence currently performed manually to make Lomiri see/use the external display is owned by the product service and is idempotent across boot and reconnect. |
| `E3-T05` Connect/disconnect/reconnect UX matrix | P0 | planned | Starting from a normal boot, repeated cable connect/disconnect cycles do not require DRM node selection, enumeration commands, service restarts, or compositor restart. Known platform limitations must be automatically recovered where possible and clearly diagnosed otherwise. |

Do not hide the manual workflow behind a single undocumented script and call it complete. The product service must own ordering, retries, instance identity, stale-resource cleanup, and bounded failure behavior.

### E4 — Display correctness and mode policy

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
