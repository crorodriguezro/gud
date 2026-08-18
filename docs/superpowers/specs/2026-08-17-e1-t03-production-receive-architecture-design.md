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
matching completion nonblockingly. The exact completion transfers the
transaction from USB receive ownership to serialized processing ownership;
only successful completion of decompression, framebuffer copy/presentation,
and final accounting may return the transaction to `Idle`.

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
| Userspace readiness barrier | `io_submit()` accepted exactly one request; E1-T01/T02 correlate that acceptance with FunctionFS/DWC2 queue-before-host-send trace | Not available with `STATUS_ON_SET=0` |
| EP0 while awaiting payload | Event loop remains available | Event loop blocks in `read()` |
| Target-hardware breadth | Two ordered diagnostic transactions | 45 payloads in the ten-cycle reliability matrix |
| Ambiguous ownership policy | Explicit `InFlight`/`Processing`/`Poisoned` containment | Explicit `InFlight`/`Poisoned` containment |
| Safe automatic fallback | No | No |
| Role after this decision | E1-T04 production candidate | Detached/Idle rollback and comparison |

## Why this option

### Safety and protocol ordering

The native-AIO design reproduces the reference GUD gadget's required ordering:
prepare the exact bulk receive before allowing the host to send the payload.
The upstream Linux host and the OnePlus 4.9 backport both treat successful
`GET_STATUS` as that protocol barrier.

`io_submit()` acceptance is the observable userspace ownership/readiness
barrier. It does not, by itself, let userspace claim direct observation that a
DWC2 `usb_request` is physically armed. E1-T01 and E1-T02 qualify the barrier
for this target by correlating AIO acceptance with the FunctionFS and DWC2
queue-before-host-send trace. The E1-T02 gate proved this resulting order twice:

1. validate `SET_BUFFER`;
2. enter aggregate `Arming` ownership;
3. accept one exact 12,800-byte FunctionFS AIO request and enter `InFlight`;
4. answer `GET_STATUS=OK`;
5. submit and complete one 12,800-byte host URB;
6. harvest one exact userspace completion and enter `Processing`;
7. finish decompression, framebuffer copy/presentation, and transaction
   accounting; and
8. return the transaction owner to `Idle` before admitting the next
   `SET_BUFFER`.

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
lifecycle events. A second `SET_BUFFER` is rejected while the transaction is
`Arming`, `InFlight`, or `Processing`. Suspend, disable, disconnect, shutdown,
timeout, or an EP0 error after acceptance enters containment; responsiveness
does not imply that cancellation or software teardown is safe.

The blocking rollback remains intentionally less responsive. Once it calls
`read()`, it cannot process EP0 until that read returns. This is acceptable for
rollback and comparison, but not the preferred production architecture.

### Measured overhead

The corrected E1-T02 evidence bounds the selected primitive's observed cost; it
is not a full-pipeline benchmark:

- validation-to-AIO-acceptance took approximately 0.87 ms and 0.80 ms;
- AIO-acceptance-to-the-post-arm status marker took approximately 0.94 ms and
  0.99 ms;
- both 12,800-byte USB receive guards returned to their diagnostic `Idle` in
  2 ms as reported by userspace; this predates and does not satisfy the new
  aggregate `Processing -> Idle` definition;
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

## Transaction lifecycle and ownership

E1-T04 uses one aggregate, serialized transaction lifecycle:

```text
Idle
  -> Arming
  -> InFlight
  -> Processing
  -> Idle

Any ambiguous state after accepted I/O
  -> Poisoned
```

The state meanings are:

- **Idle:** no AIO request is accepted, no prior transaction owns EP1, no
  receive buffer or mutable `SET_BUFFER`/compression metadata remains owned,
  and all transaction-specific processing and final accounting are finished.
- **Arming:** one validated `SET_BUFFER` owns its mutable metadata and exact
  buffer while the process attempts `io_submit()`. No kernel-accepted I/O is
  assumed yet. A submission failure proven to have accepted zero requests
  latches a GUD error and returns `Arming -> Idle`; the host must not send bulk.
- **InFlight:** `io_submit()` accepted exactly one exact request. The process
  owns and must account for that request, buffer, metadata, and sequence until
  exactly one matching completion is harvested or the transaction enters
  containment. `GET_STATUS=OK` is permitted only in this state.
