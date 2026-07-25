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

**2026-07-25 diagnostic finding:** This boundary is now after successful host
bulk completion but before Pi userspace read completion. The OnePlus submitted
the first 64,000-byte URB and its callback reported `status=0 actual=64000`; Pi
`gud-drm` had validated the matching `SET_BUFFER` and entered its 512-byte
FunctionFS receive loop without reporting aggregate payload completion, then
the next control request timed out. The current logging does not identify
which of the 125 reads failed to complete. Preserve/recover Pi pstore and
persistent journal evidence before attempting another lifecycle repair. The
original phone module was restored after the temporary host instrumentation
run.

## 2026-07-25 userspace-first decision

Do not require a custom Raspberry Pi kernel for the first repair attempt.
Keep `XDISP-P0.1` blocked and leave the OnePlus module and Mir unchanged.
Execute the following plan in order:

1. **Contain failure:** disable automatic restart of
   `gud-userspace.service`. After any host `-110`, do not stop, unbind, or
   restart the affected instance; recover the Pi by a hard reset and collect
   the previous boot.
2. **Repair lifecycle:** in `gud-gadget` and its vendored `usb-gadget`,
   disconnect/unbind the UDC, let the FunctionFS read exit, close endpoint
   files, and release DRM last. Systemd `SIGTERM` must actively trigger this
   path instead of only setting a flag.
3. **Test `g_dma=0`:** only if the userspace repair still reproduces the
   failure, test a Raspberry Pi kernel/configuration with DWC2 DMA disabled.
4. **Harden DWC2:** only if the kernel test requires it, change the Raspberry
   Pi `drivers/usb/dwc2/` stop-timeout path.
5. **Test read sizes:** instrument individual read completions and test larger
   aligned FunctionFS receive requests in `gud-gadget` as a separate variable.
6. **Staged verification:** pass three safe mini-cycles before restarting the
   official ten-cycle matrix from cycle 1, retaining evidence through the
   `gud` procedures.

Actual execution order is now recorded separately from the original numbered
proposal: Steps 1, 2, and 5 are implemented and hardware-exercised; the modern
laptop transfer-shape control inserted below is next; Step 3 (`g_dma=0`) is
conditional on that result; Step 4 remains conditional; and Step 6 is blocked.
The unchecked baseline tasks above are historical planning text and must not
be interpreted as authorization to repeat an unchanged failing payload.

Step 1 is containment, not verification. It must not change the status or
authorize `XDISP-P0.2`.

**Step 1 completed (2026-07-25):** installed the tracked
`10-xdisp-p0.1-containment.conf` drop-in on the Pi and ran only
`systemctl daemon-reload`. The loaded unit reports `Restart=no`,
`ActiveState=active`, `SubState=running`, unchanged `MainPID=849`, and
unchanged `NRestarts=1`. The UDC remained `not attached`; the service was not
stopped or restarted.

**Step 2 implementation completed (2026-07-25):** `gud-drm` now keeps an
explicit gadget registration owner, unbinds the UDC to cancel a blocked
FunctionFS read, removes the gadget, and drops the remaining FunctionFS
endpoint owners before returning into DRM cleanup. The vendored
`usb-gadget` removal path unbinds before FunctionFS `pre_removal`.
Shutdown/unbind is idempotent, a failed unbind remains retryable, and the
`ctrlc` termination feature routes systemd `SIGTERM` through the same path.

The shutdown controller is covered for shutdown-before-bind,
shutdown-after-bind, and first-unbind-failure cases. All 9 `gud-drm` tests,
all 30 `gud-gadget` tests when serialized, and the vendored library test
target pass. The release artifact SHA-256 is
`5aae726497cb6ea7b21336fae49528a9b69e6592d6871bc1068e11f0aebf7a06`.
It was staged on the Pi as
`/home/cristian/gud-drm.xdisp-p0.1-step2-new`; dependency resolution succeeds.
After explicit authorization, the prior binary was preserved as
`/home/cristian/gud-drm.pre-xdisp-p0.1-step2-7c99034` and the repaired binary
was activated.

