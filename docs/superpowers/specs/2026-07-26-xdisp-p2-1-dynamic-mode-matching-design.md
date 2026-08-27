# XDISP-P2.1 Dynamic Physical Mode Matching Design

> **SUPERSEDED TRANSPORT ASSUMPTIONS**
>
> The 12,800-byte host logical-payload ceiling and 16-KiB logical
> FunctionFS ceiling described below are historical qualification constraints
> and are no longer production limits. Current architecture uses one
> negotiated logical GUD update. Qualified examples are 1280x720 RGB565 =
> 1,843,200 bytes and 1920x1080 RGB565 = 4,147,200 bytes. Pi internal
> FunctionFS/DWC2 request chunking remains an implementation detail below the
> logical GUD transaction boundary. Historical commands and evidence remain
> unchanged; do not use their limits to characterize current production.

## Purpose

Remove avoidable Raspberry Pi software scaling when a committed GUD mode is
an exact mode of the connected physical display, without removing the
aspect-ratio-preserving scaler needed by the OnePlus-as-display use case and
other non-exact modes.

This is a userspace presentation optimization. It does not modify either
kernel, the normal OnePlus `gud.ko`, the Mir/Lomiri plugin, the qualified
12,800-byte host-payload boundary, or the 16 KiB FunctionFS read ceiling.

## Context

The current Rust `gud-drm` service selects one physical DRM mode at process
startup, creates two buffers of that size, and keeps that physical mode for
the lifetime of the process. It advertises the connector modes plus synthetic
portrait modes. When the committed GUD source dimensions differ from the
fixed physical framebuffer, it:

1. accumulates each received rectangle in a source-sized shadow framebuffer;
2. scales the entire shadow framebuffer into a physical back buffer;
3. presents that back buffer; and
4. repeats the complete scale and presentation for every GUD rectangle.

That scaling path was intentionally added so lower-resolution modes fill the
fixed OnePlus panel rather than occupying only a portion of it. It preserves
aspect ratio, centers the image, and supplies black borders. It must remain
available.

The 2026-07-26 Pi native-scanout gate exposed an avoidable case. A 1280x720
GUD source was initially scaled into a fixed 1920x1080 Pi scanout. Ten video
frames produced 113 rectangles, so the Pi repeated a roughly 39--40 ms
full-frame scale/present operation 113 times. Selecting the Pi's real
1280x720 HDMI timing made every rectangle use the direct-copy path:

- update rate reached the configured 5-fps pacing ceiling at 4.999 fps;
- average synchronous host commit fell from 489.394 ms to 50.097 ms;
- all 113 transfers still returned from `InFlight` to `Idle`; and
- no new host timeout or Pi kernel fault occurred.

The 5-fps number is a paced lower bound, not maximum throughput. The next
implementation must remove the fixed test override and then run an unpaced
benchmark.

## Decision

Use route selection per successfully committed GUD mode:

1. **Exact physical timing:** select that physical DRM mode and use direct
   rectangle copies with no geometric scaling.
2. **No exact physical timing:** retain the existing source shadow,
   aspect-ratio-preserving scaler, physical back buffer, and presentation
   fallback.
3. **Exact-mode preparation or switch failure:** retain a valid existing
   scanout and use the scaled fallback. Record the optimization failure; do
   not leave the host with a committed state that has nowhere safe to render.

No-scaling therefore becomes the preferred fast path for exact matches, not a
global default. The OnePlus fixed-panel behavior remains unchanged for lower
or synthetic portrait modes.

The first implementation is enabled only by:

```text
GUD_TEST_DYNAMIC_MODE_MATCH=1
```

It is mutually exclusive with the fixed
`GUD_TEST_OUTPUT_MODE=WIDTHxHEIGHT` gate. Absence of the new control preserves
the current fixed-physical-mode/scaled behavior. Making dynamic matching the
normal default requires a later promotion commit after the complete hardware
gates pass.

## Ownership and interfaces

- **Coordination and evidence:** `gud`
- **Implementation owner:** `gud-gadget`
- **Protocol/state owner:** `gud-gadget/gadget/src/lib.rs`
- **DRM presentation owner:** `gud-gadget/drm/src/main.rs`
- **Safe host clients:** the separately preserved OnePlus adaptive-LZ4
  diagnostic module and controlled laptop tests with a 12,800-byte descriptor
  ceiling
