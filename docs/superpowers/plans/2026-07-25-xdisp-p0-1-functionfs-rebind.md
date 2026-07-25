# XDISP-P0.1 FunctionFS Rebind Reliability Plan

**Goal:** Remove the Pi FunctionFS first-payload stall after a gadget rebind or
OnePlus reconnect, with repeatable evidence rather than a one-off successful
frame.

**Architecture:** Keep the host `gud.ko` plus isolated KMS fill/smoke path as
the only client. Instrument and make explicit the Pi FunctionFS endpoint and
gadget lifecycle, automate the ten-cycle matrix, then repair the first failing
lifecycle boundary. The Mir external-output POC remains uninstalled and out of
scope.

**Tech stack:** OnePlus 6 Ubuntu Touch/Halium 9 Linux 4.9 GUD host module,
Raspberry Pi GUD FunctionFS gadget, Pi DRM/HDMI, SSH-collected logs.

## Preconditions

- Read `../specs/2026-07-25-xdisp-p0-1-functionfs-rebind-design.md` and the
  Pi-side `../../../../gud-gadget/docs/XDISP-P0.1-FUNCTIONFS-REBIND-TEST.md`.
- The OnePlus begins each cycle with the mandatory `1d50:614d` enumeration
  gate and a freshly discovered GUD DRM node.
- The Pi service uses a known deployed binary and has debug logs enabled.
- The normal Mir Android graphics platform is installed; do not load the POC.
- Preserve raw logs outside version control unless a selected evidence file is
  deliberately added.

## Task 1: Establish an auditable failing/passing baseline

- [ ] Run two Pi-rebind and two OnePlus-reconnect cycles with the current
  isolated KMS fill/smoke path.
- [ ] Record reset action, card node, payload size, transfer duration, UDC
  state, and the exact first error (if any) in the P0.1 evidence table.
- [ ] Capture a Pi service journal window and focused OnePlus kernel window for
  every run, including successful runs.
- [ ] Confirm the logs can distinguish descriptor publication, endpoint reader
  start, host submission, first payload receipt, and first tile present.

**Stop condition:** Do not modify lifecycle code until a failure has an
evidence pair, or four baseline cycles have all passed and the test environment
has been independently checked for reset coverage.

## Task 2: Identify the first missing or invalid lifecycle transition

- [ ] Compare a passing and failing cycle from reset through first payload.
- [ ] Determine whether failure occurs before host submission, while the Pi
  waits for endpoint data, or after data reaches Pi userspace.
- [ ] Audit FunctionFS endpoint ownership and cleanup: descriptor publication,
  endpoint open, enable, reader start, disable/cancel, close, and re-open.
- [ ] Audit gadget bind/unbind ordering relative to Pi DRM initialization; no
  host may enumerate a half-ready FunctionFS service.
- [ ] Write the identified boundary and hypothesis into the P0.1 evidence
  record before changing code.

**Decision rule:** Fix only the first failing boundary. Do not add retries,
sleep-based workarounds, or Mir changes as a substitute for a defined endpoint
lifecycle.

## Task 3: Add minimal instrumentation and a targeted repair

- [ ] Add structured, rate-limited Pi debug logging for the events required by
  the design's observability contract.
- [ ] Add or extend a source-level test where the FunctionFS/gadget lifecycle
  can be exercised without physical hardware; keep hardware timing assertions
  in the external test procedure.
- [ ] Implement the smallest repair that makes endpoint ownership, reader
  cancellation, and rebind cleanup idempotent.
- [ ] Build the Pi binary and run its existing local tests before deployment.
- [ ] Deploy only the Pi service change; keep the OnePlus kernel module and Mir
  plugin unchanged for the first verification run.

## Task 4: Verify the acceptance matrix

- [ ] Run the full ten-cycle matrix from the Pi-side procedure, without retry
  of the first fill operation.
- [ ] Require at least five Pi-rebind and five OnePlus-reconnect/reboot cycles.
- [ ] Preserve host and Pi logs for all cycles and summarize payload size/time
  and first-transfer result in one evidence table.
- [ ] Search host logs for GUD `-110` bulk/atomic failures and control-request
  timeouts; a single match fails the matrix.
- [ ] Update `PROJECT-STATUS.md` only after all acceptance criteria pass.

## Task 5: Handoff

- [ ] Document the root cause, repair, and retained evidence in
  `gud-gadget/docs/KNOWN_ISSUES.md` or a dedicated incident record.
- [ ] Mark `XDISP-P0.1` **verified** in the canonical board with an evidence
  path, not merely a statement of success.
- [ ] Only then schedule `XDISP-P0.2`, the asynchronous Mir delivery worker.

## Non-goals

- No performance tuning, HDMI mode selection, damage tracking, compression, or
  geometry debugging.
- No hard-coded `/dev/dri/card1`, device-node symlinks, or phone UI workarounds.
- No production deployment of the experimental Mir POC.
