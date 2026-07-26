# XDISP-P0.1 OnePlus Adaptive-LZ4 Diagnostic Plan

Date: 2026-07-26
Status: isolated frame and post-payload lifecycle passed; three mini-cycles
and the ten-cycle matrix have not started

## Decision

Test a separately built OnePlus GUD module before attempting another Pi kernel
change. The diagnostic module compresses the largest legal complete-row
rectangle first, then reduces its height only when the measured LZ4 payload
would exceed 12,800 bytes. Every bulk URB has an independent pre-submit cap
check.

This is a diagnostic candidate, not a production fix and not XDISP-P0.1
verification. The normal `/home/phablet/gud.ko` must remain byte-for-byte
unchanged.

## Why this is the next path

- Gate E qualified complete 12,800-byte host transfers on three fresh Pi boots.
- Gate F proved that a 12,800-byte FunctionFS read cannot safely split a
  larger host URB.
- The `g_dma=0` Pi kernel still failed the first larger transfer.
- A host-side actual-payload cap tests the qualified boundary without another
  Pi or OnePlus kernel build.
- Compressing before splitting can retain useful cadence: a compressible full
  frame can use one SET_BUFFER and one bulk URB instead of 144 uncompressed
  five-row rectangles.

## Projects and files

Only `gud/backport-4.9` is changed for the implementation:

- the normal module sources retain their existing behavior;
- `variants/xdisp-lz4-12800/` builds a separate `gud.ko` artifact;
- a private Linux-4.9-derived LZ4 compressor is linked into that module because
  the phone kernel has no exported LZ4 compressor;
- offline planner/LZ4 tests and a staging-only runner are added.

The Pi `gud-gadget` source, Pi kernel, normal OnePlus module, and Mir remain
unchanged.

The two artifacts deliberately share the internal module name `gud`. Linux
therefore refuses to load the diagnostic while the normal driver remains
loaded; the distinct staged filename, module version, and dmesg marker identify
the diagnostic.

## Wire invariants

For each rectangle:

1. `length` is the uncompressed `width * height * 2` RGB565 byte count.
2. For LZ4, `compression=1`, `compressed_length` is the exact LZ4 block size,
   and the URB length equals `compressed_length`.
3. For raw fallback, `compression=0`, `compressed_length=0`, and the URB length
   equals `length`.
4. `0 < URB length <= 12,800` is checked immediately before SET_BUFFER and
   bulk submission.
5. One SET_BUFFER has exactly one matching bulk URB.
6. Rows always advance; no row is dropped or duplicated.
7. Compression that fails, grows the input, or cannot fit falls back only
   after reducing to a raw rectangle that is itself no larger than 12,800.

The coherent bounce buffer, explicit URB, `URB_NO_TRANSFER_DMA_MAP`, timeout,
retry, disconnect, and synchronous commit semantics remain unchanged.

## Adaptive algorithm

For the remaining rows:

1. Start with the largest complete-row rectangle allowed by the gadget's
   uncompressed `max_buffer_size`.
2. Compress it into a reusable scratch buffer.
3. If the compressed block is smaller than the source and no larger than
   12,800 bytes, send it.
4. Otherwise use the measured compression ratio to reduce the row count.
5. Recompress and recheck. Each iteration strictly reduces rows.
6. At worst, send the largest complete-row raw rectangle no larger than
   12,800 bytes.
7. Seed the next rectangle from at most twice the selected row count. This
   avoids recompressing the whole remaining frame for every incompressible
   band, while allowing the candidate to grow quickly when later content is
   compressible.

The final result is always measured and checked; safety does not depend on
compression-ratio monotonicity.

## Execution gates

### Gate 1 — offline

- Build both normal and diagnostic modules against the exact
  `4.9.112-g6b190d86b` tree.
- Confirm the normal build retains SHA-256
  `bd15c2c1bc4cd941bcac88bb13276b67620d9e2eec515973ff815add68f3630c`.
- Confirm diagnostic vermagic, USB alias, module identity, and empty dependency
  list.
- Require no unresolved LZ4 symbol.
- Run standard-libLZ4 round-trip and cap/coverage tests over solid, patterned,
  mixed, and incompressible frames, plus randomized bounded-output cases under
  ASan/UBSan.
- Run all existing backport hermetic tests.

### Gate 2 — stage only

- Commit source and tests first.
- Build an artifact tied to that commit and record SHA-256, modinfo, unresolved
  symbols, build log, config hash, and Module.symvers hash.
- Stage it under
  `/home/phablet/gud.xdisp-p0.1-adaptive-12800-<commit>.ko`.