- **Component procedure:**
  `../../../../gud-gadget/docs/XDISP-P2.1-DYNAMIC-MODE-MATCHING-TEST.md`
- **Implementation plan:**
  `../plans/2026-07-26-xdisp-p2-1-dynamic-mode-matching.md`

`XDISP-P0.1` remains blocked. `XDISP-P0.2` remains unstarted. This work gives
no credit toward either status transition.

## Mode identity and route catalog

Create one immutable catalog when the physical connector is discovered.

Each route key contains:

- connector index;
- pixel clock;
- horizontal active, sync start, sync end, and total;
- vertical active, sync start, sync end, and total; and
- only GUD user-visible timing flags:
  `raw_drm_flags & GUD_DISPLAY_MODE_FLAG_USER_MASK` (`0x000033ff`).

The advertisement-only `GUD_DISPLAY_MODE_FLAG_PREFERRED` bit is not part of
physical timing identity. It affects which mode the host prefers, but it must
not make an otherwise identical state fail matching or validation. Multiple
timings with the same dimensions remain distinct.

Apply the user mask both when building `ModeKey` and when serializing
advertised modes, then add the preferred bit only to the selected
advertisement. Canonicalize/deduplicate physical entries that differ only in
unsupported or 3D/private DRM flags, choosing one deterministic real DRM
`Mode` for `set_crtc`. This matches the host, which masks CHECK flags with the
same user mask, and prevents echoed advertised modes from failing membership.

The catalog classifies every advertised mode as:

- `Exact(physical_mode)` when its complete timing matches a real connector
  mode; or
- `ScaledFallback` for derived/synthetic modes with no physical match.

The current synthetic portrait modes remain advertised and classified as
scaled fallbacks. USB advertised preference remains independent of the
currently selected physical mode. An explicit connector preference wins;
otherwise physical mode-list index zero remains the deterministic startup
fallback. Select preference by catalog entry/index rather than mode name so
exactly one advertised timing receives the preferred bit. GUD connector index
zero remains a separate protocol fact.

## Physical scanout ownership

Replace the loose parallel arrays and mode variables with stable allocation
slots, separate role indices, and borrowed mapped views:

```text
ScanoutAllocation
  physical mode and complete timing
  two owned dumb-buffer/framebuffer resource records
  pitch
  persistent front-buffer index

ScanoutPool
  up to three stable Box<ScanoutAllocation> slots
  separate active, baseline, and candidate slot indices

MappedActive<'slot>
  two mappings borrowed from ScanoutAllocation
  mutable borrow of the persistent front-buffer index

MappedTransition<'old, 'target>
  old active mappings kept usable
  fully established target mappings

MaxSourceShadow
  one startup-allocated byte vector sized for the largest advertised RGB565
  mode
  ShadowRasterIdentity(width, height, format)
  content-valid bit and active dimensions/pitch
  metadata/content changed only on COMMIT or direct-buffer invalidation
```

The DRM crate's dumb-buffer mapping borrows its dumb buffer. Do not create an
unsafe self-referential structure that tries to own both. `ScanoutAllocation`
and its resource records must not implement `Clone` or `Copy`. Boxed
allocations remain in stable slots; transitions change role indices rather
than moving an allocation while a mapping borrows it. Use split borrows over
the slot storage and role metadata so the active and target slots can be
mapped simultaneously.

The DRM crate's dumb-buffer and framebuffer handles do not remove kernel
objects on ordinary Rust drop. Every allocation therefore has an explicit,
idempotent `release(card)` path that removes each framebuffer before
destroying its dumb buffer. Partial construction unwinds in the same reverse
order. Candidate replacement and a successfully replaced old exact allocation
call this path immediately after all mappings of that slot are gone. The
startup baseline is the retention exception. Final card-FD close is
containment for process exit, not the normal resource-lifecycle mechanism.

A `ScanoutManager` owns:

- the stable scanout pool and its active/baseline/candidate roles;
- one `PendingPlan` keyed by protocol generation;
- a failed-route cache keyed by logical mode plus current physical mode;
- the committed source mode and selected route;
- the preallocated maximum source shadow; and
- mode-check, allocation, switch, fallback, no-op, and failure counters.

The working scanout is never destroyed before a replacement has been
successfully selected. Pending candidates are bounded to one; replacing a
pending state releases the old candidate exactly once. Repeated selection
must not grow framebuffer, mapping, or handle counts.

Compute a checked logical memory estimate from the catalog, not a fixed
1920x1080 assumption:

