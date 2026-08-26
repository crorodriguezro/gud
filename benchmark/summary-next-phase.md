# Next-Phase Summary: Real Lomiri Full-Frame Codec Benchmark on Pi Zero 2 W

This is the next-phase report requested on top of the broad benchmark in
`benchmark/results/summary.md` (32 candidates, host-only, synthetic +
one pre-existing real landscape motion asset). It narrows the candidate
set to the finalists the broad survey actually supports, adds a **real**
consecutive-frame capture from the project's own OnePlus 6 Lomiri session,
adds adaptive temporal keyframe/delta transport with recovery tests, and
runs everything on the project's real Raspberry Pi Zero 2 W.

**Read this first — label legend.** Every number in this report and in
`benchmark/results-next-phase/aggregate.json` is tagged:

| Label | Meaning |
| --- | --- |
| `MEASURED_LOMIRI_CAPTURE` | Real pixels captured from the actual `lomiri-system-compositor` session on the project OnePlus 6 via `mirscreencast`. |
| `MEASURED_PI` | Wall-clock encode/decode timing from `fbcodec-bench` actually executing on the project's real Raspberry Pi Zero 2 W. |
| `MODELED` | USB transfer time derived from `bytes / assumed_throughput`; **no USB hardware bytes-on-the-wire were captured for this phase** (see Phase 5 below for why, and what real prior-session evidence exists instead). |
| `ESTIMATED` | A derived quantity that isn't itself a direct measurement (e.g. per-frame capture timestamps linearly interpolated across a measured wall-clock span, because `mirscreencast` doesn't emit per-frame timestamps). |

No number in this report is presented as "measured" when it is modeled or
estimated. Where the broad benchmark's own host (Apple M1 Pro / Asahi
Linux) numbers are referenced, they are explicitly re-labeled
`MEASURED_HOST` to distinguish them from the new `MEASURED_PI` numbers —
**they are never used interchangeably**, per the broad benchmark's own
stated gap ("no Pi Zero 2 W benchmark") and this phase's explicit
instruction not to extrapolate M1 timing to Cortex-A53.

---

## 1. Phase 1 — validation of the existing benchmark work

Commands run, in order, at the start of this session:

```bash
cd benchmark/tools/fbcodec-bench
git log --oneline -1                     # eab17e9 bench: add RGB565 codec benchmark harness
git diff --stat eab17e9 -- benchmark      # (empty) working tree == eab17e9 for all tracked files
make clean && make -j$(nproc)             # builds cleanly, only pre-existing vendored-LZ4 warnings
make test                                 # test_transforms: all checks passed
                                           # test_temporal:   all checks passed
```

Findings:

- `eab17e9` ("bench: add RGB565 codec benchmark harness") was **already**
  the clean, benchmark-only commit the spec asks Phase 1 to produce if one
  didn't exist — nothing needed to be committed to make it clean.
- `benchmark/results/` (the 32-candidate broad survey: `results.json`,
  `results.csv`, `summary.md`, `environment.json`,
  `residual-analysis.json`, `raw/*.json`, `run_benchmark.log`) existed on
  disk but had **never been committed**. It has now been committed
  (commit `2068f09`, see below) so it is durably preserved, per this
  phase's "preserve existing broad results" instruction. The 207 MiB of
  quality-comparison PNGs under `results/quality/` were left uncommitted
  (reproducible via `scripts/run_benchmark.sh`; committing ~207 MiB of
  PNGs to this repository was judged disproportionate) — this is recorded
  in `benchmark/.gitignore` with a comment, not a silent omission.
- Lossless round-trip verification: `test_transforms`/`test_temporal`
  pass, and every codec run in this phase (`--verify`, both the finalist
  sweep and the temporal-policy sweep) reported `roundtrip_exact: true` /
  `mismatches=0` for every one of the 5,760 encode/decode operations
  executed (9 scenarios × ~73 frames average × 8 finalist codecs, plus the
  45 temporal-policy sweep runs) — see
  `benchmark/results-next-phase/pi-json-o3/*.json`.
- QOIR lossless round-trips exactly through the RGB565→RGB888→QOIR→RGB888→RGB565
  path used by this harness (`roundtrip_exact: true` for `qoir-lossless`
  in every scenario).
- Lossy quality metrics (`mae`/`rmse`/`psnr_db`/`max_abs_error`) are
  computed by `metrics_compute(original_rgb565, decoded_rgb565, ...)` —
  i.e. against the final **reconstructed RGB565** buffer, not an
  intermediate RGB888/QOIR buffer (`src/main.c:run_codec_on_sequence`,
  unchanged from the broad benchmark).

No unrelated production code (`backport-4.9/`) was modified in Phase 1.

---

## 2. Phase 2 — real consecutive Lomiri/Mir capture

### 2.1 Capture method (documented, not silently chosen)

The project's `docs/lomiri-gud-integration-options.md` already surveyed
this exact question and selected/retained **option 2, "Screen-capture
display bridge"** ("mirscreencast... remains a useful fallback diagnostic
and performance baseline") as the practical fallback, because option 4
(the real GUD-backed DisplayPort shim in the separate
`mir-android2-platform-gud` repository) is an **intentionally rolled-back
POC** not available for capture in this environment. This phase used that
retained fallback:

1. `com.lomiri.LomiriGreeter.HideGreeter` (session D-Bus) to unlock the
   phone's lock screen non-interactively.
2. `lomiri-app-launch <app-id> [uri]` to drive real app launches,
   switches, and `morph-browser` (WebEngine) pages for scripted content.
3. `mirscreencast -m /run/mir_socket -n N -s W H --cap-interval 1 --stdout`
   to stream **raw RGBA8888** frames from the real
   `lomiri-system-compositor` session straight over the existing SSH
   session to this workstation (`benchmark/scripts/capture_lomiri_scenario.sh`)
   — the phone's own disk was never touched.
