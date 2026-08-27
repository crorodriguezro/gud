# XDISP-P2.1 Dynamic Physical Mode Matching Plan

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

**Goal:** Prefer direct native scanout for an exact committed physical timing
while preserving the existing full-screen scaled fallback for the
OnePlus/fixed-panel and other non-exact modes.

**Architecture:** Refactor the Pi's physical DRM resources into stable boxed
slots with separate active/baseline/candidate roles. Expose generation-tagged
GUD state-check and state-commit notifications, preallocate maximum fallback
shadow capacity before USB bind, prepare a candidate during state-check, keep
old and target mappings alive through one bounded CRTC switch on commit, cache
same/failed-route decisions, and retain the working scaled fallback on any
no-match or failure.

**Tech stack:** Rust `gud-gadget` and `gud-drm`, Linux DRM dumb buffers and
legacy CRTC/page-flip APIs, FunctionFS, Raspberry Pi VC4 HDMI, OnePlus Linux
4.9 diagnostic GUD host, and controlled laptop GUD tests.

## Preconditions

- Read
  `../specs/2026-07-26-xdisp-p2-1-dynamic-mode-matching-design.md`.
- Use
  `../../../../gud-gadget/docs/XDISP-P2.1-DYNAMIC-MODE-MATCHING-TEST.md`
  as the component test and rollback procedure.
- Keep `XDISP-P0.1` blocked, `XDISP-P0.2` unstarted, and `XDISP-P2.1` in
  progress.
- Do not modify either kernel, normal `/home/phablet/gud.ko`, or Mir.
- Do not deploy an uncommitted binary.
- Preserve unrelated untracked hardware tools and evidence.

## Task 1: Expose checked and committed state transitions

**Files:**

- Modify: `gud-gadget/gadget/src/lib.rs`
- Modify: `gud-gadget/drm/src/main.rs`
- Modify: `gud-gadget/viewerd/src/main.rs`
- Tests: existing `gadget/src/lib.rs` test module

- [ ] Add an owned `DisplayStateSnapshot` containing connector, format, the
  complete `DisplayMode`, and a monotonically increasing pending-state
  generation. The counter is process-lifetime monotonic and is never reset or
  reused across protocol/lifecycle resets.
- [ ] Replace raw advertised-mode equality with normalized full-timing
  membership that masks flags with
  `GUD_DISPLAY_MODE_FLAG_USER_MASK=0x000033ff` and excludes the separate
  preferred advertisement bit.
- [ ] Add the protocol constant `GUD_DISPLAY_MODE_FLAG_USER_MASK=0x000033ff`
  in `gadget/src/lib.rs` with membership tests matching the host contract.
- [ ] Emit `StateChecked` only after existing wire/catalog validation stores a
  pending state.
- [ ] Make a successful commit return the promoted state and emit
  `StateCommitted`.
- [ ] Emit one composite protocol-reset/lifecycle event carrying the
  invalidated generation and reason whenever an invalid replacement check,
  failed/no-pending commit, Bind, Enable, Suspend, Resume, or
  Disable/disconnect clears pending protocol state. This preserves the
  one-`Option<Event>` return contract.
- [ ] Update both exhaustive consumers in `drm/src/main.rs` and
  `viewerd/src/main.rs` in the same commit; viewerd may explicitly ignore
  state-check/commit notifications while the composite Disable/disconnect
  reason must retain its existing disconnect behavior.
- [ ] Replace each consumer's broad “any non-disconnect event sets
  `had_host_session`” logic with explicit activity classification. Local
  Bind/Enable/Suspend/Resume/lifecycle invalidations do not mark a session;
  genuine host control traffic, including invalid CHECK/COMMIT, does.
- [ ] Preserve current status latching, controller/display enable gating, and
  buffer validation.
- [ ] Keep `GUD_DISPLAY_FLAG_STATUS_ON_SET` disabled.
- [ ] Add a descriptor contract test proving
  `GUD_DISPLAY_FLAG_STATUS_ON_SET == 0`.
- [ ] Add tests for invalid check after a valid check, commit without pending
  state, successful check/commit generation ordering, replacement of pending
  state, repeated same-state commits, and candidate invalidation on every
  Bind/Enable/Suspend/Resume/Disable reset.
