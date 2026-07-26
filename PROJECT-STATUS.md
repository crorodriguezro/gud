# Project Status

## Objective

Backport the host-side Generic USB Display (GUD) DRM driver to the OnePlus 6 Ubuntu Touch / Halium 9 Linux 4.9 kernel as a standalone `gud.ko` module. The phone is the USB host; the Raspberry Pi Zero 2 W is the GUD USB gadget and HDMI endpoint.

The MVP is one DRM/KMS external output using RGB565, full-frame USB transfers,
and a static 1280x720 test pattern. It must not require a replacement kernel
image or DRM core changes unless a documented blocker proves them unavoidable.

## Current State

- Branch: `linux-4.9-backport`
- Latest integrated implementation commit: `15f2a6e` (includes Ticket 5
  first-pixels commit `99b353f`).
- Ticket 1 build environment is implemented and its practical ABI gate is surpassed: the Ticket 2 driver built for and loaded on the phone's exact kernel ABI.
- Ticket 2 USB probe and disconnect implementation is complete and hardware-validated.
- Ticket 3, Linux 4.9 GEM and dumb-buffer support, is implemented and build-validated against the exact target kernel tree. Its userspace validation is deferred to Ticket 4 because no DRM node exists yet.
- Ticket 4 DRM/KMS registration was hardware-validated with the earlier
  XRGB8888 build. Ticket 5 RGB565 transfer and Pi presentation are now
  hardware-validated end to end on the OnePlus 6 and Raspberry Pi Zero 2 W.

## Cross-repository external-display priorities

`PROJECT-STATUS.md` is the canonical board for work that crosses the host
driver, Pi gadget, and Mir platform repositories.  Detailed implementation
notes stay in the owning repository, but every cross-repository item keeps the
same ID here and there.  This prevents a successful one-off hardware result
from being mistaken for a reliable end-user feature.

Use these states consistently:

- **planned**: scoped but not started;
- **in progress**: active implementation or investigation;
- **blocked**: waiting on a named dependency or reproducible evidence;
- **verified**: acceptance test passed with retained evidence; and
- **rolled back**: a useful experiment that is not enabled in the normal phone
  image.

| ID | Priority | Owner | State | Required acceptance evidence |
| --- | --- | --- | --- | --- |
| `XDISP-P0.1` | Make the Pi FunctionFS first bulk transfer reliable after a gadget rebind or phone reconnect. See `docs/superpowers/specs/2026-07-25-xdisp-p0-1-functionfs-rebind-design.md` and its implementation plan. | `gud-gadget` | blocked | Ten fresh rebind/reconnect cycles complete all 29 tiles of the 1,843,200-byte RGB565 frame, including the first 64,000-byte tile, without host `-110` timeout. |
| `XDISP-P0.2` | Move GUD presentation off Mir's compositor commit path; retain only the newest pending frame on overload. | `mir-android2-platform-gud` | planned | Phone input and internal display remain responsive while the Pi is slow, absent, or returns an I/O error. |
| `XDISP-P0.3` | Discover the live GUD DRM card and handle remove/re-add; do not hard-code `card1` or use a symlink. | `mir-android2-platform-gud`, `gud` | planned | Reconnect succeeds when the card number changes, with no manual node changes or compositor restart. |
| `XDISP-P1.1` | Validate external-output geometry and Lomiri placement, including the intermittent narrow/cropped image. | `mir-android2-platform-gud`, `gud-gadget` | planned | A 1280x720 extended desktop fills the selected output correctly across repeated enable/disable cycles. |
| `XDISP-P2.1` | Improve usable performance with damage-aware updates, mode matching, measurement, and optional compression. | all three | planned | Recorded end-to-end FPS, latency, CPU use, and frame-drop behavior at the chosen mode. |

`XDISP-P0.1` diagnostic evidence (2026-07-25) narrows the active failure to
the Pi: the OnePlus submitted and successfully completed the first 64,000-byte
bulk URB, while Pi `gud-drm` entered its 512-byte FunctionFS receive loop without
reporting aggregate payload completion. The next `SET_BUFFER` control request
then timed out. DWC2 subsequently timed out while stopping the OUT endpoint,
and the Pi Oopsed with `0x5a` corruption visible in allocator state. Raw logs
are ignored under `backport-4.9/env/local/evidence/`.

**Decision (2026-07-25):** take the userspace-only repair path first. Disable
automatic Pi service restart, permit controlled UDC/FunctionFS shutdown only
from an atomically idle receive session, guard in-flight reads from signal
teardown, then test larger receive requests through staged mini-cycles. Defer
a custom Pi kernel (`g_dma=0` or a DWC2 patch) unless those changes still
reproduce host `-110`, DWC2 endpoint-stop timeouts, or a Pi Oops. Keep the
OnePlus module and Mir unchanged. The item remains **blocked**; do not start
`XDISP-P0.2` or mark verification.