- Refuse `/home/phablet/gud.ko`, verify the normal hash before and after, and do
  not activate during staging.

### Gate 3 — no-Pi load/unload

- Keep `1d50:614d` physically absent and verify no GUD DRM card/user.
- Unload the normal `gud` module.
- Load the explicitly named diagnostic module and require its unique dmesg
  marker with no warning, Oops, symbol, or format error.
- Unload it once, then explicitly restore and recheck the normal module.

### Gate 4 — one isolated frame

- Start from a fresh Pi boot on stock `g_dma=1`, normal LZ4/natural descriptor,
  16 KiB blocking reads, and containment enabled.
- Load the diagnostic before enumerating the Pi.
- Force OnePlus host mode and poll dynamically for `1d50:614d`.
- Send exactly one deterministic RGB565 frame.
- Capture all SET_BUFFER raw/compressed lengths, requested/actual URB lengths,
  Pi frame statistics, and both kernel logs.

Pass requires every actual URB to be at most 12,800 bytes, a complete presented
frame, no host `-110`, no short/impossible Pi read, and no kernel warning/Oops.

### Gate 5 — lifecycle and cadence

After Gate 4 only:

- physically detach USB;
- prove no transfer is in flight;
- perform one controlled Pi service stop/start;
- collect observed frame cadence and rectangle/payload statistics.

If cadence is usable and restart is clean, run three fresh mini-cycles. Only
then may the ten-cycle XDISP-P0.1 matrix restart from cycle 1.

## First hardware result

Gate 4 and the first Gate 5 lifecycle test passed on 2026-07-26. A clean
OnePlus reboot was required to recover the phone's host controller after
repeated host-mode writes and cable/hub reseats did not enumerate any external
device. Forcing `host` before loading either GUD module then immediately found
the known powered-hub topology and Pi at `1-1.2` / `1d50:614d`. Only the
diagnostic module was loaded.

One deterministic 1280x720 RGB565 color-bar frame covered all 720 rows with
four LZ4 rectangles. Their actual payloads were 11,260, 12,144, 12,380, and
10,204 bytes, totaling 45,988 bytes. The largest payload was 12,380 bytes
against the independent 12,800-byte submission cap. Every Pi payload completed
in one 16 KiB FunctionFS read and returned the receive session to `Idle`.
There was no host `-110`, short/impossible Pi read, warning, Oops, or poisoned
state.

The Pi's per-rectangle processing totals were 72, 48, 46, and 42 ms, or
208 ms across the isolated full frame. That is approximately 4.8 full
frames/s for this highly compressible pattern, not a sustained-cadence
benchmark.

After the sender exited, physical detach removed the OnePlus GUD device and
DRM card. The Pi UDC retained a stale `configured` value, but all transfers
were proven `Idle`. The controlled post-payload restart then exited the old
service zero, unbound the UDC before FunctionFS/DRM release, started a new
service instance on the same Pi boot, and returned the UDC to `not attached`.
There was no DWC2 endpoint-stop timeout, vc4 Oops, paging fault, warning, or
panic.

Evidence is under
`backport-4.9/env/local/evidence/xdisp-p0.1-oneplus-adaptive-frame-2026-07-26T1154COT/`.
The three fresh mini-cycles are now the next gate. Each proven-idle,
physically detached cycle must use separate `systemctl stop` and
`systemctl start` operations rather than another `restart`. `XDISP-P0.1`
remains blocked and `XDISP-P0.2` remains prohibited.

## Failure handling

On any Pi `InFlight`/`Poisoned` state, timeout, short/impossible read, warning,
Oops, or SSH loss:

- do not retry;
- do not stop/restart/reboot/signal the Pi service;
- collect read-only evidence;
- physically detach USB;
- physically reset the Pi before further work.

Clean phone rollback is: detach Pi, verify the GUD card is gone, unload
the diagnostic `gud`, verify the normal hash, and explicitly load
`/home/phablet/gud.ko`. Never copy the diagnostic over the normal module.

## Interpretation

- Reliable and usable: proceed through lifecycle, mini-cycles, and matrix.
- Reliable but too slow: record the host-only boundary as valid but reject it
  as the product path.
- Any transfer above 12,800: implementation failure; stop before interpreting
  Pi behavior.
- Failure with all URBs at or below 12,800: the Gate E boundary is not
  sufficient across hosts/content; targeted Pi DWC2/FunctionFS work remains.
- Host-only path cannot meet reliability and cadence: targeted Pi DWC2 kernel
  instrumentation/fix is the remaining normal-performance path.

Until all required gates pass, `XDISP-P0.1` remains blocked, `XDISP-P0.2` does
not start, and verification is not marked.