- [ ] Test that Bind/Enable while detached cannot set `had_host_session` or
  trigger a detached restart loop, while host-originated invalidation and
  Disable/disconnect retain their current session/exit semantics in both
  consumers.
- [ ] Emit structured session/activity state so a detached operator can prove
  whether the current service instance has ever hosted a connection.
- [ ] Compile/check the complete workspace or at minimum all dependent
  `gud-gadget`, `gud-drm`, and `gud-viewerd` targets before this commit.
- [ ] Keep all invalid connector/format/timing requests in unit tests; never
  send deliberately invalid state to the hardware gadget.

**Exit condition:** the event contract exposes mode transitions without
changing framebuffer behavior or the existing control/bulk sequence.

**Commit:** `XDISP-P2.1 expose GUD mode transition events`

## Task 2: Encapsulate physical scanout ownership without behavior change

**Files:**

- Modify: `gud-gadget/drm/src/main.rs`
- Tests: `drm/src/main.rs` test module or a focused new DRM-state module

- [ ] Introduce `ScanoutAllocation` owning mode, two dumb buffers, framebuffer
  handles, pitch, and a persistent front-buffer index. It must not implement
  `Clone` or `Copy`.
- [ ] Introduce a `ScanoutPool` with up to three stable boxed allocation slots
  and separate active/baseline/candidate role indices; never move a borrowed
  allocation to change its role.
- [ ] Introduce scoped `MappedActive` and `MappedTransition` views. The latter
  holds old-active and fully mapped target views simultaneously, with each
  front index borrowed from its stable allocation.
- [ ] Use split borrows over slot storage and role metadata. Same-active
  checks, same-state commits, failed-route cache hits, and dynamic-disabled
  events must remain mapped no-ops.
- [ ] Move waiting-screen render, dirty, page-flip/set-CRTC fallback, dump, and
  cleanup operations behind that owner.
- [ ] Introduce `ScanoutManager` with the stable pool, a retained deterministic
  baseline role, at most one candidate role, generation-keyed pending plan,
  failed-route cache, current logical route, and counters.
- [ ] Put allocation, mapping, framebuffer creation, CRTC selection, and
  cleanup behind a narrow backend boundary and provide a deterministic mock
  that can fail each operation independently.
- [ ] Preserve current fixed startup selection and scaled/native decision when
  dynamic mode matching is disabled.
- [ ] Add an explicit idempotent release path: drop mappings, remove each DRM
  framebuffer, then destroy each dumb buffer. Implement reverse-order cleanup
  for every partially constructed allocation; ordinary Rust drop and final FD
  close are not the replacement-time cleanup mechanism.
- [ ] Add deterministic failure-injection seams around allocation, mapping,
  framebuffer creation, CRTC switch, dirty, and explicit release.
- [ ] Test single release, replacement of a pending candidate, active-resource
  retention on failure, persistent front index across role changes,
  disabled/same-mode zero-remap behavior, exact remove/destroy counts, every
  partial-construction failure, simultaneous old/target mapping, and 100
  repeated replacements without resource-count growth.
- [ ] Compute/log a checked conservative logical-pixel estimate from three
  two-buffer slots plus
  `max(advertised_width * advertised_height * 2)` shadow bytes. Label it an
  estimate, not a future DRM-allocation bound.
- [ ] Record returned pitch and mapping length when each slot is actually
  created; maintain checked current/observed-peak live bytes and release a
  candidate on accounting overflow. Test 1080p, 4K, duplicate-role, oversized
  mapping, and arithmetic-overflow cases; do not claim future pitches are
  known before bind.

**Exit condition:** all current tests and the fixed native/scaled behavior
remain unchanged with the new owner.

**Commit:** `XDISP-P2.1 own physical scanout resources`

## Task 3: Build a complete-timing route catalog

**Files:**

- Modify: `gud-gadget/drm/src/main.rs` or the focused DRM-state module
- Tests: matching unit tests