Containment step 1 is deployed on the Pi as
`10-xdisp-p0.1-containment.conf` (SHA-256
`e1d23477b23e3aff1647262f0f9637129d07e968a1336910c393c71db3879430`).
After `daemon-reload`, systemd reported `Restart=no`; the service remained
active with the same PID and restart count. No service restart, UDC rebind, or
payload test was used to verify this containment setting.

Lifecycle-repair step 2 is implementation-complete in `gud-gadget` and its
vendored `usb-gadget`: systemd `SIGTERM` now actively unbinds the UDC,
failed unbinds remain retryable, gadget removal closes FunctionFS after
unbind, and endpoint owners are dropped before DRM cleanup. The affected
tests pass (9 `gud-drm`, 30 serialized `gud-gadget`, and the vendored library
target). The release artifact SHA-256 is
`5aae726497cb6ea7b21336fae49528a9b69e6592d6871bc1068e11f0aebf7a06`.
It was staged on the Pi as
`/home/cristian/gud-drm.xdisp-p0.1-step2-new` and activated after explicit
authorization. The prior `7c990347...` binary is retained at
`/home/cristian/gud-drm.pre-xdisp-p0.1-step2-7c99034`.

The no-payload runtime teardown passed: SIGTERM unbound the UDC, FunctionFS
removal and endpoint-owner closure completed before DRM release, the service
restarted successfully, and the Pi kernel logged no DWC2 timeout or Oops.
Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-runtime-gate-2026-07-25/`.

The first post-payload Step 2 runtime test also passed. After forcing the
documented OnePlus host-mode node, the dynamic gate found `1d50:614d` at
`/sys/bus/usb/devices/1-1.2`, and the unchanged normal module created
`/dev/dri/card1`. One isolated 1280x720 RGB565 submission returned
`PAYLOAD_RC=0`; all 29 tiles completed. The subsequent controlled Pi service
restart completed UDC unbind, FunctionFS removal, and endpoint-owner release
before DRM release. The Pi retained its boot ID with no DWC2 timeout, Oops,
`5a5a`, or paging fault. The phone re-enumerated the gadget, recreated
`card1`, and logged no GUD `-110`. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-post-payload-runtime-2026-07-25/`.

Two non-fatal follow-ups were exposed: the stopping process briefly reported
status 1 after treating the shutdown-induced detach as a restart request, and
the repaired service's automatic Pi boot start exhausted `set_crtc` retries
with `EACCES` before a later manual start succeeded. Neither reproduced the
unsafe cleanup, but both remain separate service-lifecycle/reporting issues.
This one positive run determines the Step 2 result; it does not satisfy the
ten-cycle acceptance matrix. `XDISP-P0.1` therefore remains **blocked**, and
no later step or `XDISP-P0.2` is authorized by this result.

The outstanding post-payload previous-boot journal is now retained under
`backport-4.9/env/local/evidence/xdisp-p0.1-step2-predeploy-2026-07-25/`.
On the old shutdown path, DWC2 logged both endpoint-stop timeouts immediately
before `0x5a` allocator corruption caused repeated Oopses. The Step 2 base
binary passed the single controlled post-payload restart
described above. The old crash evidence remains the comparison baseline; the
single passing run does not change the **blocked** status or authorize
`XDISP-P0.2`.

