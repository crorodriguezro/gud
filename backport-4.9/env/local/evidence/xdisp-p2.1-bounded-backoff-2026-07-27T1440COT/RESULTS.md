# XDISP-P2.1 bounded-LZ4 incompressible-backoff gate

Date: 2026-07-27 14:39--14:41 COT

## Change under test

The test-only OnePlus bounded-output policy now owns frame-local state.

- It runs normal upstream LZ4 bounded discovery until it returns a raw
  cap-safe rectangle.
- That first fallback is still a measured discovery attempt.
- The remainder of the same atomic update sends direct complete-row raw
  rectangles, each at or below 12,800 bytes, with zero compression attempts.
- A new framebuffer update constructs fresh state and tries discovery again.

The state is local to gud_pipe_transfer_xdisp(); it is not cached across
frames or devices. The existing adjacent pre-submit payload guard remains
unchanged.

## Artifact and preflight

- Source build: diagnostic target
  backport-4.9/variants/xdisp-lz4-12800/gud.ko.
- Phone artifact:
  /home/phablet/gud.xdisp-p2.1-bounded-backoff.ko.
- SHA-256:
  3238f3eba872a0660b7941f6689965839a7b8a3fb5d202a1eda749e2c9856dfd.
- Parameters:
  xdisp_bounded_discovery=1 and xdisp_frame_stats=1;
  xdisp_ratio_cache=N.
- The normal /home/phablet/gud.ko was not changed.
- Phone host-mode VID/PID gate found 1d50:614d at dynamic path 1-1.3.
- Pi service was active, UDC configured, and restart count was zero before
  and after the gate.

Offline checks passed before deployment:

- test-xdisp-lz4.sh
- SANITIZE=1 test-xdisp-lz4.sh
- test-xdisp-lz4-contract.sh
- make -C backport-4.9 xdisp-lz4-12800

The new unit case uses a random 1280x720 RGB565 frame. It proves one bounded
discovery followed by 143 direct five-row raw chunks, all cap safe, with zero
further compression attempts; a fresh state retries discovery on the next
frame.

## Incompressible noise result

Command:

  /home/phablet/gud-kms-animate-raw /dev/dri/card1 noise 10 0

The first 10-frame run completed with no error but one frame waited 2.793 s in
bulk completion. That is a USB scheduling outlier, not planner work: its
planner time was 3.577 ms and no phone or Pi fault was recorded.

The immediate repeat is the clean comparison:

| Metric | Old bounded planner | New frame-local backoff |
| --- | ---: | ---: |
| Updates/s | 3.908 | 6.528 |
| Mean commit | 244.925 ms | 142.210 ms |
| Planner/frame | 78.98 ms | 3.506 ms |
| Transfer/frame | 234.29 ms | 141.523 ms |
| Compression attempts/frame | 147.70 | 1.0 |
| Backoff raw rectangles/frame | absent | 143.0 |
| Maximum actual payload | 12,800 bytes | 12,800 bytes |

Each repeated-gate frame used 144 raw complete-row rectangles: the first
performed bounded discovery, then 143 used direct raw backoff. The new result
reaches the earlier ratio-cache noise baseline of 6.372 updates/s while
eliminating repeated discovery. It does not remove the fundamental 144 serial
SET_BUFFER plus bulk pairs required for incompressible 1280x720 RGB565 under
the proven 12,800-byte cap.

## Regression checks

- Raw 300-frame unpaced RGB565 clip:
  32.348 updates/s, 25.806-ms mean commit. The 300 driver summaries averaged
  9.973 rectangles/frame, 15.266-ms planning, 24.094-ms transfer, and zero
  backoff rectangles. Normal compressible discovery remained active.
- Scroll, 120 unpaced frames:
  30.085 updates/s, 28.674-ms mean commit. It averaged zero raw and zero
  backoff rectangles, so this single-run improvement cannot be attributed to
  this change; it is only a regression check.

The OnePlus driver logged no GUD timeout, bulk-transfer error, atomic-update
failure, BUG, or Oops. The Pi remained active with configured UDC and zero
service restarts. The Pi kernel scan contained no DWC2 timeout, vc4 fault,
paging request, call trace, or Oops.

## Conclusion

The frame-local raw backoff corrects the measured avoidable incompressible
planner cost without weakening the actual 12,800-byte transfer invariant and
without a Linux-kernel rebuild. It remains a test-only policy. Before any
default-policy decision, repeat the four-workload comparison with multiple
trials and collect CPU, latency-percentile, and USB-throughput measurements.

This is XDISP-P2.1 performance evidence only. XDISP-P0.1 remains blocked and
XDISP-P0.2 remains unstarted.
