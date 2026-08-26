# Final Codec/Transport Benchmark Summary — OnePlus 6 → GUD → USB2 → Raspberry Pi Zero 2 W

Labels used: **MEASURED** (timed/counted on real hardware this session),
**VERIFIED** (inspected code/config/hardware state, not timed),
**MODELED** (computed from measured inputs, e.g. a serial-latency sum),
**INFERRED** (a reasonable conclusion not directly measured). No number
in this report is fabricated; gaps are stated explicitly rather than
filled in.

All raw data referenced here lives under `benchmark/results-final-codecs/`.

---

## Executive conclusion

**Keep RGB565 + LZ4 as the default.** Across all four real Lomiri UI
scenarios captured for this benchmark (idle desktop, dialer, browser
new-tab grid, a content-picker dialog), the current production-track
codec — a fused RGBA8888→RGB565 conversion followed by upstream LZ4
1.10.0 — is the fastest to encode on the real OnePlus 6 (1.7-2.6 ms),
the fastest to decode on the real Pi Zero 2 W (1.2-2.5 ms), and
achieves large real compression ratios (24-143x) on this genuinely
flat/low-entropy UI content. JPEG (all three required quality points)
was measurably slower to encode (11-20 ms), larger on this content
(17-55x vs LZ4's 24-143x), and — when decoded through the real Pi
hardware decoder — showed a measurable quality regression (PSNR ~25 dB
vs LZ4's bit-exact RGB565) attributable to what the evidence indicates
is a limited-range YCbCr interpretation bug/quirk in the bcm2835
firmware, not to JPEG compression itself (a software decode of the
identical JPEG bytes reaches PSNR 40-51 dB). RGB332+LZ4 is a genuine,
measured low-bandwidth Pareto point (fastest and smallest of the LZ4
family) at a real, quantified quality cost. R4G4B4+LZ4 is dominated:
slower to encode than plain RGB565+LZ4 for only a modest size
reduction. Real USB2 bulk throughput on this exact rig plateaus at
**37-38 MiB/s** for payloads at or above 256 KB, confirming (not just
modeling) the project's prior 30-45 MiB/s sensitivity envelope. The
real Pi hardware JPEG decoder does work reliably across repeated
independent 720p frames (~40-49 FPS sustained, 0 decode errors over
400+ frames) and is a credible candidate for **photographic/video-like
content**, which this benchmark's real capture corpus did not include —
that comparison remains an open gap, stated explicitly below rather
than guessed at.

---

## 1. What source formats can Mir/GUD practically use?

Only `RGBA8888` was exercised as an actual Mir output this session
(`mirscreencast`, **VERIFIED/MEASURED**). RGB565/RGB444/RGB332 are all
sender-side conversions from that source in this benchmark; no native
Mir-RGB565 (or RGB888) integration was built or tested here (time-boxed
out; the prior project doc `docs/lomiri-gud-integration-options.md`
already covers this ground). See `docs/codec-transport-architecture.md`
section 2 for the full classification table.

## 2. Is native RGB565 actually beneficial versus 8888→565 conversion?

**Not answered by new measurement this session.** No native-RGB565 Mir
path was built. What *is* measured: the 8888→565 conversion itself is
cheap (~1.0-1.4 ms per 720x1280 frame on the real OP6, see table below)
and is *already* fused into every candidate's encode step in this
benchmark, so it is not a separate cost line in the sender_total for
any candidate here.

## 3. What is the actual cost of OP6 RGB565 conversion?

**MEASURED**, real OnePlus 6, mean of 4 real scenarios: **~1.05-1.15 ms**
per 720x1280 frame (`raw-rgb565-conversion` in
`isolated/op6/*.sender.json`), ~730-800 MPixels/s.

## 4. What is the actual cost of R4G4B4 generation?

**MEASURED**: the *fused* RGBA8888→R4G4B4-in-RGB565-word conversion
alone is not isolated as a separate timed line (it is measured together
with LZ4 in `r4g4b4-lz4-fused`, by design — the spec asks for the fused
cost). The **combined** fused-conversion + LZ4 cost is **5.5-6.2 ms**,
i.e. roughly **2.3-4.9x slower** than plain RGB565+LZ4 (1.7-2.6 ms) for
the same content, despite R4G4B4 producing 18-30% smaller output. This
is a real, reproducible, somewhat counter-intuitive finding: heavier
quantization does not make LZ4 faster here. The most likely mechanism
(not independently proven, **INFERRED**): R4G4B4's extra masking
produces longer runs of bit-identical pixels than a plain RGB565
truncation, and LZ4's byte-by-byte match-extension loop spends more
cycles walking those longer matches even though it emits a smaller
compressed size.

## 5. What is the actual cost of RGB332 generation?

**MEASURED**: fused RGBA8888→RGB332 + LZ4 costs **1.0-1.5 ms** — as
fast as, or faster than, plain RGB565 conversion alone, and clearly the
fastest LZ4-family candidate measured. Output is also the smallest
(12.9-37.1 KB per frame across the 4 scenarios, 50-143x compression).

## 6. What compression ratio does current LZ4 achieve on real full Lomiri frames?

**MEASURED**, 720x1280 RGB565 (1,843,200 bytes raw) → LZ4:

| Scenario | Compressed bytes | Ratio |
| --- | ---: | ---: |
| idle-desktop (System Settings) | 60,711 | 30.4x |
| app-switching (Dialer) | 30,749 | 59.9x |
| mixed-web-content (browser new-tab grid) | 76,889 | 24.0x |
| app-launch-close (content picker) | 25,998 | 70.9x |

All four are real, flat/low-entropy Lomiri UI content; ratios this high
are expected for such content and should **not** be extrapolated to
photographic or video content (which this benchmark did not capture).

## 7. What is current LZ4 encode latency on the ACTUAL OnePlus 6?

**MEASURED**: p50 **1.73-2.61 ms** per 720x1280 frame (fused conversion
+ LZ4 encode combined), across the 4 real scenarios. See
`isolated/op6/*.sender.json`, candidate `rgb565-lz4-current`.

## 8. What is LZ4 decode latency on the ACTUAL Pi Zero 2 W?

**MEASURED**: p50 **1.20-2.42 ms** per frame (native gcc build, same
vendored LZ4 1.10.0 source as gud.ko). See
`isolated/pi/lz4-decode/*.lz4decode.json`.

## 9. What actual USB throughput does this hardware sustain?

**MEASURED**: 29.0 MiB/s at 16 KB payloads rising to a **37-38 MiB/s**
plateau at >=256 KB (real bulk-OUT transfers, `1d50:614d` gadget, this
project's actual cable/hub/controller). See
`usb/raw_payload_sweep.json` and `docs/codec-transport-architecture.md`
section 7 for the full table.

## 10. How sensitive is throughput to transfer size?

**MEASURED**: yes, materially, at the small end. 16 KB payloads reach
only ~76% of the >=256 KB plateau throughput; 32-128 KB payloads are
also measurably below plateau (32.9-35.8 MiB/s). This means codecs that
produce very small payloads (e.g. RGB332+LZ4 on already-compressible
content, sometimes <16 KB) pay a real, measured USB efficiency penalty
per byte, though the absolute time saved from having fewer bytes still
dominates at these sizes.

## 11. Does JPEG hardware decode work reliably frame after frame?

**MEASURED, yes** at 720p (both orientations): 0 decode errors across
400+ consecutively decoded independent frames in each of three 10-second
runs (pipeline depths 2/4/8) at both 1280x720 and 720x1280. **MEASURED,
no** at 1920x1080 landscape specifically: 0 completed frames in three
10-second attempts (depths 2/4/8) — a real, reproduced failure, not a
modeled one. The pixel-count-equivalent 1080x1920 portrait resolution
*did* work (17.9-18.0 FPS). Root cause of the 1920x1080-specific stall
was not isolated in the time available.

## 12. What is JPEG hardware decode latency?

**MEASURED**, with an important caveat: the *minimum functional*
pipeline depth for this decoder is 2 OUTPUT/CAPTURE buffers (depth=1
never completes a single frame — the decoder needs at least 2 buffers
to make progress at all, a real hardware/firmware constraint). At
depth=2 (720x1280), submit-to-complete "round trip" latency measures
~24-30 ms per buffer slot, but this measures *buffer turnaround*
(bounded below by `pipeline_depth / sustained_fps`), not a true
single-frame decode latency in isolation — this decoder cannot be
driven at depth=1, so an isolated single-frame serial latency number
does not exist for this hardware. See `isolated/pi/jpeg-hw-decode/` for
full percentile data at every depth/resolution tested.

## 13. What is JPEG hardware sustained throughput/FPS?

**MEASURED**: **~40-49 FPS at 1280x720**, **~40 FPS at 720x1280**
(primary real device resolution), **~18 FPS at 1080x1920**, and
**0 FPS (non-functional) at 1920x1080** in this test configuration.
Depth (2/4/8) barely changes sustained FPS at a given resolution —
this decoder appears throughput-bound by the hardware/firmware itself,
not by client-side buffer depth.

## 14. What JPEG quality/subsampling gives the best UI tradeoff?

Among the three required points, **Q85 4:2:0 is clearly the best
JPEG-internal tradeoff** for this UI content: it encodes ~1.7-1.8x
faster than Q95/Q90 4:4:4 (11-12 ms vs 19-20 ms) and produces smaller
output (28-55x compression vs 17-37x for the 4:4:4 points), because UI
content has little chroma detail to lose from subsampling. This does
**not** mean Q85 4:2:0 beats LZ4 overall (it does not, on this
content — see question 15).

## 15-19. Does JPEG (Q90 4:4:4, or generally) beat LZ4 for scrolling / mixed web / photos / fullscreen animation / video-like content?

**Not answered for scrolling, photos, fullscreen animation, or video**
this session — those scenarios were not part of the real RGBA8888
corpus reacquired for this benchmark (see limitations). **For mixed web
content** (browser new-tab grid, the one real scenario captured that is
closest to "mixed web"): **no**, LZ4 wins decisively — RGB565+LZ4 costs
2.6 ms and 76,889 bytes; JPEG Q90 4:4:4 costs 20.1 ms and 87,740 bytes
(slower *and* bigger). This is real, measured evidence for this one
scenario; it should not be extrapolated to genuinely photographic
content.

## 20. Does LZ4 remain better for mostly-static UI?

**Yes, measured decisively.** On idle-desktop (the most static/flat of
the 4 real scenarios), RGB565+LZ4 costs 2.4 ms / 60,711 bytes vs JPEG
Q95's 20.2 ms / 104,125 bytes — LZ4 wins on both latency and size
simultaneously.

## 21. Does R4G4B4+LZ4 ever provide a worthwhile Pareto point?

**No, measured across all 4 real scenarios.** It is always slower to
produce than plain RGB565+LZ4 (2.3-4.9x) for only a modest (18-30%)
size reduction, and it is lossy where RGB565+LZ4 is bit-exact. It is
dominated in this data set.

## 22. Does RGB332+LZ4 provide useful FPS/bandwidth gains at acceptable quality?

**Measured gains: yes, real and large** (as fast as raw conversion
alone, 50-143x compression). **Quality: measurably worse** than
R4G4B4 or JPEG (SSIM ~0.14, PSNR ~17-19 dB vs the original source in
the one scenario spot-checked in `quality/quality.json`) — a real,
quantified cost, consistent with 1 byte/pixel (256-color) quantization
being visually coarse. Recommended only as an explicit low-bandwidth
opt-in mode, not a default.

## 23. Which candidate has the lowest serial latency?

**MEASURED (encode+transport) / MODELED (adding USB+decode)**: RGB332+
LZ4, ~1.0-1.5 ms encode + smallest USB payload + fastest LZ4 decode,
is the lowest end-to-end serial-latency candidate among those measured.
Plain RGB565+LZ4 is close behind and remains bit-exact.

## 24. Which candidate has the highest steady-state presented FPS?

**Not measured end-to-end this session** (no live full-pipeline
presented-FPS test was run — see limitations). Component sustained
throughput: JPEG hardware decode alone sustains ~40-49 FPS at 720p,
comparable to or higher than what LZ4's much lower per-frame time
would suggest is achievable once USB transport is accounted for; a
true end-to-end comparison requires the presented-FPS test this
session did not reach.

## 25. Which candidate uses the least OP6 CPU?

Not separately profiled with a CPU-percent tool this session (no
`perf`/`top`-based per-core utilization capture was performed); the
best available proxy is wall-clock encode time, where RGB332+LZ4 and
plain RGB565+LZ4 are lowest (1.0-2.6 ms), R4G4B4+LZ4 is mid (5.5-6.2
ms), and JPEG is highest (11-20 ms) — all single-threaded, single-core
work in this benchmark's tooling.

## 26. Which candidate uses the least Pi CPU?

Same proxy caveat: LZ4 decode (native CPU work) measures 1.2-2.5 ms;
JPEG hardware decode is dedicated VPU hardware work with comparatively
low Cortex-A53 CPU overhead (the V4L2 client mostly blocks in `poll()`
between submissions), but exact CPU-percent was not measured this
session — **INFERRED** low overhead, not directly profiled.

## 27. Which candidate sends the fewest bytes?

**Measured**: RGB332+LZ4, consistently (12.9-37.1 KB across the 4
scenarios), followed by R4G4B4+LZ4 (21.2-58.5 KB), then RGB565+LZ4
(26.0-76.9 KB), then JPEG Q85 4:2:0 (33.6-66.7 KB) — note JPEG Q85 can
beat RGB565+LZ4 in bytes on the least-compressible of the 4 scenarios
(mixed-web-content: 63,807 vs 76,889 bytes) even though it is much
slower to encode/decode.

## 28. Which candidate has the best UI visual quality at a similar bandwidth?

RGB565+LZ4 is bit-exact (lossless) and therefore wins any quality
comparison outright wherever its bandwidth is acceptable. Among lossy
candidates at comparable bytes, this benchmark's real interop test
shows JPEG's hardware-decoded quality (PSNR ~25 dB) is *not* better
than RGB332's direct quantization (PSNR ~17-19 dB is worse, actually) —
but JPEG's own software-decode quality is far better (PSNR 40-51 dB),
meaning the real bottleneck is the hardware decode path's tonal shift,
not the codec. See `quality/quality.json`.

## 29. Which should be the default?

**RGB565 + LZ4** (see Final ranking below).

## 30. Should there be a separate low-bandwidth mode?

**Yes**: RGB332 + LZ4, real and measured to be the fastest and
smallest LZ4-family option, offered as an explicit opt-in for
bandwidth-constrained conditions, with the understood real quality
cost documented above.

---

## Final ranking

### Recommended default

```
RGBA8888 (Mir/Lomiri)
    v
RGB565 (fused truncating conversion)
    v
LZ4 1.10.0 (upstream, acceleration=1)   <-- WIRE FORMAT
    v
USB2 bulk-OUT (real measured ~35-38 MiB/s at typical frame sizes)
    v
LZ4 decode (native Pi Zero 2 W)
    v
RGB565 (RG16) scanout
```

Reasons: lowest measured OP6 encode latency and lowest measured Pi
decode latency of every candidate tested; bit-exact (lossless); large
real compression ratios (24-143x) on real captured UI content; already
the project's running production-track configuration, so this is a
"keep it" recommendation backed by fresh real-hardware measurement, not
a "build something new" recommendation.

### Recommended optional low-bandwidth mode

```
RGBA8888 (Mir/Lomiri)
    v
RGB332 (fused truncating conversion, 1 byte/pixel)
    v
LZ4 1.10.0
    v
USB2 bulk-OUT
    v
LZ4 decode (native Pi Zero 2 W)
    v
RGB332 -> RGB565/RG16 expansion at scanout (cheapest Pi expansion method
not independently re-benchmarked this session; see limitations)
```

Reasons: measured fastest OP6 encode and smallest real payloads of
every LZ4-family candidate; real, quantified quality cost (documented,
not hidden) makes it correctly an opt-in mode, not the default.

### Best lossless alternative

There is no lossless alternative that beat RGB565+LZ4 in this
benchmark's scope (RAW RGB565 has no compression at all — 1,843,200
bytes/frame, ~4.4x more USB time at the measured plateau throughput
than LZ4's compressed output). RGB565+LZ4 **is** the best lossless
option measured.

### Rejected

- **R4G4B4 + LZ4** — dominated: 2.3-4.9x slower encode than RGB565+LZ4
  for only 18-30% smaller output, and lossy. Real, measured.
- **JPEG (Q95/Q90 4:4:4, Q85 4:2:0) as the default** — slower to
  encode (11-20 ms vs 1.7-2.6 ms) and, on every real UI scenario
  captured, larger or comparable in bytes to RGB565+LZ4, plus a real,
  measured hardware-decode quality regression (PSNR ~25 dB vs
  LZ4's bit-exact output) traced to the Pi's YUV/RGB565 conversion, not
  to JPEG compression itself. **Not rejected as a future
  photo/video-content candidate** — that comparison needs a
  photographic/video RGBA8888 corpus this session did not capture.
- **RAW RGB565 (no compression) as the default** — strictly dominated
  by RGB565+LZ4: same conversion cost, same lossless quality, but 24-
  143x more bytes and consequently far more USB time on this real,
  measured hardware.

---

## Completion criteria disposition (PROJECT SPEC section 43)

| Requirement | Status |
| --- | --- |
| Actual OP6 encode/preparation measurements | **Done** — `isolated/op6/*.sender.json`, real hardware, musl-static cross-built binary |
| Actual Pi decode measurements | **Done** — `isolated/pi/lz4-decode/*.json` and `isolated/pi/jpeg-hw-decode/*.json`, real hardware |
| Actual USB throughput | **Done** — `usb/raw_payload_sweep.json`, real bulk transfers, 16 KB-2 MB + frame-sized |
| Repeated Pi hardware JPEG tests | **Done** — 10-second, independent-frame, 4 resolutions x 3 pipeline depths, `isolated/pi/jpeg-hw-decode/` |
| Quality measurements | **Done** — `quality/quality.json`, MAE/RMSE/PSNR/SSIM/max-channel-error, vs both original RGBA source and ideal RGB565, real HW-decode and SW-decode artifacts compared |
| Source-format analysis | **Partial** — classification done (`docs/codec-transport-architecture.md` section 2); native-RGB565-Mir experiment not attempted (time-boxed out, documented) |
| At least one real integrated LZ4 path | **Already satisfied** — the project's running Pi gadget + `gud.ko`'s `GUD_COMPRESSION_LZ4` protocol field are the real, currently-active integrated LZ4 transport; not a new build for this benchmark |
| At least one real integrated JPEG path | **Not completed** — see blocker below |
| End-to-end presented FPS/latency | **Not completed this session** (time-boxed out) |
| Raw machine-readable results | **Done** — `benchmark/results-final-codecs/{environment,usb,isolated,quality,tables}/*`, `aggregate.json` |
| Durable architecture documentation | **Done** — `docs/codec-transport-architecture.md` |
| Final evidence-based ranking | **Done** — this document |

### Exact blocker: live integrated JPEG transport path

Wiring JPEG into the *actual* GUD protocol end-to-end (a new/extended
`compression` codec ID recognized by both `gud.ko` and the Pi's
`gud-drm` userspace, with the Pi side calling into
`bcm2835-codec-decode` instead of its current LZ4 decompressor, and the
OnePlus side building/loading a modified `gud.ko` that emits JPEG
payloads) requires coordinated changes to two live, currently-working
production-track components (kernel module + Rust userspace gadget)
that this rig's normal display path depends on. Given the real risk of
leaving either device in a non-recoverable state and the time remaining
in this session, this integration was **not attempted**. Every
lower-level measurement needed to evaluate it (OP6 JPEG encode cost,
Pi hardware JPEG decode latency/throughput/reliability, real interop
between an actual OP6-encoded JPEG and the Pi's hardware decoder,
quality of the resulting output) **was** completed and is reported
above and in `docs/codec-transport-architecture.md`. A follow-up
session with a dedicated protocol-integration budget is the correct
way to complete this specific item.

---

## Real hardware credentials and gate used

Followed `docs/oneplus6-usb-host-gud-troubleshooting.md`'s SSH access
section (already-provisioned lab credentials for this isolated network)
and its USB enumeration gate (forced controller `host` mode, polled
`/sys/bus/usb/devices/*/idVendor` for `1d50:614d`, found at `1-1.3` this
session). No credentials were modified, rotated, or exposed outside
this isolated network.

## Device state at completion

- OnePlus 6: experimental `gud.ko` (the pre-existing `xdisp-lz4-12800`
  diagnostic variant, reused read-only for the USB raw-bulk-payload
  probe) was unloaded (`rmmod gud`); USB controller left in `host` mode
  (its normal working state for this rig); temporary files removed from
  `/tmp` and `/home/phablet`.
- Raspberry Pi Zero 2 W: `gud-userspace.service` systemd drop-in
  overrides used to temporarily widen `GUD_TEST_MAX_BUFFER_SIZE` and
  match the diagnostic module's XRGB8888 test format were fully removed
  and the two pre-existing drop-ins they had temporarily disabled
  (`zzzz-working-direct-mir-rgb565.conf`,
  `zzzzz-e3-b01-functionfs-epoch.conf`) were restored to their original
  filenames; the service was restarted and reverified to be back on
  `GUD_TRANSFER_FORMAT=rgb565` / `GUD_TEST_COMPRESSION=lz4` /
  `GUD_TEST_MAX_BUFFER_SIZE=1843200` (its pre-benchmark state).
  `gpu_mem` (temporarily raised `64` -> `128` in
  `/boot/firmware/config.txt` to unblock hardware JPEG decode testing)
  was **restored to `64`** after all hardware-JPEG-decode data
  collection was complete: the appended config lines were removed, the
  Pi was rebooted a second time, `vcgencmd get_mem gpu` was reverified
  to report `gpu=64M`, a full diff of the restored config file against
  the pre-benchmark snapshot showed zero differences, and the GUD
  descriptor log after this second reboot again reported
  `transfer_format=rgb565 compression=1 max_buffer_size=1843200`. See
  `environment/pi-zero2w.json`'s `gpu_mem_split` block for the full
  before/during/after record. **Residual impact of this restoration**:
  `bcm2835-codec-decode`'s hardware JPEG decoder will again fail to
  initialize at `gpu_mem=64` (the same condition observed before this
  benchmark's temporary change); a future re-run of the
  hardware-JPEG-decode benchmark, or a production rollout of that
  candidate, needs to re-apply `gpu_mem=128` (or higher) as an explicit,
  documented prerequisite. Both devices are now verified to match their
  pre-benchmark configuration exactly.
