# XDISP-P2.1 adaptive raw-30 motion gate — 2026-07-26

## Scope

This is a direct-KMS transport and presentation characterization. It does not
verify `XDISP-P0.1`, does not exercise a post-payload service restart, and
does not start `XDISP-P0.2`.

- Phone: OnePlus 6, Linux `4.9.112-g6b190d86b`
- Gadget: Raspberry Pi, Linux `6.12.47+rpt-rpi-v8`
- USB identity: `1d50:614d` at dynamic path `1-1.3`
- Source and physical mode: 1280x720 RGB565, native scanout (`scaled=false`)
- Host module: `/home/phablet/gud.xdisp-p0.1-adaptive-12800-2a8f59b.ko`
- Host module SHA-256:
  `2369eccc5cf3afc7ff8364921b8ce94fec8363518466f727b71d2d009c6c5999`
- Normal Pi descriptor maximum: 8,294,400 bytes
- Actual host bulk-payload ceiling: 12,800 bytes
- FunctionFS read ceiling: 16,384 bytes
- Raw input:
  `/home/phablet/xdisp-motion-1280x720-30fps-10s.rgb565le`
- Input: 300 frames, 30 fps nominal, 10 seconds, 552,960,000 bytes
- Input SHA-256:
  `6d9a805defac27daa9a777b19bbbf2d5fba0b702f176670815cc4a4ca031a7dc`

The earlier test-only Pi descriptor override
`GUD_TEST_MAX_BUFFER_SIZE=12800` was disabled before this gate. That override
made the protocol's source-buffer maximum 12,800 bytes, which forced
1280x5 RGB565 rectangles and 144 `SET_BUFFER`/bulk pairs per complete frame.
The adaptive OnePlus module's independent actual-payload ceiling stayed at
12,800 bytes throughout this gate.

## Controlled frame

`gud-kms-fill /dev/dri/card1` completed one 1,843,200-byte source frame in
five compressed complete-row rectangles:

```text
rows:             160, 151, 155, 151, 103
payload bytes:    57,679 total
maximum payload:  12,712 bytes
Pi receive state: five InFlight entries, five returns to Idle
```

This directly demonstrates that the normal descriptor permits large source
rectangles while the actual USB submissions remain inside the proven boundary.

## Raw 30-fps gate

The same predecoded raw animation was run twice:

```text
/home/phablet/gud-kms-animate-raw /dev/dri/card1 raw 300 30 \
  /home/phablet/xdisp-motion-1280x720-30fps-10s.rgb565le
```

| Run | Frames | Elapsed | Achieved FPS | Mean commit | Min / max commit | Exit |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 300 | 15.348 s | 19.481 | 44.550 ms | 33.321 / 61.076 ms | 0 |
| 2 | 300 | 13.073 s | 22.871 | 39.763 ms | 30.309 / 61.285 ms | 0 |

The first raw run consumed payload sequences 6 through 2,365: 2,360
rectangles, or 7.87 rectangles/frame. The second run ended at sequence 6,389:
4,024 rectangles, or 13.41 rectangles/frame. The variation is expected from
content-dependent LZ4 compression. Retained rate-limited host samples from the
second run show 13--14 compressed rectangles/frame, 121,495--125,881 payload
bytes/frame, and a maximum payload of 12,782 bytes. Every sampled transfer is
at or below the cap.

The Pi's final six payloads (sequences 6,384--6,389) each returned to `Idle`.
Their source rectangles ranged from 29 to 58 rows and their largest payload
was 12,708 bytes. The service remained active and the UDC remained
`configured` after both runs.

## Fault scan

The OnePlus post-module-load interval contained no GUD request failure,
atomic-update failure, bulk-transfer failure, timeout, disconnect, Oops, BUG,
or paging-request line. The Pi kernel scan contained no DWC2 endpoint-stop
timeout, vc4 fault, Oops, BUG, or watchdog reset/lockup. The only matching
kernel strings were boot-time `mmc_debug` and watchdog configuration messages.

## Conclusion

The 12,800-byte value is a safe ceiling for an *actual bulk payload*, not a
descriptor maximum for the uncompressed source rectangle. Restoring the normal
descriptor while retaining the host-side payload guard removed the 144-control-
transfer-per-frame cadence failure: both 300-frame raw runs completed with no
host `-110` and no Pi kernel failure. The observed 19.5--22.9 fps is a passing
transport characterization, not a 30-fps guarantee and not a complete
XDISP-P2.1 or XDISP-P0.1 verification.