A later attempt to verify the small intentional-shutdown exit-status follow-up
reproduced the intermittent transport failure before SIGTERM could be tested.
That follow-up artifact, SHA-256
`7053d5b1cf3f7cc94da776a97f64d3471382df9b8f40b00b511eb9ef3bcf1e12`,
is installed on the Pi; the `5aae726...` base is
retained at `/home/cristian/gud-drm.pre-xdisp-p0.1-exit0-5aae726`.
The unchanged OnePlus module re-probed `1d50:614d`; the isolated KMS tool
reported `PAYLOAD_RC=0`, but the host kernel then logged request `0x60` and
atomic-update `-110`, followed by request `0x64` `-110`. Pi SSH became
unreachable while the gadget and host DRM node remained present. In accordance
with containment, the affected service was not stopped or restarted. The
exit-status change is outside the payload path and passes focused unit tests,
but its hardware result remains undetermined because SIGTERM was never
reached. Recovery journals are now collected. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-exit0-hardware-blocked-2026-07-25/`.

Recovery after two manual Pi restarts retained the failed payload as boot
`-2`. The Pi accepted the first 64,000-byte `SET_BUFFER` at 20:40:22 and
entered the 512-byte FunctionFS loop without an aggregate completion. The old
logging cannot identify which of its 125 reads stalled. At 20:40:37 the kernel
Oopsed in `__kmalloc_noprof` while `sshd-session` was loading an ELF binary,
with `f81ff81ff81ff81f` in allocator state and a subsequent bad RSS-counter
report. No SIGTERM, DWC2 endpoint-stop timeout, FunctionFS teardown, or DRM
release occurred. This establishes that the remaining allocator corruption
can occur during the active blocked payload, independently of the repaired
cleanup ordering. Pstore was empty, but the persistent service and kernel
journals are retained in the evidence directory above.

Step 5 was deployed on the inactive Pi with the expected artifact SHA-256
`0c5961daf65a543101bb1727c2c6909a19b5a48ae398cadd94c4c6ebd044b662`;
the prior `7053d5b...` binary is preserved at
`/home/cristian/gud-drm.pre-xdisp-p0.1-step5-7053d5b`. Auto-start remains
disabled. Manual start acquired DRM, bound the UDC, and reached a fresh
high-speed OnePlus probe after the documented phone `device -> host` role
reset.

The first 16,384-byte hardware payload failed on read 1 of the first
64,000-byte tile. FunctionFS returned `18446744073709045760`, signed
`-505856`, after 476 microseconds. DWC2 debugfs showed `g_dma=1`,
`g_dma_desc=0`, and ep1 OUT `DOEPTSIZ=0x0007f800` (522,240 bytes remaining)
against 16,384 bytes loaded. The buffer-DMA completion calculation therefore
underflowed exactly: `0x4000 - 0x7f800 = 0xfff84800 = -505856 signed`.
FunctionFS propagated that impossible `req->actual` value to userspace.

The service correctly changed `InFlight -> Poisoned`, refused another read,
parked, and did not enter the known-risk teardown path. The Pi remained
reachable with the same boot ID, the UDC remained configured at high speed,
and no new Oops, allocator warning, DWC2 endpoint-stop timeout, or pstore
record appeared. The host utility printed `PAYLOAD_RC=0`, but the fresh host
kernel log recorded bulk and atomic-update `-110`, followed by request `0x64`
`-110`; userspace return status is therefore not standalone transfer proof.

Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-step5-16k-first-hardware-2026-07-25T2214BST/`.
That failed Pi boot was poisoned and was subsequently physically recovered.
For any future poisoned instance, do not stop, restart, reboot, shut down, or
retry another read size; recover by physical power cycle, hardware reset, or
watchdog reset. Keep `XDISP-P0.1` **blocked** and do not start Step 5
mini-cycles, the ten-cycle matrix, or `XDISP-P0.2` yet.

**Read-count/DMA decision (2026-07-25):** keep the configurable four-read
16 KiB strategy in `gud-gadget`, but do not accept it as the normal default
until the changed-variable reliability gates pass. Do not restore the 125-read
512-byte loop as a fix; that path has an active-payload stall and
allocator-corruption result. The initial next action was DMA isolation; the
later laptop result and revised next steps below supersede that ordering.

The installed Pi kernel is `6.12.47+rpt-rpi-v8` with
`CONFIG_USB_DWC2=y`. It has no `g_dma` module parameter, its `dwc2` overlay
does not expose one, and its debugfs `params` file is read-only. Therefore
`g_dma=0` requires a separately named patched/prebuilt Pi test kernel; it
cannot be enabled through `cmdline.txt`, `config.txt`, module reload, or
debugfs on the current image. The minimal source experiment sets
`p->g_dma = false` in the Broadcom callback in
`drivers/usb/dwc2/params.c`. Preserve the stock kernel and modules as the
rollback path. This is a Raspberry Pi kernel-only diagnostic; it does not
change or rebuild the OnePlus kernel, `gud.ko`, or Mir. The executable
procedure and source references are in
`../gud-gadget/docs/XDISP-P0.1-FUNCTIONFS-REBIND-TEST.md`.

A non-disruptive modern-laptop control materially narrows that decision. After
physical recovery, the same Pi binary ran at high speed with `g_dma=1` against
the laptop's upstream Linux 7.0 GUD/xHCI host and appeared in KDE as a
1920x1080 extended monitor. The Pi completed 953 compressed payloads,
432,606,856 transfer bytes, and 27,053 of 27,053 FunctionFS reads with zero
short reads, poisoned transitions, or impossible lengths. Receive processing
averaged 14.86 ms and total Pi processing averaged 32.48 ms; neither side
logged a transport or kernel failure.