- **Processing:** the matching exact completion succeeded and USB no longer
  owns the receive buffer, but the same transaction still exclusively owns the
  payload, rectangle and compression metadata, sequence, and processing result.
  Decompression, framebuffer copy/presentation, and final accounting run
  serially in this state.
- **Poisoned:** ownership or outcome after accepted I/O is unsafe or ambiguous.
  It is terminal for normal software operation and never becomes `Idle` by
  assumption, timeout, cancellation, close, or fallback.

For v1 there is no immutable handoff object and no processing pipeline. The
same transaction remains the sole owner from validation through processing.
This intentionally leaves EP1 idle during `Processing`; simplicity and
unambiguous lifetime are more important than receive concurrency. Any future
immutable transaction object, pipelining, or concurrent transport buffering
requires measurements showing serialized operation cannot meet the release
SLO and a separate design review.

## Required invariants

1. At most one transaction owns EP1, and queue depth is exactly one.
2. At most one transaction owns mutable `SET_BUFFER` rectangle/compression
   metadata or a receive buffer.
3. A receive buffer cannot be reused, cleared, resized, or submitted again
   while its transaction is `InFlight` or `Processing`.
4. The request size is exactly `compressed_length` when nonzero, otherwise
   exactly `length`, and never exceeds 12,800 actual bytes for v1.
5. A valid `SET_BUFFER` transitions `Idle -> Arming`; only acceptance of exactly
   one request transitions `Arming -> InFlight`.
6. A rejected submission may transition `Arming -> Idle` only when the result
   proves zero requests were accepted. It latches a GUD error and the host must
   not send bulk.
7. `GET_STATUS=OK` is impossible before exact AIO acceptance and the
   `InFlight` transition.
8. Submission attempt, submission acceptance, and completion are distinct
   telemetry events and counters.
9. Only the exact matching completion for the active sequence can transition
   `InFlight -> Processing`.
10. Short, zero, oversized, wrong-sequence, missing, invalid, or
    cross-associated completion enters `Poisoned` and cannot reach processing.
11. Only successful completion of decompression, framebuffer
    copy/presentation, and final transaction accounting transitions
    `Processing -> Idle`; processing failure enters explicit containment and
    does not silently reopen admission.
12. A second `SET_BUFFER` during `Arming`, `InFlight`, or `Processing` returns
    `BUSY` and queues no request.
13. After accepted I/O, ambiguous ownership or outcome never returns to `Idle`
    by assumption.
14. EP0 remains serviced while AIO is `InFlight`; polling is bounded and does
    not busy-spin. Processing remains serialized for v1.
15. Non-buffer SET requests retain their existing status semantics because the
    descriptor flag applies to every successful SET.
16. No automatic cancellation, retry through blocking I/O, descriptor change,
    UDC unbind, service restart, or software teardown occurs from `InFlight`,
    `Processing`, or `Poisoned`.
17. Sequence IDs are unique for the process lifetime and correlate
    `SET_BUFFER` validation, submission attempt, submission acceptance, status,
    host bulk, AIO completion, payload processing, and final state.

## Current implementation gaps handed to E1-T04

The E1-T01/T02 diagnostic remains valid for its bounded gate, but its internal
states are not yet the production lifecycle:

- `ExactAioTransaction::return_idle()` and
  `BulkReceiveSession::finish_receive()` currently report `Idle` immediately
  after AIO completion, before `finish_payload()`, framebuffer copy, scanout,
  and final accounting. The diagnostic admission guard happens to remain active
  through processing, but E1-T04 must not remove that guard without replacing
  it with aggregate `Processing` ownership.
- `BulkReceiveSession::begin_receive()` currently labels the pre-`io_submit()`
  interval `InFlight`. E1-T04 must expose `Arming` separately, whether through
  one enum or coordinated guards.
- The endpoint's vendored AIO driver is constructed with the generic default
  queue length of 16. Current application logic submits only one request, but
  E1-T04 must make and verify effective production depth one rather than rely
  only on convention.
- `EndpointReceiver::try_recv()` discards the AIO `OpHandle`, and
  `try_fetch()` returns only the next buffer. Queue depth one prevents practical
  cross-association in the diagnostic, but E1-T04 telemetry and completion
  validation must preserve an explicit active transaction/completion identity.
- `u64` AIO and payload sequences currently use wrapping increment. E1-T04 must
  fail safely on exhaustion rather than reuse a process-lifetime correlation
  ID, even though exhaustion is operationally remote.