```text
max_shadow_bytes =
  max_checked(advertised_width * advertised_height * 2)
logical_scanout_estimate =
  3 slots * 2 buffers * max_checked(physical_width * physical_height * 2)
logical_peak_estimate = logical_scanout_estimate + max_shadow_bytes
```

This is a conservative logical-pixel estimate, not an upper bound on future
DRM backing allocations. Dumb-buffer pitch and mapping length are known only
after `CREATE_DUMB`/mmap and may exceed logical pixels. Fail startup before UDC
bind on estimate/shadow arithmetic overflow or fallible maximum-shadow
allocation failure. For every actual slot construction, record each returned
pitch and mapping length, use checked arithmetic to maintain current and
observed-peak live allocation bytes, and release a candidate on accounting
overflow. Connector catalogs that advertise 4K or larger modes therefore get
an honest larger estimate and exact runtime accounting without pretending
future pitches were known before bind.

The active mappings remain alive while preparing a candidate. CHECK may
temporarily map/initialize a candidate to prove it usable, then leave that
candidate staged and unmapped without touching the active mapping. At COMMIT,
map the complete target again before `set_crtc` while the old active mappings
remain valid. On success, keep the target mappings alive, update only role
indices, then drop/release the old slot if it is not the retained baseline. On
mapping or `set_crtc` failure, drop/release the target candidate and continue
with the still-mapped old active scanout. There is no post-switch remap window.

Same-active checks, same-state commits, failed-route cache hits, and all
notifications when the feature is disabled remain mapped no-ops. This avoids
self-referential ownership, preserves a usable fallback across every fallible
transition, and prevents per-frame map/unmap churn.

## Protocol events

The current protocol library commits only metadata and does not tell
`gud-drm` about state-check/commit transitions. Extend its event contract with
an owned snapshot:

```text
DisplayStateSnapshot
  complete DisplayMode
  pixel format
  GUD connector index
  process-lifetime monotonically increasing pending-state generation
```

`ActiveScanoutState` currently carries only width, height, format, and
connector, so it is insufficient and must not be reused as though it contained
timing. Emit:

- `StateChecked(DisplayStateSnapshot)`
- `StateCommitted(DisplayStateSnapshot)`
- one composite
  `ProtocolStateInvalidated { generation: Option<u64>, reason }` event for
  pending invalidation and lifecycle/protocol reset, preserving the current
  one-`Option<Event>` result per FunctionFS event

Every path that clears protocol pending state must notify the presentation
owner: invalid replacement check, failed or no-pending commit, Bind, Enable,
Suspend, Resume, and Disable/disconnect. A staged candidate is keyed by
generation. Replacement with a different route or invalidation releases it
immediately. A same-timing replacement rekeys the existing candidate to the
new generation without reallocating; the old generation can no longer
activate it. A matching successful commit removes it from the candidate slot
by transferring ownership to the active scanout only after `set_crtc`
succeeds. Reset events never reset or reuse the generation counter. A
Disable/disconnect invalidation must retain the consumers' existing
disconnect behavior.

The expanded event contract must also classify host activity. Bind, Enable,
Suspend, Resume, and other lifecycle-only invalidations do not set
`had_host_session`. Genuine host control traffic, including a valid/invalid
CHECK or COMMIT, does. Disable/disconnect retains the existing disconnect path.
Both `gud-drm` and `gud-viewerd` replace their current broad
“every non-disconnect event means host activity” test with this explicit
classification so local Bind cannot create a detached restart loop.

Basic connector, format, mode, and property validation remains in the gadget
library. Its advertised-mode membership check must compare normalized timing
identity rather than raw `DisplayMode` equality: mask both sides with the GUD
user mask and ignore the separate preferred metadata bit. A valid check still
creates protocol pending state; a valid commit still promotes it to committed
state. The new events let the presentation owner prepare and activate matching
physical resources before it processes a later `Buffer` event.

Every protocol path that clears or replaces pending state must also invalidate
the matching DRM candidate. A rejected state check or commit may not leave a
stale candidate eligible for a later commit.

The presentation owner converts each `StateChecked` snapshot into exactly one
generation-keyed `PendingPlan`:

- `ExactActiveNoOp`
- `ExactCandidate(slot)`
- `ScaledBaseline { switch_required }`
- `ScaledCurrentAfterFailure(reason)`