This proves that 16 KiB reads and DWC2 buffer DMA are not universally broken
on this Pi. The OnePlus failure is conditional on host, transfer shape,
timing, or their interaction. The laptop used upstream GUD/xHCI and compressed
full-screen payloads, whereas the OnePlus uses the Linux 4.9 backport and
uncompressed 64,000-byte tiles. Keep four reads as the candidate; do not
restore the 125-read loop. `g_dma=0` remains an OnePlus-specific isolation
test, not a general laptop requirement. The control is not an OnePlus
acceptance cycle and does not change the **blocked** status. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-16k-live-2026-07-25T2256BST/`.

**Performance decision (2026-07-25):** the user noticed no visible-performance
difference between the previous 512-byte userspace reads and the 16 KiB
strategy. This was not a controlled A/B and creates no performance claim.
Keep 16 KiB for reduced request churn and diagnostics. Defer an identical-
workload comparison—including presented FPS, latency, dropped frames, host/Pi
CPU, and USB throughput—to `XDISP-P2.1` after reliability is stable. Do not
restore 512-byte reads during `XDISP-P0.1` for benchmarking.

**Laptop Gate A/B/C result (2026-07-25):** the userspace-only controls produced
a decisive split. Gate A used LZ4, `max_buffer_size=64000`, 16 KiB reads, and
`g_dma=1`. Usbmon retained 5,671 matched SET_BUFFER/bulk pairs with no control
or bulk error, including 15 complete RGB565 1280x720 frames. Every actual
compressed bulk URB was 131--12,600 bytes, below one 16 KiB read. The
post-detach controlled stop exited zero and the Pi kernel remained clean.

Gate B disabled compression with the same advertised maximum. KDE initially
restored 1920x1080, so the first complete-row rectangle was 1920x16, or
61,440 bytes. The SET_BUFFER completed and upstream GUD/xHCI submitted one
61,440-byte bulk URB. It was cancelled 3.033326 seconds later with `-104`
after 18,432 bytes, while the host logged framebuffer flush `-110`. The Pi's
first 16,384-byte read returned unsigned `18446744073709045760`, signed
`-505856`; ep1 OUT reported `DOEPTSIZ=0x0007f800`, and
`16384 - 522240 = -505856` exactly.

Containment parked the Gate B service without teardown. After evidence
capture, physical recovery retained the prior boot journal; pre-reset and
post-reset pstore were empty, watchdog bootstatus was zero, and there was no
DWC2 endpoint-stop timeout or kernel Oops. The failed drop-in is quarantined,
the Pi service is disabled/inactive, and the UDC is `not attached`. Evidence
is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-a-2026-07-25T1754COT/`
and
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-b-2026-07-25T1810COT/`.

Gate C kept compression disabled and reduced the first payload to
1920x4/15,360 bytes. Usbmon records the complete host bulk URB succeeding
after 774 microseconds, but the matching Pi FunctionFS read never returned.
The DWC2 request remained in flight with zero bytes done after physical USB
detach. Containment again prevented teardown, and recovery found no DWC2 stop
timeout, Oops, watchdog reset, or pstore record. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-c-2026-07-25T1834COT/`.

**Laptop Gate D result (2026-07-25):** Gate D passed one complete 1920x1080
frame as 360 uncompressed 11,520-byte transfers, then six complete 1280x720
frames as 1,080 uncompressed 10,240-byte transfers. Usbmon matched all 1,440
bulk submits/completions with status zero and full length. The 10,240-byte
shape is exactly 20 maxpackets, but all submits had transfer flags zero and
there was no zero-length bulk URB. The Pi recorded 1,440 returns to `Idle`;
post-detach controlled shutdown exited zero with full gadget/DRM teardown and
no host/Pi kernel anomaly. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-d-2026-07-25T1905COT/`.

Exact maxpacket termination is therefore not a sufficient trigger. Cancel the
proposed OnePlus `URB_ZERO_PACKET` diagnostic. The useful aligned boundary is
now a 10,240-byte pass versus 15,360-byte failure; size and/or intermittent
DWC2 state still matters.

**Laptop Gate E first result (2026-07-25):** Gate E passed one known-clean
1920 frame followed by six complete target 1280x720 frames. Usbmon matched
1,224/1,224 transfers; 864 were aligned 1280x5/12,800-byte target transfers.
Every completion had status zero/full length, and the Pi returned to `Idle`
1,224 times. Physical detach and controlled stop were clean, with no host/Pi
kernel anomaly. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-e-2026-07-25T1919COT/`.

The observed aligned boundary is now 12,800 bytes clean versus 15,360 bytes
failed. This is a ceiling candidate, not verification or root-cause proof.