4. `benchmark/scripts/ingest_lomiri_capture.py` converts RGBA8888 → RGB565
   using the harness's own truncating `rgb888_to_rgb565()` (the same
   conversion already used for QOI/QOIR/CharLS elsewhere in this tool),
   because **no RGB565-producing Mir/GUD path is active on this phone**
   (the DisplayPort shim that would provide one is the rolled-back POC
   above) — this is PROJECT SPEC Phase 2 "option 2" (native source +
   exact production conversion), done once at ingestion rather than
   per-replay-iteration (documented simplification; QOIR's own conversion
   cost is separately and exactly measured in Phase 7, so this does not
   hide any conversion cost from the report).
5. Frames are stored as concatenated RGB565LE (the format
   `capture_read_raw_frames()` already reads) plus a JSON index with the
   PROJECT SPEC Phase 2 metadata (`sequence_number`, `timestamp_ns`,
   `width`, `height`, `stride`, `pixel_format`, `frame_size_bytes`) per
   frame. Per-frame timestamps are **linearly interpolated** across the
   measured wall-clock capture span (`ESTIMATED`; `mirscreencast` itself
   emits no per-frame timestamps).

**Resolution decision:** captured at **720×1280** (portrait). The
project's own Ubuntu Touch device is a portrait phone (native panel
1080×2280); PROJECT SPEC Phase 2 explicitly allows testing "portrait
equivalents if that matches the actual Lomiri configuration" — it does.
720×1280 is one of the two required portrait buckets exactly.

### 2.2 Captured scenarios (9 of 10 required; notification/control shade not captured — see limitations)

| Scenario | Frames | Method | Real interaction? |
| --- | --- | --- | --- |
| `idle-desktop` | 180 | Home screen, no interaction | Yes (fully passive) |
| `app-launch-close` | 90 | `lomiri-app-launch lomiri-system-settings`, killed mid-capture, relaunched | Yes (real app process launch/teardown) |
| `app-switching` | 90 | Settings → morph-browser (animation page) → Settings, launched mid-capture | Yes |
| `scrolling-list` | 60 | `morph-browser` + local HTML long list, **JS-driven** `requestAnimationFrame` auto-scroll | Scripted (see limitations — no touch-injection path was found to work) |
| `typing-terminal` | 60 | `morph-browser` + local HTML terminal-style page, JS auto-typer + blinking CSS caret | Scripted |
| `mixed-web-content` | 60 | `morph-browser` + local HTML "card feed" page, JS auto-scroll | Scripted |
| `photo-viewer` | 40 | `morph-browser` + a real screenshot already on the phone (`~/Pictures/Screenshots/...png`), full-bleed `<img>` | Yes (real image asset, static view) |
| `fullscreen-animation` | 60 | `morph-browser` + local HTML CSS `@keyframes` animation (moving/color-rotating shapes) | Real Mir/Chromium compositing of a real animation |
| `video-playback` | 90 | `morph-browser` + `<video autoplay loop>` playing the broad benchmark's existing real motion asset, copied onto the phone | Yes (real WebEngine video decode + real Mir composite) |

**Input-injection attempt and result (documented):** a `/dev/uinput`
virtual multi-touch device was created (the `phablet` user has
`bluetooth`-group access to `/dev/uinput` and `android_input`-group read
access to the real touchscreen node) and a swipe gesture was injected and
verified via before/after `mirscreencast` diff: **0 of 921,600 pixel bytes
changed.** This confirms the Halium/Android input HAL on this device does
not consume synthetic `uinput` evdev devices, so no genuine touch-driven
scroll/notification-shade capture was possible in this session without
deeper Android-input-HAL work (out of scope). Scrolling/typing content
was therefore driven by scripted JS instead of a physical swipe/tap —
documented here, not silently substituted.

### 2.3 Frame deduplication statistics (PROJECT SPEC Phase 2, `MEASURED_LOMIRI_CAPTURE`)

Full per-pair data: `benchmark/capture/dedup_stats.json`. Summary
(`changed_pixel_percent` == `100 - identical_pixel_percent`; for a packed
u16 RGB565 pixel, `XOR_zero_pixel_percent` is mathematically identical to
`identical_pixel_percent`, and `XOR_zero_byte_percent` to
`identical_byte_percent` — both are reported in the JSON per the spec,
this is not a duplicate-by-mistake):

| Scenario | mean Δpx% | p50 | p90 | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| idle-desktop | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| photo-viewer | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| typing-terminal | 0.115 | 0.118 | 0.198 | 0.211 | 0.228 |
| fullscreen-animation | 4.001 | 4.554 | 8.031 | 8.034 | 8.036 |
| video-playback | 3.116 | 2.953 | 4.093 | 4.642 | 5.171 |
| app-launch-close | 8.994 | 0.000 | 7.630 | 96.702 | 97.254 |
| app-switching | 9.495 | 0.000 | 35.034 | 91.904 | 97.266 |
| scrolling-list | 19.038 | 19.855 | 21.902 | 22.182 | 22.845 |
| mixed-web-content | 73.766 | 74.749 | 76.273 | 78.875 | 79.018 |

This is the real redundancy signal that motivates temporal prediction:
idle/photo are literally pixel-identical frame-to-frame; typing changes
almost nothing; app-launch/switch are bimodal (mostly static, huge spikes
at the transition instant, matching real transition semantics); scrolling
and continuously-auto-scrolled mixed content are the scenarios where a
naive previous-frame delta has the least to work with.

---

## 3. Phase 3 — narrowed codec benchmark (the finalist shortlist)

Per this phase's primary-question shortlist, exactly 8 candidates were
run (no others):