A controlled no-payload restart passed the ordered teardown path: SIGTERM
unbound the UDC, the main loop exited, gadget/FunctionFS removal completed,
endpoint owners were dropped before DRM release, and the service restarted
with no DWC2 timeout or Pi Oops. The retained runtime evidence is
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-runtime-gate-2026-07-25/`.

**Step 2 post-payload runtime result (2026-07-25): passed once.** After Pi and
OnePlus recovery, forcing the documented OnePlus host-mode node produced the
mandatory dynamic gate
`FOUND: /sys/bus/usb/devices/1-1.2` for `1d50:614d`. The unchanged normal
OnePlus module created `/dev/dri/card1`. One isolated 1280x720 RGB565
submission returned `PAYLOAD_RC=0`; the Pi completed 28 64,000-byte tiles and
one 51,200-byte tile. A controlled service restart then unbound the UDC,
removed FunctionFS, dropped endpoint owners before DRM release, and started a
new process. The Pi stayed in the same boot with no DWC2 stop timeout, Oops,
`5a5a`, or paging fault. The phone re-enumerated `1d50:614d`, recreated
`card1`, and logged no GUD `-110`.

The stopping process reported status 1 because the signal-induced detach was
also classified as a restart request; the explicit systemd restart itself
succeeded and the replacement is active with `Restart=no`, `NRestarts=0`, and
`Result=success`. Treat that as a service-status/reporting defect, not a
kernel-cleanup failure. The rebooted Pi also needed one manual service start
after its boot start exhausted `set_crtc` retries with `EACCES`; that separate
DRM-startup race does not invalidate the post-payload lifecycle result.
Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-post-payload-runtime-2026-07-25/`.
This single safe run establishes a positive Step 2 result only; it does not
mark `XDISP-P0.1` verified or authorize later steps.

**Intentional-shutdown status follow-up (2026-07-25): locally passed,
hardware blocked.** A focused control-flow change makes SIGTERM take
precedence over a queued detach restart request, so successful intentional
teardown returns status 0 while unexpected detach still requests a restart.
All 11 focused `gud-drm` tests pass. Artifact
`7053d5b1cf3f7cc94da776a97f64d3471382df9b8f40b00b511eb9ef3bcf1e12`
is installed on the Pi; the `5aae726...` base is
preserved at `/home/cristian/gud-drm.pre-xdisp-p0.1-exit0-5aae726`. The first
hardware attempt did not reach SIGTERM: after the mandatory gate and normal
host reprobe, the first payload reproduced request `0x60`/atomic-update
`-110`; Pi SSH became unreachable, so the Step 1 containment rule prohibited
stop/restart. The later two-boot recovery evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-exit0-hardware-blocked-2026-07-25/`.

Recovery after two Pi restarts retained that failed payload as boot `-2`.
The service accepted the first 64,000-byte `SET_BUFFER` and entered its
512-byte FunctionFS loop without an aggregate completion. The old logging
cannot identify which of its 125 reads stalled. Fifteen seconds later the
kernel Oopsed in `__kmalloc_noprof` while `sshd-session` loaded an ELF binary,
with `f81ff81ff81ff81f` in allocator state and a bad RSS-counter report. No
SIGTERM, DWC2 endpoint-stop timeout, FunctionFS teardown, or DRM release ran.
This moves the stop condition ahead of cleanup: do not retry the existing
512-byte receive loop. The next userspace experiment must be the planned
aligned read-size test; only use DWC2 DMA isolation if that remains necessary.
The current service is failed after the separate boot-time `set_crtc`/`EACCES`
race, and the UDC is `not attached`; leave it stopped.

**Step 5 implementation completed locally (2026-07-25); first hardware gate
failed.**
The `gud-gadget` blocking path still reuses the endpoint file opened during
FunctionFS initialization and does not return to native AIO. It now validates
`GUD_FFS_READ_SIZE` as a 512-byte-aligned ceiling from 4,096 through 65,536
bytes and queues exactly `min(remaining_payload, ceiling)` for each syscall.
The tracked first-test setting is 16,384 bytes, so a normal 64,000-byte tile
uses `[16384, 16384, 16384, 14848]`; the exact tail is never padded. The caller
atomically changes `Idle -> InFlight` before blocking, and `SIGTERM` may claim
teardown only from `Idle`. A short, zero, failed, or more-than-one-second
completion does not queue another read and permanently poisons that process
when it returns. One second is a conservative threshold relative to the
observed 5--11 ms receives, not a userspace timeout; a hung read remains
`InFlight`. In-flight and poisoned processes refuse further USB/control
processing and require a physical power cycle, hardware reset, or watchdog
reset rather than graceful teardown.

Structured logs identify the payload sequence, read index, requested/result
bytes, remaining bytes, and per-read duration. Aggregate `frame_stats`
separates `read_calls` from `usb_packets_est`. Focused tests cover the
16 KiB first-test strategy, a later 64 KiB one-read A/B case, the 51,200-byte
final tile, ceiling splitting, an unaligned exact tail, short/zero completions,
endpoint I/O errors, caller-level receive poisoning, in-flight shutdown
exclusion, late-completion poisoning, post-read isolation, and idle shutdown
claiming. All 39 `gud-gadget` and 17 `gud-drm` tests pass. The local AArch64
artifact SHA-256 is
`0c5961daf65a543101bb1727c2c6909a19b5a48ae398cadd94c4c6ebd044b662`.

The Fedora verification host needs its installed cross-toolchain name in
place of the stale linker/sysroot configured in `.cargo/config.toml`. The
passing test and release commands use:

```bash
env CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-redhat-linux-gcc \
    CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS='-C link-arg=-static-libgcc' \
    'CC_aarch64-unknown-linux-gnu=aarch64-redhat-linux-gcc' \
    'CFLAGS_aarch64-unknown-linux-gnu=' \
    cargo test -p gud-gadget -p gud-drm -- --test-threads=1
env CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-redhat-linux-gcc \
    CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS='-C link-arg=-static-libgcc' \
    'CC_aarch64-unknown-linux-gnu=aarch64-redhat-linux-gcc' \
    'CFLAGS_aarch64-unknown-linux-gnu=' \
    cargo build --release -p gud-drm
```

**Step 5 first 16 KiB hardware result (2026-07-25): failed.** The documented
artifact and drop-ins were hash-verified and deployed while the service was
inactive. After manual start and a fresh high-speed OnePlus probe, the first
16,384-byte read returned `18446744073709045760` (signed `-505856`) instead of
16,384. DWC2 debugfs showed `g_dma=1`, `g_dma_desc=0`, and ep1 OUT
`DOEPTSIZ=0x7f800` (522,240) against a loaded `0x4000` bytes. The DWC2
buffer-DMA size calculation therefore underflowed exactly to the returned
`0xfff84800`.

The service entered `Poisoned`, refused further processing, and safely avoided
teardown. The Pi remained reachable and showed no new Oops or allocator/DWC2
kernel warning. The OnePlus utility printed `PAYLOAD_RC=0`, but its kernel
logged fresh bulk/atomic `-110` and control request `0x64` `-110`, so tool
return status is not transfer proof. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step5-16k-first-hardware-2026-07-25T2214BST/`.

Keep `XDISP-P0.1` blocked. That failed boot was poisoned and was subsequently
physically recovered. Any future poisoned boot permits no stop/restart or
read-size retry and requires physical/hardware/watchdog reset. Do not run the
old 64 KiB A/B, return to 512 bytes, or begin the acceptance matrix.

**Initial read-count and DMA-isolation decision (2026-07-25):** retain the
configurable four-read 16 KiB implementation, but do not accept it as the
normal default until the changed-variable reliability gates pass. Do not
restore the historical 125-read loop as a fix. DMA isolation was initially
next; the later laptop result and revision below supersede that ordering.