CHECK lookup order is active route, prepared same-timing candidate,
failed-route cache, new exact preparation, then scaled fallback. COMMIT
consumes only the `PendingPlan` with the same generation; it does not
reclassify or allocate. This makes invalidation deterministic and prevents an
unchanged failed exact route from reallocating before its no-op decision is
reached.

`GUD_DISPLAY_FLAG_STATUS_ON_SET` remains disabled. In the current FunctionFS
architecture it deadlocks `SET_BUFFER`: the main loop blocks waiting for bulk
data while the host waits for `GET_STATUS` before sending that data.
Supporting it requires a separate control/bulk concurrency redesign and is
not part of this change.

Because runtime DRM failure cannot be reported through status-on-set here,
every accepted logical mode must retain a functional scaled fallback. This is
an intentional difference from the upstream C gadget's transactional DRM
validation: the test-only optimization occurs after protocol acceptance and
falls back to scaling if physical preparation or switching fails. Strictly
rejecting a state after a DRM failure is out of scope until the control and
bulk paths can service status requests concurrently.

The descriptor and every hardware preflight must prove
`GUD_DISPLAY_FLAG_STATUS_ON_SET == 0`. Protocol-invalid connector, format, or
timing requests are tested only against deterministic unit/mock backends.
Sending one on real hardware is prohibited: without status-on-set, a host can
continue into a bulk transfer after the rejected request and recreate a
receive timeout.

## Transition sequence

### Startup

1. Discover the connected connector, its complete mode list, encoder, and
   compatible CRTC.
2. Select the normal deterministic startup/fallback physical mode.
3. Build/canonicalize the route catalog and compute the checked logical
   scanout estimate plus exact shadow-capacity requirement.
4. Fallibly allocate and fully size the maximum advertised RGB565 source
   shadow. Do not bind USB if this fails.
5. Create and map one baseline `ScanoutAllocation`, show the waiting screen,
   and prove CRTC setup before exposing USB.
6. Advertise modes with preference independent of later physical selection.
7. Bind the USB gadget.

The existing bounded startup retry remains limited to startup, before USB is
visible.

### `SET_STATE_CHECK`

After protocol validation emits `StateChecked`, classify it while the current
active scanout remains mapped:

1. Look up the active route, same-timing prepared candidate, and failed-route
   cache before attempting any allocation.
2. If the matching exact mode is already active or prepared, create the
   appropriate no-op/candidate `PendingPlan`.
   Rekey a same-timing prepared candidate to the new pending generation.
3. If it is a different exact physical mode, allocate/map a candidate while
   retaining the mapped current working scanout. Initialize both candidate
   buffers to deterministic black, flush where supported, set its persistent
   front index to zero, and unmap only the candidate before staging.
4. If allocation or preparation fails, record a scaled-fallback decision and
   retain the mapped working scanout. Create a
   `ScaledCurrentAfterFailure` plan, release any partial candidate, and insert
   the separate `(logical,current-physical)` failure entry; no source-shadow
   allocation is needed because maximum capacity already exists.
5. For a non-exact mode, create a `ScaledBaseline` plan without altering the
   current committed shadow contents.

Candidate allocation happens here rather than after commit. The host's next
control request remains queued until the single-threaded event loop completes
this work. The state-check control transfer has already completed, so record
its latency separately from the later commit control latency; preparation
delays servicing the next control request rather than extending the
acknowledged state check. Preparation gets one allocation/mapping attempt with
no sleep or retry. The initial hardware gate fails if `mode_prepare_ms`
exceeds 1,000 ms or any host control request times out. The 1,000-ms threshold
is an acceptance budget, not a cancellable userspace timeout.

### `SET_STATE_COMMIT`

After protocol commit emits `StateCommitted`, consume only its
generation-matched `PendingPlan` before processing a queued `SET_BUFFER`:

1. Look up the generation and snapshot in the pending plan. Do not reclassify
   or allocate.
2. If the plan is missing, stale-generation, or snapshot-mismatched, drop and
   explicitly release any stale candidate, activate the preallocated shadow
   for the committed snapshot, retain the mapped current scanout, select
   `scaled-current-after-failure(plan-mismatch)`, and fail the optimization
   gate. Later bulk remains safe for the already committed protocol state.
3. For the same active exact mode, do nothing.
4. For a new exact mode, fully map the prepared candidate while keeping the
   old active mappings, then issue exactly one active-session CRTC switch.
5. On success, keep the candidate mappings as the new active view, update role
   indices, and retain/release old resources according to the bounded baseline
   rule.