**Gate E qualification result (2026-07-25):** two additional fresh-boot
repeats passed the identical configuration. Each of the three boots completed
six target 1280x720 frames, 864 aligned 12,800-byte transfers, and an exit-zero
controlled stop. Across the qualification, all 2,592 target transfers
completed without a host URB error, length mismatch, Pi read anomaly, poisoned
session, or kernel fault. The table is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-e-qualification-2026-07-25.md`.

The 12,800-byte descriptor ceiling is not a usable normal configuration. At
1280x720 it requires 144 SET_BUFFER operations per frame, and the user
observed roughly one visible frame every five seconds.

**Laptop Gate F result (2026-07-25):** Gate F restored normal LZ4 and the
natural maximum buffer while changing only the internal FunctionFS read
ceiling to 12,800 bytes. Its first full-screen compressed payload was 16,274
bytes. The first 12,800-byte read returned unsigned
`18446744073709042176`, signed `-509440`, exactly matching
`12800 - 522240` from ep1 OUT `DOEPTSIZ=0x7f800`. Usbmon recorded the
16,274-byte submit and cancellation 3.052803 seconds later with status `-104`
and 14,848 bytes actual; the laptop logged framebuffer-flush `-110`.
Containment parked the poisoned service without teardown, and physical
recovery found no DWC2 stop timeout, Oops, watchdog reset, or pstore record.
Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-laptop-gate-f-2026-07-25T2039COT/`.

Gate E passed when the complete host transfer and userspace read were both
12,800 bytes. Gate F proves that a 12,800-byte FunctionFS prefix read does not
safely consume a larger host transfer under `g_dma=1`. The descriptor and
internal-read ceilings therefore cannot be decoupled as a performance
workaround. Do not reinstall Gate F unchanged or proceed to the OnePlus gate.

**Gate F decision at the time:** isolate DWC2 buffer DMA with the separately named Pi
`g_dma=0` test kernel documented in
`../gud-gadget/docs/XDISP-P0.1-FUNCTIONFS-REBIND-TEST.md`. The installed Pi
kernel cannot enable this through `cmdline.txt`, an overlay, module reload, or
debugfs; it requires a separately built/prebuilt Pi test kernel while
preserving the stock rollback kernel. This does not modify the OnePlus kernel,
`gud.ko`, or Mir. Keep `XDISP-P0.1` blocked and do not start mini-cycles, the
matrix, or `XDISP-P0.2`.

**`g_dma=0` result (2026-07-26):** the stock-preserving one-shot kernel
`6.12.47+rpt-rpi-v8-xdisp-gdma0` booted with buffer and descriptor DMA both
disabled. With the normal LZ4/natural descriptor and the 16 KiB blocking read
ceiling, its first laptop payload was a 16,274-byte compressed 1920x1080
rectangle. Usbmon recorded the upstream host submitting and successfully
completing all 16,274 bytes in 823 microseconds. Pi FunctionFS returned only
3,986 bytes from the exact 16,274-byte request after 302 microseconds. Live
DWC2 state reported `total_data=3986` and 12,288 bytes remaining in
`DOEPTSIZ`, exactly `16274 - 3986`.

Containment changed the service to `Poisoned` and refused teardown. Later host
control retries caused 17 secondary DWC2 ep0-state warnings; no OnePlus test,
mini-cycle, matrix, or service stop was attempted on that boot. This rejects
`g_dma=0` as an unchanged workaround and further localizes the failure to the
Pi DWC2/FunctionFS receive path. It does not establish a safe DWC2 patch.
Physical reset returned to stock `6.12.47+rpt-rpi-v8` with `g_dma=1`, the
service disabled/inactive, and the UDC detached. The previous boot contains no
endpoint-stop timeout, Oops, panic, or pstore record; watchdog bootstatus is
zero.
Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-pi-gdma0-laptop-normal-2026-07-25T2140COT/`.

Do not automatically start the original Step 4 kernel-hardening work. Gate E
is the only laptop-qualified userspace boundary, but its observed
one-frame-per-five-seconds cadence is not a usable product setting. The next
no-Pi-kernel design candidate is a separately preserved OnePlus diagnostic
module that adds compression-aware adaptive rectangle splitting and enforces
an actual bulk payload ceiling at or below the qualified 12,800-byte boundary.
The normal `/home/phablet/gud.ko` must remain untouched until that design is
reviewed and explicitly authorized. If that host-only path is rejected or
cannot meet the boundary, normal-performance progress requires targeted Pi
DWC2/FunctionFS kernel instrumentation and another test build. Keep
`XDISP-P0.1` blocked and do not start `XDISP-P0.2`.

The design was authorized on 2026-07-26 and is being implemented as a separate
build under `backport-4.9/variants/xdisp-lz4-12800/`. It compresses the
largest legal source rectangle before splitting, uses measured compression
size to reduce complete rows, and retains a final pre-submit `<= 12,800`
check. A private Linux-4.9-derived LZ4 compressor is linked because the
OnePlus kernel exports no LZ4 compressor. The normal build remains a separate
target and must retain its preserved SHA-256. Offline completion, staging, one
real frame, safe restart, and cadence evidence are distinct gates;
implementation alone is not a reliability result. The execution and rollback
plan is
`docs/superpowers/plans/2026-07-26-xdisp-p0-1-oneplus-adaptive-lz4.md`.

**Adaptive-LZ4 first hardware result (2026-07-26):** the separate diagnostic
module completed one 1280x720 RGB565 OnePlus frame as four contiguous LZ4
rectangles. Actual payloads were 11,260, 12,144, 12,380, and 10,204 bytes;
the maximum was 12,380 against the final 12,800-byte submission cap. All four
Pi transfers completed in one 16 KiB read, returned to `Idle`, and presented
the complete frame. The host logged no `-110`; neither kernel logged a warning
or Oops. After physical detach, the OnePlus GUD device/card disappeared. The
Pi UDC retained stale `configured` state, but a controlled post-payload
restart cleanly unbound UDC first, exited zero, started a new service instance
on the same boot, and returned to `not attached` without DWC2 stop timeouts or
the vc4 release fault.

The Pi processing totals were 208 ms across the four rectangles, an isolated
upper bound of about 4.8 full frames/s for the highly compressible color-bar
pattern rather than a sustained benchmark. The first-frame and lifecycle
prerequisites have passed; three fresh mini-cycles are next, using separate
stop/start operations after proven-idle physical detach. Do not start the
ten-cycle matrix before all three pass. `XDISP-P0.1` remains **blocked** and
`XDISP-P0.2` must not start. Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-oneplus-adaptive-frame-2026-07-26T1154COT/`.