- [ ] Define a `ModeKey` containing connector, clock, active/sync/total fields,
  and `raw_flags & GUD_DISPLAY_MODE_FLAG_USER_MASK`.
- [ ] Use the centralized `GUD_DISPLAY_MODE_FLAG_USER_MASK`; apply it to
  advertised flags and route identity. Add preferred only after masking and
  exclude it from identity.
- [ ] Canonicalize/deduplicate physical entries that differ only outside the
  user mask, retaining one deterministic real DRM `Mode` for `set_crtc`.
- [ ] Convert every physical connector mode to one exact route.
- [ ] Preserve current derived portrait modes as `ScaledFallback`.
- [ ] Keep one deterministic advertised preference independent of physical
  runtime selection.
- [ ] Select the preferred mode by the chosen physical catalog entry/index,
  not by a potentially duplicated mode name; assert exactly one preferred
  advertisement bit.
- [ ] Test identical dimensions with different clocks/totals, polarity
  differences, preferred-bit independence, unsupported/3D flag masking,
  host-masked CHECK echo, deduplication, exact physical match, and synthetic
  fallback.

**Exit condition:** route selection is pure, deterministic, and fully covered
without touching DRM hardware.

**Commit:** `XDISP-P2.1 classify exact and scaled display modes`

## Task 4: Add the test-only dynamic policy

**Files:**

- Modify: `gud-gadget/drm/src/main.rs`
- Add:
  `gud-gadget/systemd/test-only/50-xdisp-p2.1-dynamic-mode-match.conf`
- Add:
  `gud-gadget/systemd/test-only/60-xdisp-p2.1-laptop-max-12800.conf`
- Tests: environment parsing and policy tests

- [ ] Add exact parser behavior for `GUD_TEST_DYNAMIC_MODE_MATCH=1`; reject all
  other values.
- [ ] Reject simultaneous dynamic matching and `GUD_TEST_OUTPUT_MODE`.
- [ ] Keep absence of both controls identical to current normal behavior.
- [ ] Log an explicit test-only startup warning and selected deterministic
  fallback timing.
- [ ] Add a drop-in containing only the dynamic enable and existing required
  diagnostics; do not change descriptor compression, maximum buffer, or
  FunctionFS read defaults.
- [ ] Add a separate laptop-only `60-*` drop-in containing only
  `GUD_TEST_MAX_BUFFER_SIZE=12800`. It must not set
  `GUD_TEST_COMPRESSION`; the old compression-disabled `30-*` gate is not used
  by this plan. Record the `60-*` hash before staging.
- [ ] Log the exact serialized descriptor fields at startup, including
  `flags=0`, compression, and maximum buffer size.

**Exit condition:** the experimental path cannot silently become normal
behavior and has a one-file rollback control.

**Commit:** `XDISP-P2.1 add test-only dynamic mode policy`

## Task 5: Prepare candidates on state-check

**Files:**

- Modify: `gud-gadget/drm/src/main.rs`
- Tests: scanout-manager state tests

- [ ] When dynamic mode matching is enabled, compute the largest checked
  advertised RGB565 source size and fallibly allocate/fully size one maximum
  shadow before UDC bind. Fail startup on overflow/allocation failure.
- [ ] Preserve committed shadow dimensions/content through every CHECK and
  invalidation. Add `ShadowRasterIdentity(width, height, format)` and a
  content-valid bit; timing-only changes do not alter pixel-layout identity,
  and only COMMIT or direct-route invalidation changes the state.
- [ ] On `StateChecked`, resolve the route before the next control event while
  keeping the current active mappings usable.
- [ ] Use this lookup order before allocation: active route, prepared
  same-timing candidate, failed-route cache keyed by logical+current physical
  mode, new exact candidate, scaled fallback.
- [ ] Create exactly one generation-keyed `PendingPlan`:
  `ExactActiveNoOp`, `ExactCandidate(slot)`,
  `ScaledBaseline { switch_required }`, or
  `ScaledCurrentAfterFailure(reason)`.
- [ ] Treat an already active timing as an exact no-op, a prepared timing as
  no-new-allocation/rekey, and a failed-cache hit as a fallback plan with no
  allocation or modeset.