6. On candidate-map or switch failure, issue no recovery modeset. Drop target
   mappings, explicitly invalidate and release that candidate exactly once,
   insert/retain the separate `(logical,current-physical)` failure entry,
   preserve the still-mapped known-good scanout, and select
   `scaled-current-after-failure`.
7. For a non-exact mode, select the deterministic startup/fallback scanout and
   the existing scaler. If a switch is required, fully map the baseline while
   retaining the old mapping and give the transition one CRTC attempt. If
   mapping or switching fails, retain the still-mapped current scanout and use
   `scaled-current-after-failure`.
8. Only after COMMIT, activate shadow identity/content using the rules below,
   without allocation. CHECK and invalidation never repurpose the old
   committed shadow.

Do not reuse the startup loop of repeated `set_crtc` calls and 250-ms sleeps.
An active-session switch gets one attempt and no sleep. Log
`mode_prepare_ms` and `mode_switch_ms`. The initial hardware gate fails if a
switch exceeds 250 ms, even if it eventually succeeds; this is an acceptance
budget, not a userspace timeout capable of cancelling a blocked DRM ioctl.

The host may queue enable or `SET_BUFFER` immediately after the commit control
status completes. Main-loop ordering must ensure no userspace bulk read starts
until route activation or fallback selection is complete.

An outstanding scaled page flip can make a following `set_crtc` return
`EBUSY`. Do not retry or sleep. Keep the current active/baseline resources,
drop target mappings, invalidate and release an unselected candidate exactly
once, classify the result as `scaled-current-after-failure`, and scale into
the still-mapped current scanout. Retire a replaced old exact allocation only
after a successful `set_crtc` has made the target active. The hardware matrix
must explicitly cover scaled output with a recently submitted flip followed
by an exact-mode commit.

### Repeated same-mode commits

The OnePlus may check and commit the same mode for every frame. Ten identical
frame commits must produce:

- one initial physical switch at most;
- no repeated dumb-buffer allocation;
- no repeated CRTC modeset; and
- ten normal frame submissions into the already active route.

A failed exact transition is also cached for that unchanged logical/physical
route. Same-state recommits do not repeatedly allocate or modeset; a different
logical mode alone does not clear the entry. The candidate/PendingPlan
identity is consumed and released, then a separate
`(logical_mode, current_physical_mode)` failure entry is inserted or retained.
Retry is permitted only after the current physical timing successfully changes
(so the key differs) or a fresh service session clears the cache.

### Suspend, resume, disconnect, and shutdown

- Discard an uncommitted candidate when protocol pending state is cleared.
- Do not modeset merely because FunctionFS reports suspend, resume, or
  disconnect.
- Keep the last valid physical scanout until a later committed state or clean
  service restart.
- Preserve the existing `Idle`/`InFlight`/`Poisoned` teardown containment.
- During an active session, release only inactive candidates or old
  allocations that a successful `set_crtc` has replaced, always outside bulk
  receive. During final shutdown, release all remaining physical scanout
  resources only after UDC unbind, FunctionFS removal, and endpoint-owner
  drop.

## Scaled fallback behavior

The fallback is the existing behavior, not a reduced feature:

- update the source-sized shadow at the received rectangle;
- fit the complete source into the physical target while preserving aspect
  ratio;
- center it and clear uncovered borders;
- render into the physical back buffer; and
- present by swapping front/back roles.

Shadow accumulation follows an explicit validity contract:

- `ShadowRasterIdentity` is only committed width, height, and transfer format;
  physical/logical timing fields do not change pixel layout.
- A same-identity scaled recommit preserves existing pixels; this is required
  for damage rectangles, repeated same-mode commits, and same-resolution/
  different-timing scaled transitions.
- A geometry/format identity change zeros the new active shadow region once
  before its first rectangle, then marks that identity valid.
- Entering a direct route marks the shadow content invalid because direct
  rectangles do not update it. A later direct-to-scaled transition or exact
  switch failure therefore zeros the active region once, even if dimensions
  happen to match.
- A COMMIT plan mismatch uses the same allocation-free identity activation.
  CHECK and invalidation never clear or resize committed content.

Zeroing changes no capacity and cannot fail after protocol acceptance.

For an exact match, direct copy retains old pixels in the live framebuffer and
updates only the received rectangle. Transport validation, decompression,
RGB565 copy, RGB888-to-RGB565 conversion, dirty handling, dumps, and
instrumentation remain available on both routes.

