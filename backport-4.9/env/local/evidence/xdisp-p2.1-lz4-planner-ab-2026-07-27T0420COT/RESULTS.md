# XDISP-P2.1 unpaced LZ4 planner A/B

Date: 2026-07-27 04:13--04:20 COT

## Purpose

Compare the preserved ratio-cache adaptive planner with the test-only embedded
upstream bounded-output planner. Both are OnePlus-only diagnostic gud.ko
variants, operate at 1280x720 RGB565 with the unchanged actual host bulk
payload ceiling of 12,800 bytes, and use the Pi's active native 1280x720
scanout route. This is performance characterization, not XDISP-P0.1
verification.

## Artifacts and common setup

- Pi service: gud-userspace.service active; UDC configured throughout.
- Phone USB preflight: host mode; dynamic VID/PID gate found 1d50:614d at
  1-1.3; DRM node /dev/dri/card1 present.
- Ratio-cache artifact:
  /home/phablet/gud.xdisp-p2.1-ratio-cache-e49d8bc.ko
  (SHA-256 e49d8bcf3a0b5ee346b22c646a50783b315f3ceb0d9793bb83699554d1626e73);
  xdisp_ratio_cache=1, xdisp_frame_stats=1.
- Bounded artifact:
  /home/phablet/gud.xdisp-p0.1-upstream-lz4-bounded-b33dfaf.ko
  (SHA-256 761973718fb3d9659137e362e0b51c2b822a8d9d5c2297a7d38aa2945e71e4de);
  xdisp_bounded_discovery=1, xdisp_frame_stats=1.
- Normal /home/phablet/gud.ko was not changed.
- All frame updates are synchronous KMS commits. target_fps=0 submits the
  next frame as soon as the prior commit returns.

## Raw clip: three 300-frame unpaced trials

Command: /home/phablet/gud-kms-animate-raw /dev/dri/card1 raw 300 0
/home/phablet/xdisp-motion-1280x720-30fps-10s.rgb565le

| Policy | Run | FPS | Mean commit | Elapsed |
| --- | ---: | ---: | ---: | ---: |
| bounded-output | 1 | 31.107 | 27.118 ms | 9.612 s |
| bounded-output | 2 | 31.758 | 26.575 ms | 9.415 s |
| bounded-output | 3 | 32.067 | 26.341 ms | 9.324 s |
| bounded-output mean | 3 | 31.644 | 26.678 ms | 9.450 s |
| ratio-cache | 1 | 23.376 | 37.658 ms | 12.791 s |
| ratio-cache | 2 | 23.359 | 37.587 ms | 12.800 s |
| ratio-cache | 3 (replacement) | 24.415 | 35.951 ms | 12.246 s |
| ratio-cache mean | 3 | 23.717 | 37.065 ms | 12.612 s |

The original ratio-cache run 3 completed all 300 driver-frame records, but its
runner stdout was lost after SSH. A replacement run was used for its timing.
The 900 original frame records span dmesg timestamps 6706.105700 to
6744.189745, proving this was not a transport failure.

Bounded output is 33.4% higher in measured FPS and has 28.0% lower mean
synchronous commit time. Across the last 900 frame records for each policy:

| Metric | Bounded-output | Ratio-cache |
| --- | ---: | ---: |
| Rectangles/frame | 9.987 | 15.356 |
| Compression attempts/frame | 18.973 | 25.123 |
| Rejected attempts | 0.00% | 38.88% |
| Planner time/frame | 15.704 ms | 21.662 ms |
| SET_BUFFER time/frame | 4.233 ms | 6.471 ms |
| Bulk wait/frame | 4.861 ms | 6.472 ms |
| Transfer time/frame | 24.859 ms | 34.685 ms |

Every submitted payload remained at or below 12,800 bytes.

## Short mixed-workload comparison

The same unpaced runner used 120 frames for desktop and scroll, and 10 frames
for the intentionally expensive incompressible noise pattern.

| Workload | Policy | FPS | Mean commit | Planner | Transfer | Rectangles/frame |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| desktop | ratio-cache | 57.318 | 16.095 ms | 9.47 ms | 14.16 ms | 3.93 |
| desktop | bounded-output | 61.306 | 14.982 ms | 7.25 ms | 11.92 ms | 2.74 |
| scroll | ratio-cache | 32.507 | 25.153 ms | 11.06 ms | 23.13 ms | 12.10 |
| scroll | bounded-output | 25.997 | 33.169 ms | 18.87 ms | 31.08 ms | 11.89 |
| noise | ratio-cache | 6.372 | 146.474 ms | 20.04 ms | 147.46 ms | 147.40 |
| noise | bounded-output | 3.908 | 244.925 ms | 78.98 ms | 234.29 ms | 144.00 |

The single 120/10-frame samples are directional, not promotion benchmarks.
Bounded discovery is a clear win for raw-video and desktop motion, but not
for scrolling or incompressible content.

For noise, bounded discovery made 147.70 compression attempts/frame but no
rejected attempts because it immediately selected raw fallback. Its final frame
scanned 133,632,000 source bytes to deliver 1,843,200 source bytes: it
repeatedly runs bounded discovery over a wide candidate before falling back to
five raw RGB565 rows (12,800 bytes). That repeated work is the 78.98 ms
planner cost. Once data is known to be incompressible, the separate hard floor
is the roughly 144 serial SET_BUFFER plus bulk pairs needed for a full raw
frame under this cap.

## Safety and conclusion

After the final bounded run, xdisp_bounded_discovery=Y,
xdisp_ratio_cache=N, /dev/dri/card1, and the GUD USB node were present. The Pi
service was active and UDC configured. There was no new phone GUD timeout,
atomic-update, bulk-transfer, BUG, or Oops, and no Pi DWC2 timeout, vc4 fault,
paging request, or Oops. The Pi dirty_framebuffer-not-supported debug line is
the pre-existing optional dirty-framebuffer fallback, not a transport failure.

For compressible content, phone planning plus serial control/bulk pairs are the
main cost, and bounded output removes rejected work and reduces rectangles.
For incompressible content, bounded discovery's repeated wide scan is the
immediate avoidable cost; removing it cannot eliminate the approximately
144-transfer/frame cap cost.

Before this policy is promoted, implement and measure a conservative
per-frame incompressible backoff: after a non-beneficial bounded candidate
selects raw fallback, plan remaining chunks in that frame directly at the
known cap-safe raw-row size. Preserve the final 12,800-byte check and try
normal discovery again on the next frame. Repeat all four workloads with
multiple trials before selecting a default.

No service stop/restart was run as part of this performance-only comparison.
XDISP-P0.1 remains blocked and XDISP-P0.2 remains unstarted.