- [ ] When a new valid CHECK repeats the prepared timing, rekey that candidate
  to the new generation without reallocating and make the old generation
  ineligible to activate it.
- [ ] Allocate and temporarily map at most one different exact candidate while
  retaining the mapped live scanout.
- [ ] Initialize both candidate buffers to deterministic black, flush as
  supported, set persistent front index zero, and unmap before staging it so a
  successful pre-buffer modeset never exposes stale allocation contents.
- [ ] On allocation/map/framebuffer failure, retain the working scanout,
  record `ScaledCurrentAfterFailure` in the pending plan, and increment a
  reason-specific counter. Release partial candidate resources and insert the
  separate `(logical,current-physical)` failed-cache entry; no shadow
  allocation is allowed after protocol ACK.
- [ ] Drop a replaced/uncommitted candidate exactly once.
- [ ] Record `mode_prepare_ms` and fail offline/hardware gates if preparation
  exceeds the provisional 1,000-ms budget. Use one attempt with no sleep or
  retry.
- [ ] Record state-check control latency, candidate preparation time, and the
  later commit control latency separately. Require no host control timeout;
  preparation occurs after state-check has already been acknowledged.

**Exit condition:** no state-check changes the active CRTC, active mapping, or
committed shadow; every accepted state already has non-fallible shadow
capacity and one deterministic pending plan.

**Commit:** `XDISP-P2.1 prepare exact scanout candidates`

## Task 6: Switch once on commit and preserve fallback

**Files:**

- Modify: `gud-gadget/drm/src/main.rs`
- Tests: scanout-manager transition/failure tests

- [ ] On `StateCommitted`, activate the prepared route before processing any
  later `Buffer`.
- [ ] Consume only the generation-matched `PendingPlan`; do not classify or
  allocate on COMMIT.
- [ ] If the plan is missing, stale-generation, or snapshot-mismatched, drop
  and release any stale candidate, retain the mapped current scanout, activate
  the preallocated shadow for the committed snapshot, and select
  `scaled-current-after-failure(plan-mismatch)`. Never leave the committed
  protocol state without a bulk-safe route.
- [ ] For a switch, map the complete target while old active mappings remain
  alive. On success, keep target mappings as the render view and update stable
  role indices; never switch first and remap afterward.
- [ ] Use exactly one `set_crtc` attempt and no sleep/retry for active-session
  switching.
- [ ] Promote candidate ownership only after successful CRTC selection.
- [ ] Cache same-mode state so ten repeated commits cause no additional
  allocation or modeset.
- [ ] For no-match, preparation failure, or switch failure, select the
  scaled fallback without changing protocol committed state.
- [ ] Use one coherent switch policy: exact preparation failure performs no
  modeset; exact switch failure performs no recovery modeset; both retain the
  current known-good scanout and use the preallocated
  `scaled-current-after-failure` shadow.
- [ ] Explicitly release a prepared candidate after a failed switch because it
  never became active: drop target mappings, consume/invalidate its candidate
  and PendingPlan identity, remove framebuffer objects, and destroy dumb
  buffers exactly once. Then insert or retain a separate
  `(logical_mode, current_physical_mode)` failed-cache entry.
- [ ] For a valid non-exact route, make one attempt to return to the retained
  deterministic baseline. On failure, issue no second modeset, retain the
  still-live current scanout, and use `scaled-current-after-failure`.
- [ ] Select scaling/direct copy from the committed route, not width/height
  inequality, so an equal-size/different-timing failure still scales and logs
  correctly.
- [ ] Cache a failed exact route so unchanged state recommits do not repeatedly
  allocate or modeset. CHECK must consult this cache before candidate
  creation. A different logical commit alone does not clear it; permit a new
  attempt only after the current physical timing successfully changes or a
  fresh service session clears the cache.
- [ ] Treat `EBUSY` from an outstanding scaled page flip as switch failure:
  retain active/baseline resources, release an unselected candidate, do not
  retry/sleep, and retire a replaced old exact allocation only after a
  successful `set_crtc`.
