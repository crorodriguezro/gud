# E1-T03 Production Receive Architecture Decision

## Purpose

Select the Raspberry Pi FunctionFS receive architecture that E1-T04 will turn
into the long-lived production path. The decision must preserve the qualified
12,800-byte actual-payload envelope, keep EP0 responsive, make accepted I/O
ownership explicit, and retain a controlled rollback to the previously
qualified blocking receiver.

## Decision

Use **one exact native Linux-AIO receive, armed behind GUD
`STATUS_ON_SET`, as the production default**.

For each valid `SET_BUFFER`, the Pi validates the request, enters its receive
guard, submits exactly one AIO read for the declared wire length, and reports
`GUD_STATUS_OK` only after `io_submit()` confirms that the request was
accepted. The event loop continues servicing EP0 while it harvests the one
matching completion nonblockingly. Only an exact completion may return the
receive guards to `Idle` and enter decompression, framebuffer copy, and
presentation.

Keep the initialized-endpoint-file blocking receiver as an **explicit startup
rollback mode** through E1-T06. It is not an automatic retry or per-transaction
fallback. Blocking mode advertises no `STATUS_ON_SET`; exact-AIO mode advertises
it. A process must select one coherent descriptor/receive mode before binding
the UDC and may not switch while attached.

## Dependency and evidence state

E1-T01 and E1-T02 are verified prerequisites. E1-T01 proved exact AIO followed
by normal payload processing and a controlled detach after the host's pending
cleanup statuses. E1-T02 proved two ordered transactions and an `Idle` return
between them. The current implementation remains a guarded diagnostic limited
to two transactions; this decision does not promote that binary to production.

| Criterion | Protocol-gated exact AIO | Qualified blocking receive |
| --- | --- | --- |
| Queue-before-host-send proof | Verified twice through `io_submit()`, FunctionFS, and DWC2 trace | Not available with `STATUS_ON_SET=0` |
| EP0 while awaiting payload | Event loop remains available | Event loop blocks in `read()` |
| Target-hardware breadth | Two ordered diagnostic transactions | 45 payloads in the ten-cycle reliability matrix |
| Ambiguous completion policy | Explicit `InFlight`/`Poisoned` containment | Explicit `InFlight`/`Poisoned` containment |
| Safe automatic fallback | No | No |
| Role after this decision | E1-T04 production candidate | Detached/Idle rollback and comparison |

## Why this option

### Safety and protocol ordering

The native-AIO design reproduces the reference GUD gadget's required ordering:
queue the exact bulk request before allowing the host to send the bulk payload.
The upstream Linux host and the OnePlus 4.9 backport both treat successful
`GET_STATUS` as that barrier. The E1-T02 gate proved the resulting order twice
on the target hardware:

1. validate `SET_BUFFER`;
2. enter the outer `InFlight` guard;
3. accept one exact 12,800-byte FunctionFS AIO request;
4. answer `GET_STATUS=OK`;
5. submit and complete one 12,800-byte host URB;
6. harvest one exact userspace completion;
7. process the framebuffer payload; and
8. return both guards to `Idle` before admitting the next transaction.

The earlier unsafe AIO implementation does not contradict this result. It
speculatively queued 16 requests and could arm too late; it returned an invalid
completion value and must not be restored. The selected path creates one exact
request only after a valid `SET_BUFFER` and proves acceptance before status
success.

The blocking receiver is retained because it has the broader historical
qualification inside the 12,800-byte envelope: the replacement reliability
matrix completed 45 payloads across ten frames and ten rebind/reconnect cycles.
It is not selected as the default because it cannot arm before status without
a separate thread whose readiness signal is not a kernel-queue barrier. With
`STATUS_ON_SET` enabled, entering its blocking read from `Event::Buffer` would
deadlock while the host waits for `GET_STATUS`. With the flag disabled, a hung
read also monopolizes the event loop and prevents EP0 lifecycle processing.

