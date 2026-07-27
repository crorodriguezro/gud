# XDISP-P2.1 upstream bounded-LZ4 first hardware gate

Date: 2026-07-27 04:02--04:04 COT (Pi journal time)

## Artifact and setup

- Source commit: `b33dfaf` (`perf: embed bounded upstream lz4 in xdisp module`)
- Module version: `xdisp-p0.1-adaptive-12800-upstream-lz4-v2`
- Phone artifact:
  `/home/phablet/gud.xdisp-p0.1-upstream-lz4-bounded-b33dfaf.ko`
- SHA-256:
  `761973718fb3d9659137e362e0b51c2b822a8d9d5c2297a7d38aa2945e71e4de`
- Test-only module parameters:
  `xdisp_bounded_discovery=1 xdisp_frame_stats=1`
- Preflight: phone controller `host`, dynamic phone VID/PID gate found
  `1d50:614d` at `1-1.3`; Pi service was `active` and UDC `configured`.

The normal `/home/phablet/gud.ko` was not modified. The previous adaptive
artifact remains available for rollback.

## Static RGB565 gate

`/home/phablet/gud-kms-fill /dev/dri/card1` completed successfully.

The phone logged one bounded frame with five compressed complete-row
rectangles: source `1,843,200` bytes, payload `50,357` bytes, and maximum
payload `12,159` bytes. The Pi received all five payloads with one 16 KiB
FunctionFS read each and returned every receive to `Idle`.

## Raw 300-frame gate

Command:

```text
/home/phablet/gud-kms-animate-raw /dev/dri/card1 raw 300 30 \
  /home/phablet/xdisp-motion-1280x720-30fps-10s.rgb565le
```

Result:

```text
modeset_ms=23.918
update_elapsed_s=9.976
update_fps=29.972
commit_avg_ms=28.083
commit_min_ms=18.168
commit_max_ms=92.510
RAW300_EXIT:0
```

The phone logged 301 bounded frames (the static gate plus 300 raw frames),
with a maximum actual bulk payload of `12,797` bytes and 5--10 rectangles per
frame. The final raw-frame records used ten compressed rectangles, each at or
below `12,158` bytes. The final Pi records show one 16 KiB FunctionFS read per
payload and successful LZ4 decode/copy.

No new `GUD atomic update failed`, `GUD bulk transfer failed`, timeout, BUG,
Oops, warning, DWC2, vc4, or Pi service error appeared in the post-run scans.
The Pi service remained `active`.

## Interpretation

This proves the new embedded upstream bounded-output planner can drive one
static frame and sustain this paced raw clip without exceeding the established
12,800-byte host bulk-payload ceiling. It is not a three-run A/B benchmark
against the ratio-cache policy, does not include scrolling/desktop/noise
workloads, and does not include a post-payload service restart. Therefore it
does not unblock `XDISP-P0.1` or start `XDISP-P0.2`.