The proof of concept verified that Lomiri can expose an independent
`DisplayPort-2` output backed by GUD. It also froze or severely slowed the
phone because it did synchronous USB work in Mir's commit path, and it has
been rolled back from the phone. The detailed POC record and its constraints
are in `../mir-android2-platform-gud/GUD-EXTERNAL-DISPLAY-POC.md`.

Read first:

- `AGENTS.md`: project constraints and verification expectations.
- `LINUX-4.9-BACKPORT.md`: architecture, MVP, and compatibility strategy.
- `BACKLOG.md`: ordered milestones; update only with evidence-backed completion.
- The cross-repository priority table above: the source of truth for the
  extended-display work spanning all three repositories.
- `docs/superpowers/CROSS-REPOSITORY-WORKFLOW.md`: placement, lifecycle, and
  evidence rules for future cross-repository specifications and plans.
- `docs/superpowers/specs/2026-07-23-oneplus6-gud-usb-probe-design.md`: Ticket 2 requirements.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-usb-probe.md`: Ticket 2 implementation and acceptance procedure.
- `docs/superpowers/plans/2026-07-23-oneplus6-gud-host-backport.md`: full driver ticket sequence.
- `docs/oneplus6-usb-host-gud-troubleshooting.md`: mandatory start-of-session USB enumeration gate, validated host-mode procedure, and Ticket 4 atomic diagnostics.

Every new phone-test session must run that troubleshooting document's
start-of-session enumeration gate before loading or testing `gud.ko`. Match the
Pi by `1d50:614d`; its sysfs path is dynamic and has appeared as both `1-1.2`
and `1-1.4`. Do not conclude that enumeration failed from one empty scan.

## Implemented

`backport-4.9/` now contains:

- Ticket 1 scripts for target capture, pinned kernel preparation, external-module build, and guarded deployment.
- `gud.ko` composed from `gud_drv.c`, not the removed no-op stub.
- Fixed USB matching for the validated Pi GUD gadget, `1d50:614d`.
- Packed GUD v1 display-descriptor definitions and explicit little-endian conversion.
- USB probe that locates bulk-out endpoint `0x01`, reads and validates the GUD display descriptor, and logs its capability limits.
- Disconnect that clears USB interface data before marking the private state unavailable and releasing it.
- Laptop-side Pi USB capture and phone-side probe deployment/evidence scripts, with hermetic shell tests.
- A Wi-Fi SSH workflow for phone testing when the Pi occupies the OnePlus 6's only USB-C port.
- A Linux 4.9-native local GEM layer with page-backed objects, lazy page pinning, cached CPU `vmap()`, 16-bpp RGB565 dumb-buffer creation, mmap-offset and fault callbacks, and explicit teardown.
- Rejection of imported dma-buf-backed objects with `-EOPNOTSUPP`; no PRIME import/export callbacks are present.
- Ticket 3 shell contract coverage and an ABI/symbol-audit workflow. Kbuild `.cmd` metadata is ignored rather than versioned.
- Ticket 4/5 DRM registration: one `DRIVER_ATOMIC` GUD card, one virtual connector, one simple display pipe, RGB565 only, and one preferred 1280x720@60 mode.
- A local GEM-backed framebuffer path that validates RGB565 layout and backing-object bounds before an atomic modeset can reference it.
- Linux 4.9 USB unplug lifetime handling through `drm_unplug_dev()`, retaining private state until the final DRM file release.
- Ticket 4 contract coverage and a libdrm phone smoke utility for dumb-buffer create/map/write/destroy plus atomic modeset validation.

Ticket 4's earlier XRGB8888 result intentionally contained no GUD
connector/EDID query, display-state request, framebuffer upload, USB bulk
transfer, workqueue, asynchronous USB transfer, or visible output. Ticket 5
adds the state and synchronous RGB565 transfer path and has now completed a
full hardware framebuffer update to the Pi display pipeline.

## Hardware Evidence

The following selected raw evidence is intentionally versioned at its existing `env/local/` paths for future debugging. Other captures, kernel trees, build artifacts, modules, and unrelated runtime logs remain ignored.

- `backport-4.9/env/local/pi-usb/identity.env`: captured Pi USB identity `1d50:614d`.
- `backport-4.9/env/local/pi-usb/lsusb-v.txt`, `usb-devices.txt`, and `device-descriptors.bin`: raw laptop-side Pi descriptor capture.
- `backport-4.9/env/local/evidence/module-metadata.txt`: `gud.ko` metadata and unresolved-symbol list.
- `backport-4.9/env/local/evidence/probe-cycle-dmesg.txt`: full phone dmesg capture for the cable cycle.
- `backport-4.9/env/local/evidence/probe-unload-dmesg.txt`: focused GUD probe/disconnect and unload record.

The module was built for and loaded on:

```text
4.9.112-g6b190d86b SMP preempt mod_unload modversions aarch64
```

The focused GUD record shows:

- Six successful probes of `1d50:614d`.
- Five cable-removal disconnects followed by successful re-probes.
- A final disconnect during `usbcore: deregistering interface driver gud`.
- Descriptor values: GUD v1, bulk-out endpoint `0x01`, maximum buffer `8294400`, width `640-1920`, height `400-1080`.

The full dmesg capture has one `WARNING: CPU` in `dwc3_send_gadget_ep_cmd` at timestamp `70261`, before `gud` registered at `70940` and before the first Pi probe at `72066`. It is an unrelated pre-test USB-gadget warning, not a GUD driver warning. The GUD test interval from driver registration through unload has no `BUG:`, `Oops`, `WARNING:`, `lockdep`, or `use-after-free` entry.

## Ticket 2 Acceptance

Ticket 2 is complete: the OnePlus 6 recognizes the Pi gadget, validates its GUD v1 descriptor, and survives five remove/reconnect cycles plus final module unload without a GUD-related failure.

Do not overstate this result: Ticket 2 proves USB enumeration and lifetime handling only. It does not expose `/dev/dri/cardX`, create a framebuffer, enumerate a display mode, or display pixels.

## Ticket 3 Acceptance

Ticket 3 is complete at the source/build boundary:

- `gud.ko` builds against the captured `4.9.112-g6b190d86b` target tree with `gud_gem_4_9.o` linked.
- The GEM and USB contract tests pass, along with the existing environment/probe test suite.
- The module's new GEM and VM dependencies are audited against the matching target build and captured phone symbols.
- The local-only GEM callbacks are ready for Ticket 4 registration: `gem_free_object_unlocked`, `dumb_create`, `dumb_map_offset`, file `mmap`, and VM operations.

No phone runtime result is claimed for Ticket 3. It cannot expose or map a userspace buffer until Ticket 4 registers `/dev/dri/cardX`.

## Ticket 4 Status

Ticket 4 is complete at the source/build boundary:

- `gud.ko` builds against the captured `4.9.112-g6b190d86b` target tree with the DRM/KMS objects linked.
- The USB, GEM, DRM/KMS, and environment/probe contract suites pass.
- New DRM/KMS and helper symbol references are exported by the matching target kernel build.
- The phone smoke utility compiles with `-Wall -Wextra -Werror` and libdrm.
- Review findings were corrected: the driver advertises `DRIVER_ATOMIC`, USB disconnect uses `drm_unplug_dev()` to defer final private-state release, and framebuffer creation verifies its memory extent.
- The Linux 4.9 atomic state initialization gap is corrected with `drm_mode_config_reset()` after simple-pipe construction and before DRM registration.

Phone runtime acceptance covers the DRM node, dumb buffer, framebuffer,
topology/property enumeration, atomic validation, and a successful
state-applying atomic commit. The final captured package confirms:

1. The Pi enumerated as `1d50:614d` at the dynamic path `1-1.2`; `gud.ko`
   probed successfully and created `/dev/dri/card1`.
2. `modetest -M gud -c -p` reported one connected `Virtual-2` connector, one
   CRTC and primary plane, `XR24` (XRGB8888), and preferred 1280x720@60.
3. `gud-kms-smoke /dev/dri/card1` completed a dumb-buffer create/map/write and
   printed `atomic modeset succeeded`.
4. The matching kernel-log interval contains no `BUG:`, `Oops`, `WARNING:`,
   `lockdep`, or `use-after-free` record.

The raw captures are retained under `backport-4.9/env/local/evidence/` as
`drm-kms-modetest.txt`, `drm-kms-smoke.txt`, and `drm-kms-dmesg.txt`.

## Ticket 5 Status

Ticket 5 is complete for the first-pixels/full-frame milestone. The active
source uses RGB565 end to end: it advertises the RGB565 DRM format, creates
16-bpp dumb buffers, validates the two-bytes-per-pixel layout, sends GUD v1
state-check/state-commit and controller/display-enable requests, then uploads
complete-row rectangles after `GUD_REQ_SET_BUFFER`.

On the OnePlus 6, the host transfer path uses a coherent DMA bounce buffer and
an explicit URB with `URB_NO_TRANSFER_DMA_MAP`. This replaced `usb_bulk_msg()`,
which caused the phone xHCI controller to remap the coherent buffer and return
`-EAGAIN`.

The initial fresh test reached the host bulk URB but timed out with `-110`.
Pi diagnostics isolated two FunctionFS failures: native AIO returned an invalid
completion despite the DWC2 controller receiving packets, and reopening the
already-enabled bulk endpoint could block indefinitely. The Pi now reuses the
endpoint file opened during FunctionFS initialization. The original repair
used synchronous 512-byte reads; the current candidate performs exact-length
reads capped at 16 KiB. It also binds USB only after DRM CRTC/framebuffer
initialization has succeeded, avoiding a transient half-started gadget during
host enumeration.

The final clean retest rebooted the Pi, deployed the fix, forced the OnePlus
through device then host mode, and obtained a fresh `1d50:614d` probe and
`/dev/dri/card1`. `gud-kms-fill` uploaded the complete 1280x720 RGB565 frame.
Pi logs recorded every payload read, including representative 64,000-byte tiles
in 125 packets taking 5--11 ms; every tile was copied/scaled and the back
buffer was presented. The phone kernel log had no new GUD `-110` atomic-update
or bulk-transfer failure. This is the required end-to-end hardware evidence.

The remaining work is performance and robustness: damage tracking, LZ4,
throughput measurement, unplug-during-payload behavior, and upstreaming or
replacing the local `usb-gadget` endpoint-file extension.

## Ticket 4 Atomic Diagnostic

The initial connector query reset the phone because the simple pipe's atomic
connector, CRTC, and plane state had not been initialized. Adding
`drm_mode_config_reset()` after simple-pipe construction corrected that Linux
4.9 initialization gap.

Fresh phone evidence now passes these stages without a kernel failure record:

- `caps`: open the GUD DRM card and enable universal-plane and atomic clients.
- `dumb`: create, map, write, unmap, and destroy a 1280x720 32-bpp dumb buffer.
- `fb`: create and remove an XRGB8888 framebuffer.
- `resources`, `connector`, `encoder-crtc`, `planes`, and `properties`: complete
  DRM/KMS object, fixed-mode, primary-plane, and property enumeration.
- `atomic-build`: create the mode blob and build the atomic request.
- `atomic-test`: execute Linux 4.9 atomic property validation without state
  application.
- `atomic-commit`: apply the real state change and return successfully.

The no-transfer pipe now consumes the pending DRM completion event
synchronously from `gud_pipe_update()`. Fresh ordered phone evidence passes
`atomic-commit` with exit code zero and contains no `WARNING:`, `flip_done timed
out`, `BUG:`, `Oops`, `lockdep`, or `use-after-free` record.

Do not confuse USB enumeration with DRM validation. At the beginning of every
session, force controller host mode and poll all USB device paths for
`1d50:614d`; only then run the KMS stages. A path such as `1-1.4` is historical,
not an invariant.

## Constraints

- Do not patch DRM core unless a concrete, documented standalone-module blocker proves it necessary.
- Do not replace the installed kernel or flash a boot image.
- Preserve unrelated working-tree changes, including `opencode.json` and untracked plan files.
- Keep `BACKLOG.md` evidence-backed. Runtime logs are required for hardware milestones; first pixels and hot-unplug hardening also require hardware observation.