Rendering selects direct copy or scaling from the committed route, not merely
from a width/height comparison. Equal dimensions with different timing can be
a scaled-current failure route and must not be mislabeled or rendered as
native.

## Presentation and tearing

Dynamic mode matching does not create a GUD end-of-frame signal. Direct native
copy can expose progressive updates or tearing because the display may scan
while rectangles are written. The current scaled path is also not truly
frame-atomic: it rebuilds and flips after each rectangle.

Native off-screen accumulation and a timing/damage-based flip policy are a
separate P2.1 presentation phase. They must be benchmarked independently so
mode matching is not conflated with batching latency or frame-boundary
heuristics.

## Observability contract

For every check and commit, log one structured decision containing:

- source mode key and connector;
- route: `exact`, `scaled-baseline`, or
  `scaled-current-after-failure`;
- reason: `exact-match`, `no-match`, `prepare-failed`, `switch-failed`, or
  `baseline-switch-failed`;
- old and requested physical timings;
- protocol generation, consumed `PendingPlan`, and failed-cache hit/miss;
- whether allocation and switch were no-ops;
- `mode_prepare_ms` and `mode_switch_ms`;
- active framebuffer dimensions and pitch;
- logical catalog estimate, exact shadow capacity, per-slot returned
  pitch/mapping lengths, current/observed actual live bytes, and serialized
  descriptor fields;
- cumulative candidate allocations/releases, switches, no-ops, and
  fallbacks, explicit framebuffer removals/dumb-buffer destroys, and
  map/unmap operations; and
- whether `GUD_TEST_DYNAMIC_MODE_MATCH` is active.

Existing `frame_stats` must retain `source`, `scaled`, receive/copy/scale/flush
times, and payload sequence. Evidence must correlate mode events with the
first later `SET_BUFFER`.

## Safety invariants

- Never modeset, allocate/drop an active scanout, or change route inside a
  FunctionFS bulk receive.
- Never destroy the working framebuffer before a replacement succeeds.
- Never drop the active mappings until a target is completely mapped and
  `set_crtc` succeeds; there is no fallible target remap after switching.
- Allocate maximum advertised RGB565 shadow capacity before UDC bind. CHECK
  never reallocates or repurposes committed shadow contents.
- Never accept a later `Buffer` event against presentation dimensions that do
  not match the committed logical state.
- Never send a deliberately invalid state request to the hardware gadget;
  keep negative protocol tests in deterministic unit/mock tests.
- Keep every initial OnePlus hardware payload at or below 12,800 bytes and the
  Pi read ceiling at 16 KiB.
- Define the payload cap from the actual host bulk URB submit/completion
  length, not the uncompressed `SET_BUFFER.length` or a Pi read size. Correlate
  every `SET_BUFFER` with one host bulk completion and one complete Pi receive.
- Do not stop, restart, reboot, signal, deploy, or roll back a Pi service whose
  receive state is `InFlight` or `Poisoned`.
- A never-attached service may stop without disconnect/`Suspend` evidence only
  when the cable is absent, structured state proves no host session, receive
  is `Idle`, the UDC never configured in that instance, and kernels are clean.
  After any enumeration, the full detach/card-removal/`Idle`/`Suspend`
  boundary is mandatory.
- Laptop hardware gates run with the graphics session quiesced and no
  compositor/display-manager opener of the GUD DRM node. Descriptor usbmon
  evidence is decoded before the runner, and a full-session capture covers all
  later bulk traffic.
- A missing exact match is normal fallback behavior, not an error.
- A failed exact optimization may not corrupt or crop fallback output.
- No Pi or OnePlus kernel build is part of this design.

## Projects and expected files

Runtime implementation changes are limited to `gud-gadget`:

- `gadget/src/lib.rs` — checked/committed/lifecycle event exposure;
- `drm/src/main.rs` — scanout allocation/mapped-view split, route catalog,
  candidate preparation, bounded commit switch, fallback, and counters;
- `viewerd/src/main.rs` — exhaustive handling of the expanded event contract
  without changing viewer presentation;
- focused Rust tests in those crates;
- `systemd/test-only/50-xdisp-p2.1-dynamic-mode-match.conf` — temporary
  dynamic-mode enable;
- `systemd/test-only/60-xdisp-p2.1-laptop-max-12800.conf` — separate
  max-only, default-LZ4 laptop cap; and