- Several processing error paths currently log and continue after the receive
  guards already report `Idle`. Under the production lifecycle they must leave
  `Processing` only through successful finalization or explicit containment.

## In scope for E1-T04

- Remove the one- and two-transaction diagnostic admission limits without
  weakening single-owner admission.
- Replace diagnostic cleanup-status assumptions with normal long-lived GUD
  control handling.
- Make exact AIO the ordinary descriptor and receive behavior in the selected
  production mode.
- Implement the aggregate `Idle -> Arming -> InFlight -> Processing -> Idle`
  owner and hold admission closed through final processing/accounting.
- Make effective AIO queue depth one and preserve a unique active transaction
  identity through completion harvesting.
- Add cumulative counters for accepted, completed, rejected, timed out,
  processing, poisoned, busy, and currently owned transactions, plus
  arm/status/receive/processing timing.
- Add offline state-machine, descriptor-coherence, overlap, completion, and
  shutdown-order tests.
- Package a reproducible production candidate and a distinct blocking rollback
  configuration/artifact.

E1-T04 also includes a minimal semantic audit of the vendored production
primitive. This is documentation and verification, not E1-T08 upstreaming or a
rewrite. The component document must trace:

```text
PixelDataEndpoint::arm_exact_payload_aio()
  -> EndpointReceiver::try_recv()
  -> Linux native AIO IOCB_CMD_PREAD / opcode::PREAD
  -> SYS_io_submit against the initialized FunctionFS ep1 OUT fd
  -> pinned iocb plus driver-owned BytesMut for the request lifetime
  -> accepted-count == 1 as the userspace ownership/readiness barrier
  -> SYS_io_getevents and nonblocking try_fetch() completion harvesting
  -> FunctionFS ffs_epfile_read_iter()/ffs_epfile_io() queue path
  -> documented suspend/disable/disconnect/close/drop expectations
```

It must state who owns the endpoint fd, iocb, buffer, transaction metadata, and
completion at every lifecycle state; what successful submission does and does
not prove; and why close, driver drop, or cancellation is not production
recovery for accepted I/O on this hardware.

## Out of scope

- Proving sustained multi-frame operation, disconnect safety, suspend behavior,
  or soak reliability; those are E1-T04 through E1-T06 acceptance gates.
- Raising or explaining the 12,800-byte actual-payload ceiling.
- Automatically cancelling accepted FunctionFS/DWC2 I/O.
- Multiple simultaneous AIO requests, queue depth greater than one, speculative
  prearming, multiple accepted `SET_BUFFER` transactions, overlapping
  framebuffer transactions, or receiving one GUD payload through independently
  managed chunks.
- Pipelining, immutable transaction handoff, or double buffering for transport
  concurrency unless a later measured SLO failure opens a separate design.
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
processing ownership failure, unexpected overlap, lifecycle event, kernel
warning, DWC2 anomaly, or missing correlation marker enters containment.
Preserve logs and use the established physical disconnect/power-cycle
procedure; do not stop, unbind, reboot, or switch to blocking mode in software
from `InFlight`, `Processing`, or `Poisoned`.

## Rollback

Rollback is deliberate and selected before UDC bind. First physically detach
and prove the aggregate transaction state is safely `Idle`; only then perform a
controlled service stop and change artifacts:

1. preserve logs and record why exact AIO was rejected as the production path;
2. install or select the hash-qualified blocking artifact/configuration;
3. verify startup reports `blocking` and the descriptor reports
   `STATUS_ON_SET=0` before binding; and
4. rerun the applicable blocking-path qualification gate within the unchanged
   12,800-byte envelope.

Never reuse the exact-AIO descriptor with blocking receive, and never retry a
possibly accepted AIO transaction through blocking I/O. Retain the rollback
through E1-T06; removing it later requires a separate evidence-backed decision.
There is no `auto` receive mode.

## Acceptance and definition of done

E1-T03 is **verified** when this decision, including the serialized processing
ownership and E1-T04 semantic-audit handoff, is accepted and linked from the
roadmap and component backlog. No new hardware run is required: E1-T01 and
E1-T02 provide the architecture evidence, while the qualified blocking matrix
provides the rollback evidence.

This decision does not verify E1-T04 or production transport. E1-T04 starts
with source still constrained by a two-transaction diagnostic guard and is done
only after its separately declared long-lived multi-frame gate passes.