- [ ] On a same-identity scaled recommit, preserve accumulated shadow pixels.
  This includes same-resolution/different-timing scaled transitions. On
  geometry/format change, zero the active region once. Mark shadow invalid on
  entry to/direct updates in a direct route, so a later direct-to-scaled
  failure/transition zeros once even when dimensions match. Never change
  capacity.
- [ ] Do not modeset on suspend, resume, disconnect, display enable, or inside
  a bulk receive.
- [ ] Record old/new timing, reason, `mode_switch_ms`, counters, framebuffer
  size, pitch, map/unmap counts, and explicit framebuffer-remove/dumb-destroy
  counts.
- [ ] Treat any observed active switch above 250 ms as a hardware-gate
  failure; never add the startup retry loop to hide it.

**Exit condition:** exact routes switch once and copy directly; every
target-map/switch failure continues with the still-mapped old scanout and
preallocated shadow, and every non-exact route produces a correctly fitted
image.

**Commit:** `XDISP-P2.1 switch exact modes on committed state`

## Task 7: Complete offline verification

**Files:**

- Add: `gud/backport-4.9/tests/decode-gud-usbmon-descriptor.py`
- Add: `gud/backport-4.9/tests/test-gud-usbmon-descriptor-contract.sh`
- Add: `gud/backport-4.9/tests/gud-kms-mode-sequence.c`
- Add: `gud/backport-4.9/tests/test-gud-kms-mode-sequence-contract.sh`

- [ ] Add a standard-library-only parser that finds the
  `GUD_REQ_GET_DESCRIPTOR` control-IN response in a snaplen-sufficient usbmon
  text capture and emits magic, version, flags, compression, maximum buffer,
  and dimension limits as stable JSON.
- [ ] Test valid flags-zero, forbidden status-on-set, truncated data, wrong
  request, and missing-completion fixtures. The parser must exit nonzero unless
  exactly one usable served descriptor is selected or an explicit occurrence
  is requested.
- [ ] Implement `gud-kms-mode-sequence`: enumerate complete GUD timings,
  select modes by a unique complete-timing selector, create matching RGB565
  framebuffers, submit deterministic full-frame content, and emit
  machine-readable geometry/coverage/byte totals.
- [ ] Define the stable CLI used by the runbook:
  `--device`, `--list-modes`, `--mode-key`, `--frames`, `--pattern row-id`,
  and `--json`. `--mode-key` contains every normalized timing field and must
  reject zero or multiple matches.
- [ ] Test runner parsing, unique selection, framebuffer sizing, row coverage,
  byte accounting, and error paths offline.
- [ ] Build/hash the runner for the controlled x86-64 laptop before Gate 0 and
  for any later intended host; record the exact Gate 0 one-frame command.
- [ ] Run `cargo fmt --all -- --check`.
- [ ] Run serialized `gud-gadget` and `gud-drm` tests in the known working
  toolchain.
- [ ] Run workspace/dependent checks so the `gud-drm` and `gud-viewerd`
  exhaustive event consumers compile together.
- [ ] Run `cargo check` and focused `clippy` where the current tree supports
  it without unrelated cleanup.
- [ ] Build the AArch64 release artifact with the recorded Fedora cross-linker
  overrides.
- [ ] Run mode/state/failure tests repeatedly enough to detect leaked
  framebuffer ownership.
- [ ] Use the deterministic DRM mock to cover allocation, mapping, framebuffer
  creation, `set_crtc`, and cleanup failures, including a valid committed mode
  whose internal physical switch fails while scaling remains usable.
- [ ] Prove maximum-shadow overflow/allocation failure prevents UDC bind;
  CHECK/invalidation preserves committed shadow content; same-scaled identity
  preserves partial rectangles, including a same-size/different-timing scaled
  transition; geometry/format change and direct-to-scaled entry zero once; and
  all COMMIT shadow changes allocate nothing.
- [ ] Prove active mappings remain usable across candidate preparation,
  target-remap failure, and `set_crtc` failure. After success, prove the
  already mapped target continues as the render view with no post-switch mmap.
- [ ] Prove failed-route cache lookup occurs before allocation and a
  generation-matched `PendingPlan` is the normal COMMIT input. Test missing,
  stale-generation, and snapshot-mismatched plans use the allocation-free
  current-scanout fallback and release stale candidates.