### Control responsiveness

Exact AIO separates submission from completion. While one request is accepted,
the main loop can answer the required status and observe later control or
lifecycle events. A second `SET_BUFFER` is rejected while the transaction owns
EP1. Suspend, disable, disconnect, shutdown, timeout, or an EP0 error after
acceptance enters containment; responsiveness does not imply that cancellation
or software teardown is safe.

The blocking rollback remains intentionally less responsive. Once it calls
`read()`, it cannot process EP0 until that read returns. This is acceptable for
rollback and comparison, but not the preferred production architecture.

### Measured overhead

The corrected E1-T02 evidence bounds the selected primitive's observed cost; it
is not a full-pipeline benchmark:

- validation-to-AIO-acceptance took approximately 0.87 ms and 0.80 ms;
- AIO-acceptance-to-the-post-arm status marker took approximately 0.94 ms and
  0.99 ms;
- both 12,800-byte receive guards returned to `Idle` in 2 ms as reported by
  userspace;
- host bulk completion itself took 380 us and 409 us; and
- the complete isolated host commands took 25.07 ms and 21.78 ms, including
  their state setup, payload, framebuffer processing, and cleanup controls.

These two transactions do not support an FPS claim or a direct performance
comparison with blocking mode. They do show that arming and the extra status
barrier have bounded millisecond-scale cost on the target pair. E1-T04 must add
cumulative, non-rate-limited timing counters, and E1-T06/E5 must decide whether
the extra status transaction affects the release SLO. Performance alone may
trigger the explicit rollback only from a safely detached `Idle` session; it
may never trigger an in-session fallback.

## Ownership and interfaces

- **Primary owner:** `gud-gadget`.
- **Host contract:** the existing `gud` 4.9 host behavior is unchanged; it
  already honors `GUD_DISPLAY_FLAG_STATUS_ON_SET`.
- **Consumer:** E2 presentation may rely on bounded completion/error behavior,
  but does not own or bypass the transport state machine.
- **Canonical roadmap:** `../../../PROJECT-ROADMAP.md`, E1-T03.
- **Decision evidence:**
  `../../../../gud-gadget/evidence/functionfs-status-on-set-e1-t02-hs-corrected-20260817T192939Z/`.
- **Prior blocking evidence:** `../../../PROJECT-STATUS.md`, the replacement
  XDISP-P0.1 ten-cycle matrix.

The receive mode crosses three interfaces that must agree before UDC bind:

| Mode | Display descriptor | EP1 receive | Intended use |
| --- | --- | --- | --- |
| `status-on-set-aio` | `STATUS_ON_SET=1` | one exact request per valid `SET_BUFFER` | production default and E1-T04 candidate |
| `blocking` | `STATUS_ON_SET=0` | initialized endpoint file, bounded blocking reads | explicit rollback/comparison only |

E1-T04 may choose the configuration name, but it must expose the selected mode
in startup logs and diagnostics. There is no `auto` mode.

Existing blocking-path runbooks and tests that require `STATUS_ON_SET=0` remain
correct for their qualified artifacts. This decision does not authorize
changing a deployed descriptor. E1-T04 must update or add a component runbook
for its distinct production candidate before any hardware transfer.

## Required invariants

1. At most one accepted bulk receive owns EP1.
2. The request size is exactly `compressed_length` when nonzero, otherwise
   exactly `length`, and never exceeds 12,800 actual bytes for v1.
3. The outer receive guard enters `InFlight` before AIO submission.
4. `GET_STATUS=OK` is impossible until one exact request is accepted.
5. Submission acceptance and transfer completion remain separate states and
   counters.
6. A rejected submission may return to `Idle` only when no request was
   accepted; its status is an error and the host sends no bulk payload.
7. After acceptance, only the matching exact completion may return to `Idle`.
8. Short, zero, oversized, invalid, missing, or cross-associated completion
   enters `Poisoned` and cannot reach framebuffer processing.
