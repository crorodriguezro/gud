# Linux Mobile GUD MVP Backlog

This backlog is the active implementation queue for the OnePlus 6 → Raspberry Pi Zero 2 W → HDMI product path.

For product scope and milestone ordering, see `PROJECT-ROADMAP.md`.
For detailed historical evidence, see `PROJECT-STATUS.md` and component evidence directories.

The old experiment-by-experiment backlog is preserved in git history. Do not reopen superseded transport/planner work unless a current MVP gate proves it necessary.

## Automatic Lomiri lifecycle implementation

The MVP lifecycle owner is the always-running `xdispd` system service. It
performs one bounded DRM startup scan, then waits on DRM/udev events; it starts
one managed `mirgud` child only for an exact live GUD instance and tears that
child down with the existing bounded containment. The Mir side remains the
public screencast/virtual-output path. No root udev rule launches a Mir client,
and no `/dev/dri/cardN` value is a selection policy.

The implementation is in `mir-android2-platform-gud`; the phone module-load
drop-in is `backport-4.9/systemd/modules-load.d/gud.conf`. Package details and
the required OnePlus VID/PID/UDC acceptance gate are documented in
`docs/xdisp-automatic-lifecycle.md`. Deploy the new artifact and run the
hardware matrix before changing the E3 roadmap state to hardware-verified.

## P0 — Establish the exact target build

## Current product state

Verified/accepted for the MVP baseline:

- Linux 4.9 OnePlus GUD host module builds and runs.
- Pi Zero 2 W FunctionFS GUD gadget works as the HDMI bridge.
- Long-lived transport safety and containment are accepted.
- `LatestFramePresenter` keeps one pending and one in-flight frame maximum.
- Production Mir integration uses public APIs through standalone `xdispd` / `mirgud`.
- Direct packed RGB565 + LZ4 is the production default.
- 1280x720 pixel correctness is verified.
- Exact physical 1280x720 and 1920x1080 modes route DirectExact.
- Full logical GUD payload semantics are qualified; the obsolete 12,800-byte logical planner is not the production architecture.
- Current performance is usable enough to proceed with product UX work:
  - stable 720p live desktop: approximately 21–28 FPS;
  - stable 1080p live desktop: approximately 19–20 FPS;
  - raw/incompressible USB2 limits are already characterized;
  - synthetic CPU scaling is known to be slow and is post-MVP.

## NOW — MVP activation UX

### [ ] E3-T02 — Event-driven GUD presence

**Priority:** P0

**Problem:** normal operation still relies on manual enumeration/bring-up steps.

**Goal:** the installed phone runtime notices the supported GUD device at boot or hot-add and owns discovery automatically.

Tasks:

- identify the correct stable event source for GUD device add/remove;
- discover by driver/device identity, never by assumed `/dev/dri/card1`;
- handle the device already being present when the service starts;
- reject stale/removed device instances;
- bound retries and log a useful reason when activation cannot proceed;
- make repeated add notifications idempotent.

Done when:

- boot with Pi attached reaches the discovered-ready state automatically;
- plugging the Pi after boot reaches the same state automatically;
- no operator enumeration command or DRM node selection is required.

### [ ] E3-T03 — Automatic `xdispd` / `mirgud` lifecycle

**Priority:** P0

**Source/basic-ioctl evidence:** the Pi hardware test creates `/dev/dri/card1`;
the active GUD card passes caps, RGB565 dumb-buffer map/write/destroy, RGB565
framebuffer creation, KMS enumeration, atomic-request build, atomic test-only
validation, and a real state-applying atomic commit. The synchronous no-transfer
completion path passes on Linux 4.9 without `WARNING:` or `flip_done` timeout
evidence. The 2026-08-29 ordered run found the Pi dynamically at `1-1.3` as
`1d50:614d`, and the matching RGB565 `gud-kms-smoke` run completed its atomic
modeset and one compressed framebuffer transfer. The phone and Pi evidence
contains no `BUG:`, `Oops`, `WARNING:`, `lockdep`, `use-after-free`, GUD state,
or atomic-update failure record.
**Goal:** once GUD is ready, automatically start the exact userspace path needed for the external output and stop it safely on removal.

Tasks:

- determine the correct user/session ownership for the bridge process;
- start only one managed instance;
- pass discovered device identity/configuration without hard-coded nodes;
- preserve `LatestFramePresenter` semantics;
- make duplicate starts harmless;
- stop/reap stale bridge processes after removal/failure;
- keep bounded containment for known Mir release stalls;
- expose concise lifecycle state for diagnostics.

Done when normal connect produces a running production bridge without a shell command.

### [ ] E3-T04 — Remove manual Lomiri bring-up sequence

**Priority:** P0

**Goal:** automate whatever enumeration/refresh/activation sequence is currently performed by hand so Lomiri sees and uses the external display.

Important:

- first document the current manual sequence exactly;
- distinguish steps that are only diagnostic from steps truly required by Mir/Lomiri;
- move required ordering into the product service;
- do not merely wrap developer commands in an opaque script;
- the sequence must be idempotent and safe when the device disappears halfway through.

Acceptance:

```text
normal phone boot
normal Pi boot
connect supported USB topology
        ↓
external desktop appears
```

No manual `ls`, enumeration helper, card selection, `xdispd`/`mirgud` start, compositor restart, or remembered development command in the successful path.

### [ ] E3-T05 — Connect/disconnect/reconnect UX matrix

**Priority:** P0

Test the automated path, not the development workflow.

Minimum matrix:

- Pi present before phone-side service starts;
- Pi connected after normal phone boot;
- clean disconnect while idle;
- disconnect while frames are flowing;
- reconnect on the same phone boot;
- repeated connect/disconnect cycles;
- device node/card number changes;
- Pi service restart while cable remains attached;
- phone bridge service restart while Pi remains attached.

Record only useful evidence:

- activation state transitions;
- discovered device identity;
- bridge PID/instance identity;
- successful external presentation count;
- safety counters;
- failure-relevant kernel/service excerpts.

Do not generate giant evidence bundles for successful repetitions.

## NEXT — package the known-good path

Do not design packaging around today's manual workflow. Finish the automatic activation path first, then package it.

### [ ] E6-T01 — Freeze the shipped bundle

**Priority:** P0

Create one compatibility manifest containing:

Phone:

- supported kernel ABI;
- `gud.ko` artifact + hash;
- `mirgud` / `xdispd` artifacts + hashes;
- activation/service files;
- configuration files genuinely required at runtime.

Pi:

- gadget binary + hash;
- service/configuration files;
- FunctionFS/configfs/bootstrap requirements;
- waiting/error presentation assets if required;
- supported base image/kernel assumptions.

Global:

- source commit for every artifact;
- bundle version;
- phone/Pi compatibility pair;
- rollback version/artifacts.

Done when a release candidate can be reconstructed without referring to a developer's build directory.

### [ ] E6-T02 — One-entry-point phone installer/update/rollback

**Priority:** P0

Requirements:

- starts from the supported Ubuntu Touch image;
- validates kernel ABI before replacing/loading `gud.ko`;
- installs the bridge binaries and activation integration;
- preserves recovery/SSH access;
- is safe to rerun;
- does not overwrite an unknown incompatible install silently;
- records installed bundle version;
- supports uninstall/rollback to the previous known version;
- reports a clear success/failure summary.

Implementation can initially be a well-structured installer if native packaging would slow the MVP. Do not lock the design to `.deb`, Click, or another format before confirming what best fits the target image.

Done when a fresh supported phone can be prepared through one documented entry point without copying files manually one by one.

### [ ] E6-T03 — One-entry-point Pi installer or supported image

**Priority:** P0

Requirements:

- starts from the supported Raspberry Pi OS/base image or a documented prepared image;
- installs the gadget runtime and exact service configuration;
- enables required boot-time pieces;
- brings up FunctionFS/GUD automatically at boot;
- owns the HDMI waiting/error screen lifecycle;
- validates prerequisites and refuses incompatible state;
- is safe to rerun;
- supports update and rollback.

Done when a fresh Pi becomes the appliance without manual configfs/FunctionFS/service setup commands.

### [ ] E6-T04 — Boot/session orchestration

**Priority:** P0

Verify installation produces the intended runtime automatically:

```text
Pi boot
  → gadget service ready

Phone boot/session
  → activation service ready

USB connect
  → GUD discovered
  → bridge starts
  → Lomiri external output appears
```

Handle ordering races:

- phone first / Pi later;
- Pi first / phone later;
- service restart;
- temporary unavailable device;
- stale bridge instance.

No infinite polling loops and no unbounded restart storm.

## THEN — supportability and release gate

### [ ] E6-T05 — `doctor` / health report

**Priority:** P1

Provide one concise supported diagnostic entry point (one per device is fine initially) that reports only actionable state.

Phone report should include:

- bundle version and artifact hashes;
- kernel ABI and loaded GUD module version;
- discovered GUD device/node;
- connector/mode summary;
- activation/bridge service state;
- bridge PID and current presentation state;
- recent host errors relevant to GUD/USB/Mir.

Pi report should include:

- bundle version;
- gadget service state;
- UDC/configuration state;
- current physical HDMI mode;
- FunctionFS transaction state;
- Poisoned/timeouts/processing failures;
- throttling/temperature;
- recent DWC2/VC4 errors.

Output should end with a coarse classification such as:

- READY;
- PHONE_GUD_NOT_FOUND;
- PI_GADGET_NOT_READY;
- BRIDGE_NOT_RUNNING;
- MIR_ACTIVATION_FAILED;
- TRANSPORT_CONTAINED;
- PI_DISPLAY_FAILED.

### [ ] E6-T06 — Fresh-install MVP qualification

**Priority:** P0

Start from supported clean images, not existing development devices.

A tester following only release instructions must be able to:

1. install phone bundle;
2. install/provision Pi bundle;
3. reboot as requested;
4. connect HDMI/USB;
5. obtain external desktop automatically;
6. use 720p and 1080p DirectExact modes as automatically selected by the current policy;
7. disconnect/reconnect;
8. run health diagnostics;
9. update or roll back;
10. uninstall/recover.

Fail the gate if success requires:

- source checkout;
- manual DRM enumeration;
- selecting `/dev/dri/cardX` by hand;
- manually starting `xdispd` or `mirgud`;
- benchmark-only tools;
- undocumented service restarts;
- compositor restart as a routine step.

### [ ] E6-T07 — Compact operator runbook/release bundle

**Priority:** P1

Keep it short and user-facing:

- supported hardware/software;
- install;
- connect/use;
- status/doctor;
- update;
- rollback/uninstall;
- known limitations.

Do not expose internal benchmark procedures as normal operating instructions.

## Post-MVP backlog

These are deliberately not on the current critical path.

### Display configuration

- [ ] `E4-T04` investigate/implement user-visible resolution selection, placement, and persistence in Lomiri.
- [ ] `E4-T05` full configurable geometry/placement/reconnect matrix.

MVP accepts Lomiri's automatic mode/layout policy as long as the output is usable.

### Performance

- [ ] damage-aware updates if real UX shows a need;
- [ ] reduce full-frame LZ4 CPU cost;
- [ ] deterministic Mir desktop workload qualification;
- [ ] compare VC4/HVS hardware scaling with current CPU ScaledFallback;
- [ ] USB3-capable transport/hardware investigation if higher incompressible 1080p throughput becomes a product requirement.

### Reliability/research

- [ ] deeper diagnosis of any remaining OnePlus same-boot reconnect limitation after the automated recovery path is implemented;
- [ ] generic FunctionFS/AIO upstream work;
- [ ] generic Linux 4.9 compatibility cleanup;
- [ ] generic GUD gadget feature parity;
- [ ] other host OS/platform ports.

## Do not work on now

Unless a current P0 gate fails because of it, do not spend MVP time on:

- old 12,800-byte logical payload planner work;
- new codecs;
- logical GUD pipelining;
- increasing presenter queue depth;
- Mir core patches;
- Android2 GUD-specific integration;
- user-configurable resolution/placement;
- scaler optimization;
- USB3;
- upstreaming/generalization.

## MVP definition of done

The project is ready for an MVP release when a fresh operator can take supported phone/Pi base images and reach this flow:

```text
install phone
install/provision Pi
reboot if requested
connect HDMI + USB
external desktop appears automatically
use it
disconnect/reconnect
run doctor if needed
update/rollback safely
```

with:

- no manual enumeration;
- no hard-coded DRM card number;
- no manual bridge start;
- correct DirectExact output;
- bounded presenter/transport ownership;
- no phone freeze on external-display failure;
- clean normal safety counters;
- compact release artifacts and documentation.