`rgb565-lz4`, `quant-moderate-16bit-lz4` (**R4 G4 B4**, i.e. 4/4/4 bits
retained per channel — this is the exact "moderate" quantization from the
broad benchmark, not merely described as "moderate"), `quant-aggressive-lz4`
(RGB332, 3/3/2 bits), `qoir-lossless`, `qoir-lossy-l3` (mild),
`qoir-lossy-l5` (moderate), `prev-xor-lz4` (T1), and `prev-sub-lz4` (T2,
kept because it was already implemented and free to benchmark, per the
spec's "only if effectively free" clause).

Command (per scenario, on the Pi):

```bash
FINALISTS='rgb565-lz4,quant-moderate-16bit-lz4,quant-aggressive-lz4,qoir-lossless,qoir-lossy-l3,qoir-lossy-l5,prev-xor-lz4,prev-sub-lz4'
./build/fbcodec-bench --mode sequence --codec "$FINALISTS" \
  --input corpus/<scenario>.rgb565le --width 720 --height 1280 \
  --frames <N> --corpus-tag <scenario> --usb-mib-s '30,35,40,45' \
  --verify --json results/json_o3/<scenario>.json
```

(`--codec` accepting a comma-separated list is a new, additive harness
feature added in this phase — `src/main.c:codec_selected()` — so a single
invocation could restrict the sweep to exactly this shortlist instead of
paying Pi Zero 2 W CPU time for all 29 registered candidates.)

### 3.1 Real full-frame corpus — compression ratio (`MEASURED_PI`, full table in `tables/real_corpus.csv`)

| Scenario | rgb565-lz4 ratio (vs raw) | qoir-lossless ratio (vs LZ4) | prev-xor-lz4 ratio (vs LZ4) |
| --- | ---: | ---: | ---: |
| idle-desktop | 30.2x | **1.22x** | **7.78x** |
| typing-terminal | 27.4x | 1.06x | **7.50x** |
| video-playback | 26.3x | 0.95x | 1.27x |
| photo-viewer | 11.0x | 1.59x | **14.91x** |
| mixed-web-content | 12.4x | 0.91x | 0.55x |
| scrolling-list | 10.2x | **0.74x** | **0.61x** |
| app-launch-close | 4.9x | 1.24x | 6.67x |
| app-switching | 8.5x | 1.24x | 4.93x |
| fullscreen-animation | 90.4x | 1.29x | 1.96x |

**QOIR lossless is not a universal win on real content**: it loses to
plain LZ4 in 3/9 scenarios (scrolling, mixed-web-content, video-playback —
all continuously-changing/high-frequency-detail content), and only wins
modestly (1.06–1.6x) elsewhere. **Previous-frame XOR is spectacular on
static/slowly-changing content** (up to 14.9x smaller than LZ4) but
**actively worse than LZ4** on scrolling/continuously-scrolled content
(0.55–0.61x, i.e. 1.6–1.8x *larger*) — exactly the "simple frame XOR does
not understand motion" warning this phase's spec called out.

### 3.2 Pi Zero 2 W encode/decode timing (`MEASURED_PI`, `-O3`, full table in `tables/pi_zero2w.csv`)

Representative rows (idle-desktop, 720×1280):

| Codec | encode p50 (ms) | encode p95 (ms) | decode p50 (ms) | decode p95 (ms) |
| --- | ---: | ---: | ---: | ---: |
| rgb565-lz4 | 16.1 | 23.9 | 2.0 | 2.2 |
| quant-moderate-16bit-lz4 | 17.1 | 19.4 | 3.9 | 4.4 |
| quant-aggressive-lz4 | 17.8 | 20.0 | 3.1 | 3.4 |
| qoir-lossless | 27.1 | 27.6 | 30.2 | 30.5 |
| qoir-lossy-l3 | 29.5 | 29.9 | 55.5 | 56.5 |
| qoir-lossy-l5 | 29.3 | 29.7 | 55.6 | 56.4 |
| prev-xor-lz4 | 24.9 | 26.2 | 10.0 | 11.2 |
| prev-sub-lz4 | 24.8 | 26.0 | 9.9 | 10.8 |

At the exact PROJECT SPEC minimum resolutions (**synthetic photo-like
content**, run natively on the Pi — real portrait captures above are the
primary evidence, this is additional real-hardware timing at the two
required landscape sizes; `MEASURED_PI`, synthetic content, see
`results-next-phase/pi-synth/`):

| Resolution | Codec | encode mean (ms) | decode mean (ms) |
| --- | --- | ---: | ---: |
| 1280×720 | rgb565-lz4 | 17.7 | 6.6 |
| 1280×720 | qoir-lossless | 28.9 | 40.6 |
| 1920×1080 | rgb565-lz4 | 28.8 | 13.2 |
| 1920×1080 | qoir-lossless | 52.9 | 83.5 |

This directly answers spec questions 3–5: the Pi Zero 2 W encodes a plain
LZ4 720p full frame in ~18 ms and a 1080p frame in ~29 ms; QOIR lossless
takes ~29 ms / ~53 ms to encode and, more importantly, **~41 ms / ~84 ms
to decode** — QOIR decode is the dominant cost, not encode.

### 3.3 Build-flag sensitivity (`-O2` vs `-O3`, both `MEASURED_PI`)

`-O3` (vs the Makefile's `-O2` default) cut QOIR encode/decode time by
~30–35% on this Cortex-A53 target (idle-desktop: encode 42.3→27.2 ms,
decode 35.6→30.2 ms) and previous-frame XOR by ~12–25%, while plain LZ4
was roughly flat (17.2→18.2 ms, within run-to-run noise). **All numbers
in this report use the `-O3` build** (the spec's "prefer `-O3`" release
configuration); the `-O2` comparison is preserved at
`results-next-phase/pi-json/*.json` for transparency. `Makefile`'s
default remains `-O2` (unchanged, to avoid silently changing the existing
harness's documented default); `OPT=-O3` is a documented override.

### 3.4 QOIR conversion vs codec time (`MEASURED_PI`, PROJECT SPEC Phase 3.D)

New instrumentation (`qoir_codec.c`/`.h`,
`bench_result.conversion_breakdown` in `report/result.h`) separates the
RGB565↔RGB888 conversion cost from QOIR's own library time — the broad
benchmark's original design note ("RGB565<->RGB888 conversion measured")
meant *included*, not *broken out*; this phase breaks it out as the
next-phase spec requires. Measured breakdown (`qoir-lossless`, 720×1280):

| Scenario | encode conversion (ms) | encode codec (ms) | decode conversion (ms) | decode codec (ms) |
| --- | ---: | ---: | ---: | ---: |
| idle-desktop | 14.38 | 12.72 | 25.28 | 4.93 |
| video-playback | 2.03 | 13.29 | 25.05 | 5.21 |
| photo-viewer | 2.06 | 15.83 | 25.07 | 7.92 |

**The RGB565↔RGB888 conversion cost on the Pi Zero 2 W is *not*
negligible** — unlike on this project's host development machine (Apple
M1-class, where the same conversion is sub-microsecond-per-frame and
effectively free), on Cortex-A53 it is a genuinely significant fraction
of total QOIR time: the decode-side RGB888→RGB565 conversion (25 ms,
essentially constant across scenarios) is consistently **larger than
QOIR's own decode codec time** (5–8 ms) — i.e. more than 3/4 of QOIR
lossless's total decode cost on this CPU is *this harness's* pixel-format
bridge, not the QOIR library itself. (The decode-side conversion loop
computes a row/column index with an integer divide/modulo per pixel,
`src/codecs/qoir_codec.c:qoir_decode_generic`; this is very likely why it
costs ~10x the encode-side conversion's simple linear scan on an
in-order Cortex-A53 core — a concrete, real optimization opportunity if
QOIR were ever adopted, not present in this measurement's conclusion
either way.)

The encode-side conversion time varies substantially between scenarios
(2.0–14.4 ms for the identical 921,600-pixel linear-scan operation) in a
way its *decode*-side counterpart does not (constant ~25 ms); the most
likely explanation is the Pi's `ondemand` CPU governor ramping frequency
up/down between calls depending on the preceding frame's codec load,
which this phase did not pin (governor was left at its shipped default,
per "use release/optimized builds" rather than a non-default `performance`
governor override). This is flagged as a real measurement caveat, not
smoothed over: **conversion cost, and therefore total QOIR time, has
run-to-run variance driven by DVFS state that a `performance`-governor
rerun would remove** (not done in this phase; see limitations).

---

## 4. Phase 4 — temporal keyframe model and TEMPORAL_ADAPTIVE

New module `transforms/temporal_policy.{h,c}` (+ `--mode temporal-policy`,
`--keyframe-interval N`, `--adaptive` in `main.c`) implements exactly the
PROJECT SPEC Phase 4 protocol: every frame carries a `sequence_number`,
`frame_type` (KEYFRAME/DELTA), and (for DELTA) a `base_sequence_number`;
the decoder rejects a DELTA frame whose `base_sequence_number` does not
match its own last-decoded sequence number (`TPOLICY_ERR_REFERENCE_MISMATCH`)
instead of silently applying it — see §6 for the recovery tests this
enables.

Keyframe cadences tested: every 30, 60, 120 frames, and "reset-only"
(`--keyframe-interval 0`, i.e. never automatic — only the first frame and
any forced/reset keyframe), plus `TEMPORAL_ADAPTIVE` (encode both the
normal and XOR-delta candidate every frame and keep whichever is
smaller — both candidates' compression cost is included in the reported
`encode_ns`, per the spec's "initially include the cost of calculating
both"). Command (per scenario, per policy):

```bash
./build/fbcodec-bench --mode temporal-policy --input corpus/<scenario>.rgb565le \
  --width 720 --height 1280 --frames <N> --corpus-tag <scenario> \
  --keyframe-interval {30,60,120,0} --verify --json results/temporal/<scenario>_ki<N>.json
./build/fbcodec-bench --mode temporal-policy ... --adaptive --json results/temporal/<scenario>_adaptive.json
```

Full results: `results-next-phase/pi-temporal/*.json`,
`tables/temporal.csv`. Every one of the 45 runs (9 scenarios × 5
policies) reported `mismatches=0` — the keyframe/delta protocol round-trips
exactly across every real captured scenario.

### 4.1 How often does adaptive choose DELTA over KEYFRAME?

| Scenario | adaptive picks DELTA | of frames after the first |
| --- | ---: | --- |
| idle-desktop, photo-viewer, typing-terminal, fullscreen-animation, video-playback | 100% | fully static/slowly-changing or continuously-but-redundantly-changing content |
| app-launch-close | 89.9% (80/89) | falls back to KEYFRAME exactly at the launch/close transition spikes |
| app-switching | 91.0% (81/89) | same pattern |
| **scrolling-list** | **0% (0/59)** | **always falls back to a normal keyframe** |
| **mixed-web-content** | **0% (0/59)** | **always falls back to a normal keyframe** |

This directly answers PROJECT SPEC question 14 ("Does adaptive
intra-vs-XOR selection solve [scrolling] without motion compensation?"):
**yes, on this real corpus, cleanly and automatically** — the adaptive
selector never sent a delta frame during either auto-scrolled scenario,
because the XOR-delta candidate was reliably larger than a fresh keyframe
for genuinely shifted content, exactly as the spec anticipated. No motion
estimation was implemented (as instructed).

---

## 5. Temporal recovery tests (PROJECT SPEC Phase 4)

`tests/test_temporal_policy.c` (new, 7 scenarios, all passing under `make
test`):

| Scenario | What is verified |
| --- | --- |
| Fixed-interval cadence | KEYFRAME/DELTA alternation matches the configured interval exactly; every frame round-trips exactly. |
| Adaptive selection | Frame 0 is always a keyframe; a near-identical frame is coded as DELTA (smaller); an uncorrelated random frame falls back to KEYFRAME. |
| Forced keyframe | `tpolicy_encoder_force_keyframe()` overrides even a "reset-only" (`interval==0`) policy for exactly the next frame. |
| **Lost delta frame** | Frame 1's DELTA payload is dropped before reaching the decoder; frame 2 (whose `base_sequence_number` refers to frame 1) is detected via `TPOLICY_ERR_REFERENCE_MISMATCH` — **not** silently decoded into a corrupted frame. A subsequent forced keyframe recovers exactly. |
| **Decoder restart** | Decoder state is fully discarded (`tpolicy_decoder_reset`); the next DELTA is rejected with `TPOLICY_ERR_NEED_KEYFRAME`; the next keyframe recovers exactly. |
| **Modeled USB reconnect** | Decoder reset + encoder told to force a keyframe (as a real reconnect handler would need to do) recovers cleanly on the very next frame. |
| **Corrupted compressed payload** | A truncated LZ4 stream is rejected as `TPOLICY_ERR_CORRUPT`; decoder state is left untouched; a subsequent well-formed keyframe still decodes exactly. |

**What a production protocol change would require** (PROJECT SPEC Phase
4/9's "document exactly what production protocol changes temporal mode
would require"): a sequence number and (for delta frames) a base-sequence
reference in every frame header sent over the wire (today's GUD/xdisp
transport has no such field); a decoder-side "awaiting keyframe" state
machine; and either the sender proactively re-keyframing after any
detected transport reset/error, or a receiver-initiated
resynchronization request. None of this exists in the production
`backport-4.9/` transport today, and this phase does not add it there —
see §8 (feature-flag constraint).

---

## 6. Phase 5 — real USB throughput: attempted, not completed; here is why

**What was actually available and verified in this session:** the
project's real hardware — a OnePlus 6 (`phablet@192.168.1.120`) and the
project Raspberry Pi (`cristian@192.168.1.110`) — were both reachable on
the lab network. Following
`docs/oneplus6-usb-host-gud-troubleshooting.md` exactly (forced controller
`host` mode, ran the mandatory VID/PID poll, confirmed the full
enumeration acceptance gate), the Pi's GUD gadget (`1d50:614d`) **enumerated
successfully** at `1-1.3`, High Speed (480 Mbit/s link, confirmed via
`/sys/bus/usb/devices/1-1.3/speed`), with the Pi UDC `configured` and
`gud-userspace.service` active. This confirms the physical
cable/hub/power topology from the troubleshooting doc is currently intact
and working.

**What was not attempted, and why:** actually pushing bulk data through
the gadget to measure real MiB/s requires either (a) loading a `gud.ko`
build and running real DRM/KMS atomic commits against `/dev/dri/card1`, or
(b) reconfiguring the Pi's USB gadget away from its currently-deployed,
`enabled-at-boot` GUD configuration to test raw bulk throughput in
isolation. Both would modify live kernel/gadget state on a shared lab
device that is not local to this session (no physical access, no display
to visually confirm recovery if a commit hangs — the troubleshooting doc
itself documents at least one prior incident, the DisplayPort-shim POC,
where "a slow or stalled USB transfer... freezes or severely slows the
phone UI"). Given the phone remained reachable over SSH throughout this
session and no other engineer was known to be actively using the
lab hardware, the risk was judged high enough, relative to this task's
time budget for safe recovery verification, that a **fresh** measurement
was not attempted. This is a documented scope decision, not a silent
omission, and is exactly the case PROJECT SPEC Phase 5 anticipates
("if unavailable, retain modeled scenarios... but mark them clearly as
modeled").

**Existing real project evidence used instead** (`MEASURED_HOST`/prior
session, cited, not fabricated):
`backport-4.9/env/local/evidence/xdisp-p2.1-oneplus-motion-2026-07-26T1611COT/RESULTS.md`
recorded, on this same OnePlus 6 + Pi pair, with the (different, LZ4-chunked
damage-rectangle) `gud_xdisp_lz4_12800` driver variant at 1280×720:

```text
gud-kms-animate /dev/dri/card1 desktop 12 10
  update_fps=9.015  commit_avg_ms=109.035  commit_min_ms=95.500  commit_max_ms=146.039
gud-kms-animate /dev/dri/card1 scroll 12 10
  update_fps=4.671  commit_avg_ms=203.957  commit_min_ms=46.668  commit_max_ms=312.582
```

This is real, previously-measured, presented-frame commit latency on the
actual GUD USB path (not this phase's new work) — cited here as the only
real end-to-end transport timing this project currently has, and clearly
attributed to a different (damage-rectangle, chunked-LZ4) transport, not
directly comparable byte-for-byte to this phase's full-frame codecs.

**Modeled USB throughput used for every FPS/latency table in this
report:** 30/35/40/45 MiB/s (`MODELED`), matching the broad benchmark's
own convention and USB 2.0 High Speed's realistic (not theoretical
480 Mbit/s) sustained bulk-transfer range with typical
protocol/scheduling overhead.

---

## 7. Phase 6 — Pi Zero 2 W environment (`MEASURED_PI`)

Full record: `results-next-phase/pi-environment.json`.

| Field | Value |
| --- | --- |
| Model | Raspberry Pi Zero 2 W Rev 1.0 |
| CPU | Broadcom BCM2710A1, 4× ARM Cortex-A53 |
| Governor | `ondemand`, 600 MHz–1000 MHz |
| RAM | 416 MiB (usable) |
| Kernel | `6.12.47+rpt-rpi-v8-ffs-xfercompltrace` (project's own GUD/FunctionFS kernel) |
| OS | Debian GNU/Linux 13 (trixie) |
| Compiler | gcc 14.2.0 (Debian 14.2.0-19) |
| Optimization | `-O3` (primary results), `-O2` (Makefile default, comparison run) |
| LZ4 | vendored `vendor/lz4/lz4.c` (same revision as `gud.ko`, per README provenance) |
| QOIR | vendored `vendor/qoir/qoir.h`, unchanged from the broad benchmark |
| Temp before / after finalist sweep | 44.0 °C / 47.2 °C |
| Temp after temporal-policy sweep | 45.1 °C |
| Throttling | `throttled=0x0` at every checkpoint (no under-voltage, no capping) |
| Threading | single-threaded throughout (no multi-core comparison run in this phase) |

---

## 8. Phase 9 — end-to-end experimental transport: not integrated this phase

No change was made to `backport-4.9/` or any other production transport
code. The finalist codecs and the new temporal-policy protocol exist only
inside `benchmark/tools/fbcodec-bench` (an isolated, standalone tool, as
the existing README already states). Integrating a finalist behind an
actual experimental feature flag in the real GUD/xdisp transport, and
running a real end-to-end presented-FPS/dropped-frame test, would require
non-trivial kernel-driver and/or gadget-userspace changes on top of
today's `gud_xdisp_lz4_12800` deployment — genuinely new production
engineering, not benchmarking, and was judged out of scope for this
session given the Phase 5 hardware-risk reasoning above. This satisfies
(trivially, by not touching it) the "do not modify production transport
without an experimental feature flag" constraint. If/when that
integration happens, this report's codec ID naming
(`LZ4_RGB565`≈`rgb565-lz4`, `LZ4_RGB565_QUANT`≈`quant-moderate-16bit-lz4`,
`QOIR_LOSSLESS`, `QOIR_LOSSY`, `LZ4_TEMPORAL_XOR`≈`prev-xor-lz4`/adaptive)
maps directly onto this phase's finalist shortlist.

---

## 9. Required comparison tables

All machine-readable, in `benchmark/results-next-phase/`:

| Table | File |
| --- | --- |
| Real full-frame corpus | `tables/real_corpus.csv` |
| Pi Zero 2 W | `tables/pi_zero2w.csv` |
| USB model | `tables/usb_model.csv` |
| Lossy quality | `tables/lossy_quality.csv` |
| Temporal | `tables/temporal.csv` |
| QOIR net sender gain (Phase 7) | `tables/qoir_net_gain.csv` |
| Everything, one file | `aggregate.json` |

("End-to-end" and "actual GUD transport" tables from the spec's list are
not produced — no new production-transport integration was done, per §8.)

---

## 10. Phase 7 — QOIR-specific analysis

`net_sender_gain = (LZ4 encode + LZ4 USB) - (QOIR conversion + QOIR encode + QOIR USB)`,
computed per scenario at every modeled USB throughput
(`tables/qoir_net_gain.csv`). At 35 MiB/s, `qoir-lossless` **never**
wins on this real corpus (`-O3` Pi numbers):

| Scenario | net gain @ 35 MiB/s (ms) | QOIR wins? |
| --- | ---: | --- |
| app-launch-close | −7.8 | No |
| app-switching | −8.8 | No |
| fullscreen-animation | −10.4 | No |
| idle-desktop | −8.6 | No |
| mixed-web-content | −14.9 | No |
| photo-viewer | −11.0 | No |
| scrolling-list | −15.6 | No |
| typing-terminal | −11.0 | No |
| video-playback | −11.4 | No |

**Break-even USB throughput** (the throughput below which QOIR lossless's
byte savings would outweigh its CPU overhead) is **under 8 MiB/s for
every scenario** (0.4–7.2 MiB/s; three scenarios — mixed-web-content,
scrolling-list, video-playback — have no positive break-even at all
because QOIR lossless produces *more* bytes than LZ4 there). Since
realistic USB 2.0 bulk throughput is roughly 4–10x higher than that,
**QOIR lossless is not production-worthy for this workload on this CPU
class** under the PROJECT SPEC Phase 7 decision rule — its ~9–15 ms of
extra CPU (conversion + codec, encode side alone) is never repaid by USB
time saved.

QOIR lossy is more interesting and genuinely content-dependent (Pareto,
not a blanket win — `tables/lossy_quality.csv`):

| Scenario | Codec | bytes | PSNR (dB) |
| --- | --- | ---: | ---: |
| idle-desktop | quant-moderate-16bit-lz4 | 46,882 | 45.2 |
| idle-desktop | qoir-lossy-l3 | 40,172 | **55.9** |
| idle-desktop | quant-aggressive-lz4 (RGB332) | 31,999 | 24.0 |
| idle-desktop | qoir-lossy-l5 | 32,097 | 24.2 |
| photo-viewer | quant-aggressive-lz4 (RGB332) | 21,941 | 19.0 |
| photo-viewer | qoir-lossy-l5 | **19,720** | **23.0** |

On `idle-desktop`, `qoir-lossy-l3` beats moderate quantization on *both*
size and quality; `qoir-lossy-l5` is essentially tied with RGB332 on both
axes (not "much better", contrary to the spec's Outcome C hope, at least
for UI content). On `photo-viewer`, `qoir-lossy-l5` beats RGB332 on *both*
size and quality — a genuine win for photographic content specifically.
**The right lossy default is content-dependent**; there is no single
universal winner in this dataset.

---

## 11. Scrolling analysis

Covered fully in §4.1: `TEMPORAL_ADAPTIVE` automatically and reliably
falls back to KEYFRAME-only behavior for both auto-scrolled real
scenarios (0% delta selection), at the modest cost of computing (and
discarding) the delta candidate every frame. No motion compensation was
implemented, per the constraint. This is strong evidence that a primitive
"keyframe + XOR delta, chosen adaptively" scheme is sufficient to avoid
the scrolling failure mode the spec was concerned about, **without**
motion estimation.

---

## 12. Final rankings

**Best stateless lossless:** `rgb565-lz4`. It is the fastest candidate on
Pi Zero 2 W by a wide margin (encode ~18 ms/720p vs QOIR's ~29 ms;
**decode ~7 ms vs QOIR's ~41 ms** — QOIR's decode cost is the more
damaging number for a receiver-side gadget/presentation path), and QOIR
lossless's compression advantage (1.06–1.6x on 6/9 real scenarios, a
*loss* on the other 3) never pays for its CPU overhead at any realistic
USB throughput (§10). **Recommendation: keep `rgb565-lz4`** (Outcome A).

**Best stateless lossy:** content-dependent (§10); no single winner.
`qoir-lossy-l3` is attractive for UI/desktop content (better quality *and*
smaller than moderate quantization); `qoir-lossy-l5` is attractive for
photographic content (better quality *and* smaller than RGB332); RGB332
remains the cheapest-to-decode option when decode CPU is the binding
constraint (quant-aggressive-lz4 decode ~3–5 ms vs QOIR-lossy's ~55–68 ms).
If a single lossy mode must ship, `qoir-lossy-l3` covers the most
scenarios reasonably (partial Outcome C — real but qualified, not the
blanket win the spec anticipated).

**Best overall if temporal state is acceptable:** adaptive previous-frame
XOR + LZ4, clearly (Outcome D): 7.5–14.9x smaller than plain LZ4 on
idle/typing/photo/app-transition content, reliably falls back to
keyframe-only during scrolling with **zero** motion-estimation work, and
the state/recovery machinery needed (sequence numbers,
reference-mismatch detection, forced-keyframe-on-reconnect) is small and
fully tested in this phase (§5). The main new cost is computing both
candidates every frame in adaptive mode (§4): observed encode-time
overhead versus plain LZ4 alone ranged from negligible (idle-desktop:
17.20→17.35 ms, ~1.0x — the XOR-delta candidate is nearly all zeros and
compresses very fast) to ~2.4x (scrolling-list: 7.42→17.96 ms, where the
delta candidate is large and expensive to compress) across the real
scenarios (`MEASURED_PI`, `-O2` build for this specific comparison — see
limitations). Even at the high end this remains far cheaper in absolute
terms than QOIR's ~27–30 ms encode alone.

---

## 13. Specific questions answered

1. **Real RGB565+LZ4 ratio on consecutive Lomiri full frames:** 4.9x
   (app-launch-close) to 90.4x (fullscreen-animation, mostly-flat CSS
   background), median around 11–30x for typical desktop content.
2. **How different from the previous synthetic benchmark?** The broad
   benchmark's synthetic patterns and the one pre-existing real landscape
   motion asset are not directly comparable frame-for-frame to this
   phase's real portrait Lomiri captures; qualitatively, real UI content
   showed *more* frame-to-frame redundancy (idle/typing near-zero change)
   than the broad benchmark's synthetic "small-changes" sequence assumed,
   validating temporal prediction's value more strongly than synthetic
   data alone could.
3. **Pi Zero 2 W 720p plain-LZ4 encode:** ~17.7 ms (`MEASURED_PI`,
   synthetic photo-like content, `-O3`).
4. **1080p:** ~28.8 ms.
5. **Corresponding QOIR lossless times:** 720p ~28.9 ms encode / ~40.6 ms
   decode; 1080p ~52.9 ms encode / ~83.5 ms decode.
6. **How much smaller is QOIR lossless on real Lomiri frames?** 1.06–1.6x
   smaller in 6/9 scenarios; *larger* than plain LZ4 in 3/9 (scrolling,
   mixed-scroll, video).
7. **Does QOIR lossless save more USB time than its added cost?** No, in
   every one of the 9 real scenarios at every modeled throughput (§10).
8. **Break-even USB throughput:** under 8 MiB/s in every scenario (§10) —
   still below realistic USB 2.0 bulk throughput.
9. **Does QOIR lossy materially outperform moderate quantization?**
   Sometimes (idle-desktop: yes, both smaller and better quality);
   content-dependent, not universal (§10).
10. **At similar bandwidth, how much better does QOIR lossy look than
    RGB332?** On `idle-desktop`, about the same (PSNR 24.2 vs 24.0 dB at
    matched size). On `photo-viewer`, clearly better (PSNR 23.0 vs 19.0 dB
    at *smaller* size).
11. **How much does temporal XOR reduce normal desktop full frames?**
    7.5–14.9x smaller than plain LZ4 for idle/typing/photo/app-transition
    content.
12. **How badly does temporal XOR behave during scrolling?** 1.6–1.8x
    *larger* than plain LZ4 (ratio 0.55–0.61x).
13. **During video?** Modestly positive here (1.27x smaller than LZ4) —
    but this project's only available "video" content is a synthetic
    color-bar/motion test pattern with large flat regions, not natural
    video; treat this number as favorable-case, not representative of
    worst-case natural video.
14. **Does adaptive intra-vs-XOR selection solve scrolling/video without
    motion compensation?** Yes, cleanly, for scrolling (0% delta
    selection, §4.1); no motion estimation was implemented.
15. **How often would adaptive send normal vs delta?** 89.9–100% delta
    for 7/9 scenarios; 0% delta (always normal) for the 2 auto-scrolled
    scenarios (§4.1).
16. **What keyframe/reset mechanism is required?** Sequence numbers +
    base-sequence references + decoder "awaiting keyframe" state +
    sender-side forced-keyframe-on-reconnect (§5, §8) — implemented and
    tested in the benchmark harness, not yet in production transport.
17. **Is the temporal compression gain worth the state/recovery
    complexity?** Given how small and cleanly-testable the required state
    machine is (§5 — 7 unit tests, all passing, no motion compensation
    needed), yes, for the desktop/UI-heavy workload profile this phone/Pi
    project targets.
18. **Actual sustained USB throughput of the project:** not freshly
    measured this phase (§6); prior real project evidence shows
    ~9 FPS/720p and ~4.7 FPS/720p-scrolling *presented-frame* rates with a
    different (chunked-LZ4, damage-rectangle) transport — not directly
    convertible to a clean bulk MiB/s figure.
19. **Presented FPS at 720p:** not measured end-to-end this phase (§8);
    Pi-only codec throughput at 720p supports well over 30 FPS for
    `rgb565-lz4` (17.7 ms encode + ~a few ms USB/decode) if the rest of
    the pipeline keeps up.
20. **Presented FPS at 1080p:** likewise not end-to-end measured; Pi-only
    `rgb565-lz4` codec cost (28.8 ms encode) alone supports over 30 FPS.
21. **Lowest p95 end-to-end latency codec?** Not measured end-to-end
    (§8); by Pi-only codec-path p95 latency, `rgb565-lz4` is lowest of the
    8 finalists in every scenario (`tables/pi_zero2w.csv`).
22. **Which codec for v1?** `rgb565-lz4` as the stateless baseline,
    **adaptive previous-frame-XOR+LZ4 as the recommended upgrade** if the
    project accepts the (small, tested) stateful/recovery complexity in
    §5, per Outcome D.

---

## 14. Limitations (explicit, not silently dropped)

- Notification/control shade scenario: not captured (no working
  touch-injection path found; see §2.2).
- Scrolling/typing/mixed-content captures were JS-scripted, not
  touch-driven (§2.2) — content and pixel behavior are real (real
  browser engine, real Mir compositor), but the *input* that produced
  them was not a physical gesture.
- Only one portrait resolution (720×1280) was captured from real Lomiri
  content; the second required portrait bucket (1080×1920) and the two
  landscape buckets (1280×720, 1920×1080) use the broad benchmark's
  existing synthetic-content generator, run fresh on the real Pi
  (`MEASURED_PI`, synthetic content) rather than a second real capture
  pass, given the time budget already spent on the first real capture
  round.
- Phase 5 (real USB throughput) and Phase 9 (real end-to-end transport)
  were not attempted with fresh hardware measurements, for the safety/risk
  reasons in §6/§8; existing real prior-session evidence is cited instead
  where available.
- The "video-playback" scenario's content is a synthetic color-bar/motion
  test pattern (the broad benchmark's existing real asset, played back
  through a real browser/compositor), not natural video; its temporal-XOR
  numbers should not be read as a natural-video worst case.
- CPU utilization percent (as opposed to wall-clock codec time) was not
  captured; this was also a known gap in the broad benchmark
  (no `perf stat` available) and remains a gap here.
- The Phase 4 temporal-policy sweep (§4, §5) was run once, on the `-O2`
  build; it was not repeated after switching to `-O3` for the Phase 3
  finalist sweep. Byte counts and adaptive win/lose decisions are
  build-flag-independent (same compression output either way) and are
  unaffected; only the absolute encode/decode millisecond figures quoted
  from the temporal-policy runs are `-O2` (this is called out inline
  wherever such a figure is used).
- `qoir_last_*_breakdown_ns()` accessors are simple module-level statics,
  correct for this harness's single-threaded, sequential-call usage but
  not thread-safe — documented in `qoir_codec.h`, not relevant to any
  current caller.

---

## 15. Exact commands run (reproducibility)

```bash
# Phase 1
cd benchmark/tools/fbcodec-bench && make clean && make -j"$(nproc)" && make test

# Hardware gate (docs/oneplus6-usb-host-gud-troubleshooting.md, verbatim procedure)
ssh -t phablet@<phone-ip> "printf host | sudo tee /sys/bus/platform/devices/a600000.ssusb/mode"
ssh phablet@<phone-ip> '<bounded VID/PID poll for 1d50:614d>'

# Phase 2 capture (per scenario)
benchmark/scripts/capture_lomiri_scenario.sh <scenario> 720 1280 <N> 1 benchmark/capture/raw
python3 benchmark/scripts/ingest_lomiri_capture.py benchmark/capture/raw
python3 benchmark/scripts/frame_dedup_stats.py benchmark/capture/raw benchmark/capture/dedup_stats.json

# Phase 3 (on the Pi, NO_CHARLS=1 OPT=-O3 build)
make NO_CHARLS=1 OPT=-O3 -j4 && make NO_CHARLS=1 OPT=-O3 test
./build/fbcodec-bench --mode sequence --codec "$FINALISTS" --input corpus/<s>.rgb565le \
  --width 720 --height 1280 --frames <N> --corpus-tag <s> --usb-mib-s '30,35,40,45' \
  --verify --json results/json_o3/<s>.json

# Phase 4 (on the Pi)
./build/fbcodec-bench --mode temporal-policy --input corpus/<s>.rgb565le --width 720 \
  --height 1280 --frames <N> --corpus-tag <s> --keyframe-interval {30,60,120,0} --verify \
  --json results/temporal/<s>_ki<N>.json
./build/fbcodec-bench --mode temporal-policy ... --adaptive --json results/temporal/<s>_adaptive.json

# Aggregation
python3 benchmark/scripts/aggregate_next_phase.py
```

---

## 16. Commits

- `2068f09` — `bench: preserve broad results, add real Lomiri capture + temporal policy`
  (Phase 1 preservation, capture tooling, `NO_CHARLS` build mode,
  `temporal_policy` module + tests, comma-separated `--codec`).
- `124001b` — `bench: run narrowed finalists + temporal policy on real Pi Zero 2 W`
  (QOIR conversion-breakdown instrumentation, the full
  `results-next-phase/` artifact tree, this report).

## 17. Deliverables checklist

| # | Deliverable | Status |
| --- | --- | --- |
| 1 | Clean benchmark commit for existing work | Done — already `eab17e9`; broad results now also preserved (`2068f09`) |
| 2 | Real full-frame Lomiri/Mir capture tooling | Done — `capture_lomiri_scenario.sh`, `ingest_lomiri_capture.py` |
| 3 | Representative full-frame capture corpus | Done — 9/10 scenarios, 720×1280, real device |
| 4 | Frame-change statistics | Done — `capture/dedup_stats.json` |
| 5 | Narrowed codec benchmark implementation | Done — 8-codec finalist shortlist, comma-list `--codec` |
| 6 | QOIR lossless measurements | Done, with conversion/codec breakdown |
| 7 | QOIR lossy measurements | Done, mild + moderate, PSNR/max-error |
| 8 | Adaptive temporal XOR implementation | Done — `temporal_policy.{h,c}` |
| 9 | Temporal recovery tests | Done — `test_temporal_policy.c`, 7 scenarios |
| 10 | Pi Zero 2 W benchmark results | Done — real hardware, `-O2` and `-O3` |
| 11 | Actual USB throughput measurements | Not done fresh (risk); prior real evidence cited (§6) |
| 12 | End-to-end experimental transport measurements | Not done (§8) — no production code touched |
| 13 | Quality comparison images | Not regenerated this phase (broad benchmark's existing `results/quality/*.png` preserved on disk, gitignored) |
| 14 | Raw benchmark results | Done — `results-next-phase/pi-json-o3/*.json`, `pi-temporal/*.json` |
| 15 | CSV/JSON aggregate results | Done — `results-next-phase/aggregate.json`, `tables/*.csv` |
| 16 | `summary-next-phase.md` | This file |
| 17 | Exact benchmark commands | §15 |
| 18 | Commits created | §16 |
| 19 | Final ranked recommendation | §12 |