Read-only inspection of the exact Pi established
`6.12.47+rpt-rpi-v8`, `CONFIG_USB_DWC2=y`, no `g_dma` module parameter, no
`g_dma` option in the installed `dwc2` overlay, and a read-only debugfs
parameter report. There is no supported `cmdline.txt`, `config.txt`, module
reload, or debugfs switch for this test. Use a separately named Pi test kernel
with `p->g_dma = false` in the Broadcom callback in
`drivers/usb/dwc2/params.c`, preserving the stock kernel/modules as rollback.
This does not modify the OnePlus kernel. The exact safety and verification
procedure is maintained in
`../../../../gud-gadget/docs/XDISP-P0.1-FUNCTIONFS-REBIND-TEST.md`.

**Revision after the modern-laptop control (2026-07-25):** do not compile the
Pi `g_dma=0` kernel first. The laptop proved 16 KiB reads and `g_dma=1` can
complete sustained traffic, and the user noticed no visible-performance
difference from the old 512-byte read granularity. Performance benchmarking is
out of scope here and is deferred to `XDISP-P2.1`.

The userspace-only transfer-shape controls are implemented with normal
LZ4/natural-size defaults unchanged. The final compressed laptop control
reached 4,786 payloads and stopped cleanly after physical detach.

Gate A then advertised `max_buffer_size=64000` with LZ4. Usbmon captured 5,671
matched SET_BUFFER/bulk pairs with no error, including 15 complete 1280x720
frames. All actual compressed bulk URBs were 131--12,600 bytes. Gate A passed.

Gate B disabled compression on a fresh boot. KDE restored 1920x1080, producing
a first 1920x16/61,440-byte rectangle. The SET_BUFFER completed and the
upstream GUD/xHCI host submitted one 61,440-byte bulk URB. It completed
cancelled (`-104`) after 18,432 bytes and 3.033326 seconds while the host logged
`-110`. The Pi's first 16,384-byte read returned signed `-505856`, exactly
matching `16384 - 522240` from ep1 OUT `DOEPTSIZ=0x7f800`. Gate B therefore
reproduced the DWC2 residual failure without the OnePlus backport.

Containment parked the service without teardown. Evidence was copied before a
physical reset; the previous-boot journal was then retained, both pstore
captures were empty, watchdog bootstatus was zero, and there was no DWC2 stop
timeout or Oops. The failed drop-in is quarantined and the recovered service
is disabled/inactive.

**Revision after Gate B (2026-07-25):** do not repeat the unchanged
61,440-byte failure and do not compile `g_dma=0` yet. Establish an uncompressed
safe-length ceiling with the existing descriptor control:

1. Gate C: fresh boot, `g_dma=1`, 16 KiB reads, compression disabled, and
   `max_buffer_size=15360`. This is packet aligned and exactly 4 RGB565 rows at
   1920 width or 6 rows at 1280 width, so it is one URB/read in either mode.
2. Require a complete frame with exact usbmon/read matching, physical detach,
   an `Idle` receive session, and a controlled exit-zero stop. Any anomaly is
   terminal for that boot.
3. If Gate C passes, repeat on separate fresh boots at 30,720, 46,080, then
   53,760 bytes. Stop on the first anomaly; the known 61,440-byte failure is
   already the upper bound.
4. Treat the largest clean value as a userspace mitigation candidate. Verify it
   first on the laptop, then with the unchanged normal OnePlus module for one
   full frame and a safe restart.
5. Reconsider one isolated `g_dma=0` Pi test kernel only if Gate C fails or the
   ladder yields no usable safe value.

No gate starts the mini-cycles or changes `XDISP-P0.1` from **blocked**. One
complete OnePlus frame and a safe controlled stop/start remain prerequisites
for three mini-cycles and the ten-cycle matrix.

The previously outstanding post-payload crash evidence is preserved under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-predeploy-2026-07-25/`.
It shows the old shutdown path hit both DWC2 endpoint-stop timeouts immediately
before allocator state containing `0x5a` caused ten Oopses. This is evidence
for the repair boundary, not verification of the new binary. `XDISP-P0.1`
remains blocked.

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