- [ ] Prove failed candidate/PendingPlan identity is consumed separately from
  the `(logical,current-physical)` failed-cache entry; logical-only changes do
  not permit retry, while successful physical change/fresh service does.
- [ ] Test logical memory estimates separately from actual returned
  pitch/mapping-length current/peak accounting, including oversized mapping
  and accounting-overflow cleanup.
- [ ] Cover scaled presentation with a pending/recent page flip followed by an
  exact commit and prove `EBUSY` retains all working resources.
- [ ] Optionally add `vkms` coverage; VC4 hardware remains authoritative.
- [ ] Commit all source before calculating the artifact SHA-256.
- [ ] Commit the `gud` descriptor helper/tests separately from `gud-gadget`
  source, including the mode-sequence runner/contract test, and record both
  repository commits.
- [ ] Record source commit, binary hash, toolchain, and test logs.

**Stop condition:** do not deploy if status-on-set was enabled, fallback tests
fail, candidate resource counts grow, mode identity ignores timing fields, or
the artifact is not tied to a clean commit.

## Task 8: Close the fixed-override session and stage safely

- [ ] Keep the Pi-OnePlus data cable physically detached.
- [ ] Prove the phone logged disconnect and has no GUD DRM card.
- [ ] Prove the Pi's last receive returned to `Idle`, followed by `Suspend`,
  with no `Poisoned` state or kernel fault.
- [ ] Perform one controlled stop; require UDC unbound/not-attached, exit zero,
  and no DWC2/vc4 fault.
- [ ] Remove only the fixed
  `40-xdisp-p2.1-native-1280x720.conf` override.
- [ ] Preserve the known `4556800` binary with SHA-256
  `05bbc2284f38bcd0152bcf462cecd009fa94b342e42e399204347a3fb53a8984`.
- [ ] Stage the committed dynamic artifact and test-only `50-*` drop-in by
  explicit name and hash.
- [ ] Start detached and prove normal initial physical mode, clean service
  state, expected environment, and healthy UDC before reconnecting.
- [ ] Define/test the never-attached safe-stop branch: physical cable absent,
  structured `had_host_session=false`, receive `Idle`, UDC never configured in
  this instance, and clean service/kernel logs. This branch does not require a
  nonexistent host disconnect or `Suspend`; after any enumeration, the full
  detach boundary remains mandatory.
- [ ] While detached, prove from the startup structured log and serialized
  descriptor unit artifact that intended flags are zero and record the active
  compression/maximum/drop-in inventory.
- [ ] Start a snaplen-sufficient host usbmon capture before attachment. After
  fresh enumeration, run
  `decode-gud-usbmon-descriptor.py` and prove the actually served
  `GUD_DISPLAY_FLAG_STATUS_ON_SET == 0` before any KMS mode or buffer runner is
  allowed.
- [ ] Stop and retain the descriptor capture after approval, then start a
  second full-session usbmon capture before the KMS runner and keep it through
  the final bulk completion.

**Failure rule:** a missing `Idle`, hung receive, or `Poisoned` state prohibits
stop/restart/reboot. Collect read-only evidence, detach physically, and use the
existing containment recovery.

## Task 9: Run the exact-mode functional gates

### Gate 0 — capped laptop preflight

- [ ] Put the laptop into a headless/quiesced graphics session before
  attachment. After enumeration, prove no compositor/display manager has
  opened the new GUD DRM/render node and no unsolicited state/buffer request
  occurred.
- [ ] Install only
  `60-xdisp-p2.1-laptop-max-12800.conf` for the 12,800-byte descriptor cap
  through the component runbook's never-attached safe-stop branch for this
  first connection. Verify its hash and prove default LZ4 remains enabled;
  never reuse the compression-disabled `30-*` gate. On any later install, use
  the complete physical-detach, host-card-removal, Pi `Idle`/`Suspend`
  boundary.
- [ ] Require fresh high-speed USB enumeration and prove status-on-set remains
  clear.
- [ ] Starting at physical 1920x1080, select the real 1280x720 full timing and
  send one complete RGB565 frame.