- `docs/XDISP-P2.1-DYNAMIC-MODE-MATCHING-TEST.md` — executable component
  procedure.

The `gud` repository owns this specification, the paired plan, coordination
links,
`backport-4.9/tests/decode-gud-usbmon-descriptor.py`,
`backport-4.9/tests/gud-kms-mode-sequence.c`, their contract tests, and
ignored raw evidence. The normal OnePlus driver source and artifact remain
unchanged.

## Non-goals

- Removing the scaler or synthetic portrait modes
- Promoting dynamic matching as normal behavior before hardware evidence
- Hardware rotation, plane scaling, filtering, HDR, HDMI audio, or multiple
  connectors
- Solving native-path tearing or inventing a frame boundary
- Changing adaptive compression or the 12,800-byte boundary
- FunctionFS read-size benchmarking
- Enabling `STATUS_ON_SET`
- Fixing the Pi DWC2 kernel failure
- Modifying or starting Mir/Lomiri `XDISP-P0.2`

## Acceptance

The dynamic substep is ready for promotion only when all of these pass:

1. Offline state, route, ownership, repeated-commit, cleanup, descriptor,
   shadow identity/partial-damage, stable old/target mapping, missing/stale/
   mismatched pending-plan fallback, failed-cache retry, logical-versus-actual
   memory accounting, user-mask, and deterministic DRM-backend failure tests
   pass, plus a clean AArch64 release build. Allocation, mapping, framebuffer
   creation, CRTC switching, and cleanup failures are all covered; `vkms`
   remains optional.
2. A capped high-speed laptop preflight safely performs one
   1920x1080-to-1280x720 dynamic transition, one complete frame, physical
   detach, and clean receive-idle teardown. Its max-only `60-*` cap retains
   LZ4 and is removed through a detached restart before OnePlus is exposed.
   The laptop is headless/quiesced with no unsolicited node opener; descriptor
   and full-payload usbmon captures are retained.
3. Starting from normal 1920x1080 physical output with no fixed mode override,
   one 1280x720 OnePlus commit produces exactly one complete-timing
   1920x1080-to-1280x720 switch before the first buffer, with preparation at
   or below 1,000 ms and switching at or below 250 ms. The preserved
   diagnostic module identity, high-speed enumeration, descriptor flags, and
   control latencies are recorded first.
4. The one-frame gate covers source rows 0--719 exactly once and accounts for
   1,843,200 RGB565 source bytes. Every actual bulk URB is no larger than
   12,800 bytes and has exactly one completion and one Pi
   `InFlight`-to-`Idle` receive.
5. The preserved ten-frame clip produces no further physical switch,
   `scaled=false` for every payload, ten complete source frames, exact
   transfer/receive correlation, and no host or Pi fault.
6. Steady average commit is no worse than 20% above the fixed-native baseline
   (provisional target at or below 60 ms), steady p95 and maximum are
   recorded, steady maximum is at or below 85 ms, and the cold first modeset
   is reported separately.
7. A capped-laptop exact-A-to-synthetic-to-exact-B gate uses non-baseline exact
   timings (distinct A/B when available, or A repeated after baseline),
   submits one complete frame in every route, and proves that synthetic
   content returns to the baseline and fills it through the existing scaler.
   Every actual URB and Pi receive correlates, and repeated exact commits do
   not leak resources. The max-only descriptor override is installed and
   removed only through detached, receive-idle service sessions.
8. Three fresh-Pi-boot mini-cycles and repeated
   exact-to-fallback-to-exact sequences use the capped laptop and each end in
   physical detach and clean lifecycle evidence. The normal descriptor is
   restored afterward. Old/new boot IDs, previous-boot service/kernel
   journals, pstore, and watchdog state prove every boot was intentional and
   clean.
9. Physical detach and one controlled, receive-idle service teardown/restart
   with the OnePlus diagnostic module and normal descriptor remain clean.
10. An unpaced native benchmark records its exact command, frame count, input
   asset hash, CPU-sampling method, OnePlus diagnostic/normal-descriptor
   identity, achieved FPS, p50/p95/p99/max commit, host/Pi CPU, rectangles,
   compression attempts, payload bytes, switch latency, first-frame latency,
   dropped frames, and visible tearing.

Passing these gates permits a separate review of default-on promotion. It does
not verify all of `XDISP-P2.1`, unblock `XDISP-P0.1`, or start
`XDISP-P0.2`.