9. A second `SET_BUFFER` while any receive/admission guard is active returns
   `BUSY` and queues no request.
10. EP0 remains serviced while AIO is pending; polling must be bounded and must
    not busy-spin.
11. Non-buffer SET requests retain their existing status semantics because the
    descriptor flag applies to every successful SET.
12. No automatic cancellation, retry through blocking I/O, descriptor change,
    UDC unbind, service restart, or software teardown occurs from `InFlight` or
    `Poisoned`.
13. Decompression, copy, scanout, and payload-complete accounting happen only
    after the receive guards have safely returned to `Idle`.
14. Sequence IDs correlate SET validation, AIO submission, status, host bulk,
    completion, processing, and final state without reuse during a process
    lifetime.

## In scope for E1-T04

- Remove the one- and two-transaction diagnostic admission limits without
  weakening single-owner admission.
- Replace diagnostic cleanup-status assumptions with normal long-lived GUD
  control handling.
- Make exact AIO the ordinary descriptor and receive behavior in the selected
  production mode.
- Add cumulative counters for accepted, completed, rejected, timed out,
  poisoned, busy, and currently in-flight transactions, plus arm/status/receive
  timing.
- Add offline state-machine, descriptor-coherence, overlap, completion, and
  shutdown-order tests.
- Package a reproducible production candidate and a distinct blocking rollback
  configuration/artifact.

## Out of scope

- Proving sustained multi-frame operation, disconnect safety, suspend behavior,
  or soak reliability; those are E1-T04 through E1-T06 acceptance gates.
- Raising or explaining the 12,800-byte actual-payload ceiling.
- Automatically cancelling accepted FunctionFS/DWC2 I/O.
- Multiple simultaneous AIO requests, queue depth greater than one, speculative
  prearming, or receiving a payload in chunks.
- Host-driver changes, compression-policy selection, damage planning, Mir
  presentation, or performance-SLO selection.
- Upstreaming or replacing the vendored FunctionFS/AIO extension; that is
  E1-T08 after the production behavior is stable.

## Stop and containment conditions

Stop offline progression if descriptor flags and receive mode can disagree, if
an accepted request can be dropped by a Rust error path, or if tests cannot
distinguish rejected submission from accepted-but-incomplete I/O.

For hardware work, do not send payload if the wrong binary, descriptor mode,
payload cap, pixel format, speed, maxpacket, host module, service policy, or
receive state is observed. After accepted I/O, any timeout, invalid completion,
unexpected overlap, lifecycle event, kernel warning, DWC2 anomaly, or missing
correlation marker enters containment. Preserve logs and use the established
physical disconnect/power-cycle procedure; do not stop, unbind, reboot, or
switch to blocking mode in software from `InFlight` or `Poisoned`.

## Rollback

Rollback is deliberate. First physically detach and prove both receive guards
are `Idle`; only then perform a controlled service stop and change artifacts:

1. preserve logs and record why exact AIO was rejected as the production path;
2. install or select the hash-qualified blocking artifact/configuration;
3. verify startup reports `blocking` and the descriptor reports
   `STATUS_ON_SET=0` before binding; and
4. rerun the applicable blocking-path qualification gate within the unchanged
   12,800-byte envelope.

Never reuse the exact-AIO descriptor with blocking receive, and never retry a
possibly accepted AIO transaction through blocking I/O. Retain the rollback
through E1-T06; removing it later requires a separate evidence-backed decision.

## Acceptance and definition of done

E1-T03 is **verified** when this decision is accepted and linked from the
roadmap and component backlog. No new hardware run is required: E1-T01 and
E1-T02 provide the decision evidence, while the qualified blocking matrix
provides the rollback evidence.

This decision does not verify E1-T04 or production transport. E1-T04 starts
with source still constrained by a two-transaction diagnostic guard and is done
only after its separately declared long-lived multi-frame gate passes.