- [ ] Require one dynamic physical switch, direct-copy presentation, complete
  rows 0--719/1,843,200-byte source coverage, actual bulk URBs no larger than
  12,800 bytes, complete transfer/receive correlation, a clean physical
  detach, and safe service stop.
- [ ] After clean Gate 0 detach, prove final `Idle`/`Suspend`, stop safely,
  remove the laptop `60-*` descriptor gate, start detached, and verify the
  intended normal LZ4 configuration. After fresh OnePlus enumeration, decode
  that normal descriptor before any state or buffer request.
- [ ] Use the single post-frame safe stop to remove `60-*`; do not restart the
  capped service merely to stop it again.

Do not expose the new VC4 resource-replacement path to OnePlus until Gate 0
passes.

### Gate A — one exact frame

- [ ] Start with normal 1920x1080 physical scanout and no fixed output-mode
  override.
- [ ] Record the separately preserved OnePlus adaptive-LZ4 module SHA-256 and
  build marker, prove it alone is loaded, and recheck that normal
  `/home/phablet/gud.ko` still has its preserved hash.
- [ ] Require fresh high-speed enumeration and decode the descriptor to prove
  status-on-set is clear.
- [ ] Commit 1280x720 and require one full-timing 1920x1080-to-1280x720 switch
  before the first `Buffer`.
- [ ] Send one deterministic complete frame with every actual payload at or
  below 12,800 bytes.
- [ ] Prove source rows 0--719 are covered exactly once and account for
  exactly 1,843,200 uncompressed RGB565 source bytes.
- [ ] Define each capped payload from the host bulk URB submit/completion
  length, not `SET_BUFFER.length` or the Pi read size. Correlate each
  `SET_BUFFER` with exactly one bulk completion and one complete Pi receive.
- [ ] Require `scaled=false`, no scaled back-buffer presentation, and matching
  `InFlight`/`Idle` for every receive.
- [ ] Record state-check and commit control latencies independently and
  require no host control timeout.

### Gate B — repeated same mode

- [ ] Run the preserved ten-frame 1280x720 clip.
- [ ] Require ten logical commits but no additional physical switch or
  candidate allocation after the first exact activation.
- [ ] Prove ten complete frames, each covering source rows 0--719 exactly once
  and accounting for 1,843,200 uncompressed RGB565 source bytes.
- [ ] Require steady average commit at or below the provisional 60-ms target
  and maximum commit at or below the provisional 85-ms target; record p95 and
  the cold switch separately.
- [ ] Require no host `-110`, short/impossible read, DWC2/vc4 fault, Oops,
  pstore record, watchdog event, or Pi reboot.

**Stop condition:** do not proceed to fallback testing if an exact mode scales,
more than one switch occurs, any payload exceeds 12,800 bytes, or the
preparation exceeds 1,000 ms, or the transition exceeds 250 ms.

## Task 10: Prove fallback and transition behavior

- [ ] Install only the max-only laptop `60-*` 12,800-byte descriptor override
  after physical detach, host-card removal, Pi `Idle` then `Suspend`, and one
  safe service stop. Start detached, prove default LZ4, and require fresh
  high-speed enumeration plus served-descriptor decode.
- [ ] Keep the laptop graphics stack quiesced, prove no external opener of the
  GUD node, and start a full-session usbmon capture before the sequence runner.
- [ ] Use the already committed and hash-verified
  `gud-kms-mode-sequence` runner from Task 7. Do not use the hard-coded
  1280x720 animator unchanged.
- [ ] Choose and record complete timings for exact physical mode A, one safely
  advertised non-exact/synthetic mode, and exact physical mode B. Require A
  and B to differ from the baseline so the route is an actual exact-A switch,
  baseline return, then exact-B switch. Prefer distinct A/B timings when the
  catalog supplies two, but repeating A as B is valid and tests recreation
  after the baseline transition.
- [ ] Submit one complete matching RGB565 frame in A, one complete synthetic
  frame through baseline scaling, and one complete frame in B. For every
  frame, cover each source row exactly once, account for exactly
  `width * height * 2` source bytes, cap actual host bulk URBs at 12,800 bytes,
  and correlate each `SET_BUFFER` with one successful bulk completion and one
  Pi `InFlight -> Idle`.
- [ ] Commit B without an artificial settling sleep after the last synthetic
  scaled presentation. Record `EBUSY`, require no retry, and fail promotion
  unless B becomes the active exact timing.
- [ ] Require direct copy only for exact modes.
- [ ] Require the existing aspect-fit scaler, centering, and borders for the
  non-exact mode.
- [ ] Repeat the route sequence enough to prove candidate/framebuffer counts
  remain bounded.
- [ ] Inject or simulate exact allocation/switch failure and prove the working
  scaled output remains available. Use a valid committed mode plus the
  internal deterministic DRM failure seam; never send a protocol-invalid
  hardware request.
- [ ] Record HDMI blank interval and first-frame latency separately from
  steady commit latency.
- [ ] After Gate C, physically detach and prove the receive-idle boundary,
  safely stop, remove `60-*`, start detached, and verify the restored normal
  LZ4 descriptor before any later connection.

**Stop condition:** if no safe host can exercise exact-to-fallback-to-exact,
leave the feature experimental and `XDISP-P2.1` in progress.

## Task 11: Lifecycle and unpaced performance gates

- [ ] Run the lifecycle restart and unpaced performance gates only with the
  preserved OnePlus adaptive-LZ4 diagnostic module and normal Pi descriptor;
  no laptop cap drop-in may remain.
- [ ] After successful payloads, physically detach USB first.
- [ ] Prove phone card removal, final Pi `Idle`, later `Suspend`, and clean
  kernels.
- [ ] Perform one controlled service stop/start and verify ordered
  UDC/FunctionFS/DRM teardown.
- [ ] Repeat one exact frame after fresh enumeration.
- [ ] Run at least three fresh-Pi-boot exact-to-fallback-to-exact mini-cycles
  only with the capped laptop and max-only `60-*` descriptor. Enclose every
  boot/cycle group in the full detached/Idle/Suspend install and restoration
  boundaries, and end each cycle with physical detach, final
  `Idle`/`Suspend`, and clean lifecycle evidence.
- [ ] For every boot cycle record the old/new `/proc/sys/kernel/random/boot_id`
  values, require they differ, collect `journalctl -b -1` service and kernel
  logs plus pstore/watchdog state, and fail on an unexpected reboot, Oops, or
  missing previous-boot evidence.
- [ ] Run an unpaced version of the preserved native clip.
- [ ] Record the exact unpaced command, `target-fps=0`, frame count, input
  asset SHA-256, and host/Pi CPU-sampling command and interval.
- [ ] Record achieved FPS, commit p50/p95/p99/max, host/Pi CPU, first-switch
  latency, first-frame latency, payload/rectangle/compression counters,
  dropped frames, and visible tearing.
- [ ] Preserve raw logs under ignored evidence storage with a concise
  `RESULTS.md` and checksum manifest.

## Task 12: Promotion or rollback decision

- [ ] Compare dynamic exact behavior with the fixed-native baseline.
- [ ] If every exact, fallback, transition, lifecycle, and safety gate passes,
  prepare a separate review/commit making exact-match routing normal.
- [ ] Remove the temporary enable control only in that later promotion commit.
- [ ] If any mandatory gate fails, restore source `4556800`'s known binary
  while detached and receive-idle; remove every temporary `30-*`, fixed-native
  `40-*`, dynamic `50-*`, and laptop-cap `60-*` gate; retain exactly the
  normal containment `10-*` and 16 KiB read-size `20-*` drop-ins; retain
  evidence; and keep the feature test-only. Reinstall `40-*` only in a
  separately approved fixed-native benchmark session.
- [ ] Update `PROJECT-STATUS.md` and `BACKLOG.md` with observed evidence, but
  do not change `XDISP-P0.1` or start `XDISP-P0.2`.

Passing this plan completes only the dynamic-mode-matching substep.
Native-frame presentation/tearing policy, adaptive compression optimization,
Mir/Lomiri measurements, and the full `XDISP-P2.1` acceptance remain
separate.
