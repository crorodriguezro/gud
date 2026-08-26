# fbcodec-bench: RGB565 framebuffer compression benchmark

This is an isolated benchmark harness for evaluating low-complexity RGB565
compression strategies for the GUD USB framebuffer transport, comparing them
against the current RGB565 + LZ4 baseline. It lives entirely under
`benchmark/` and makes **no changes to any production code** in `backport-4.9/`
or elsewhere in the repository.

See `results/summary.md` for the evidence-heavy final report (environment,
corpus, measured tables, break-even analysis, and ranked recommendations),
and `results/results.json` / `results/results.csv` / `results/raw/*.json`
for the underlying machine-readable data.

## Layout

```
benchmark/
  tools/fbcodec-bench/     the C benchmark tool + vendored libraries + tests
    src/                   harness source (codecs, transforms, corpus, report)
    vendor/                lz4 (from the existing gud.ko vendor copy), qoi,
                            qoir, stb_image_write, charls headers
    tests/                 unit tests (round-trip + temporal validation)
    Makefile
  scripts/
    capture_environment.sh generates results/environment.json
    run_benchmark.sh        orchestrates every corpus/resolution/rect sweep
    analyze.py               aggregates raw JSON into results.json/summary.md
  results/
    raw/*.json              one file per fbcodec-bench invocation (full detail)
    quality/*.png            original/reconstructed/diff images for a
                              representative corpus subset
    environment.json
    residual-analysis.json
    results.csv
    results.json
    summary.md
    run_benchmark.log
```

## Building

```bash
cd benchmark/tools/fbcodec-bench
make            # builds build/fbcodec-bench (release, -O2)
make test       # builds and runs build/test_transforms and build/test_temporal
```

Requirements: gcc (or another C11-ish compiler), libm, and the system CharLS
runtime library (`libcharls.so.2`, package `CharLS` on Fedora). No `-devel`
package is required: CharLS's C API headers are vendored from the matching
upstream tag under `vendor/charls/`, and the Makefile links directly against
the versioned shared object (`CHARLS_LIB=/usr/lib64/libcharls.so.2` by
default -- override with `make CHARLS_LIB=-lcharls` if a `-devel` package
providing the unversioned `.so` is installed).

## Running

The full sweep (what produced `results/`):

```bash
cd benchmark
scripts/run_benchmark.sh          # full run (~10-15 minutes)
scripts/run_benchmark.sh --quick  # reduced iterations/corpora smoke pass
python3 scripts/analyze.py        # aggregates results/raw/*.json into
                                   # results/results.json + results/summary.md
```

Running `tools/fbcodec-bench/build/fbcodec-bench` directly:

```bash
# List every registered candidate with its metadata:
build/fbcodec-bench --list

# Benchmark everything on a synthetic pattern:
build/fbcodec-bench --mode frame --codec all --synthetic gradient-2d \
  --width 1280 --height 720 --rect full --iterations 20 --warmup 5 \
  --usb-mib-s 30,35,40,45 --csv results.csv --json out.json

# Benchmark a damage-rectangle-sized crop of a frame:
build/fbcodec-bench --mode frame --codec u16-xor-lz4 --synthetic photo-like \
  --width 1280 --height 720 --rect 128x128 --iterations 20

# Sequence mode (drives stateful/temporal codecs across many frames with
# independent encoder/decoder contexts, and lets stateless codecs be
# compared frame-by-frame on the same content):
build/fbcodec-bench --mode sequence --codec all \
  --synthetic-sequence small-changes --width 1280 --height 720 --frames 60

# Real-asset frame/sequence (already in this repository):
build/fbcodec-bench --mode frame --codec all \
  --input ../../../backport-4.9/env/local/assets/xdisp-motion-1280x720-30fps-10s.rgb565le \
  --width 1280 --height 720 --corpus-tag real-motion

# Damage-stream replay: derives a synthetic damage-rectangle event stream by
# tile-diffing consecutive frames of the real asset, then replays every
# stateless codec against the real per-event sizes/timing:
build/fbcodec-bench --mode damage-stream --codec all \
  --input ../../../backport-4.9/env/local/assets/xdisp-motion-1280x720-30fps-10s.rgb565le \
  --width 1280 --height 720 --frames 60 --tile 32

# Residual/entropy analysis for a single predictor (explains *why* one
# predictor compresses better than another; sections 42-43):
build/fbcodec-bench --mode residual --predictor u16-xor --synthetic photo-like \
  --width 640 --height 360

# --verify: round-trip verification and quality metrics always run; --verify
# additionally makes any lossless round-trip mismatch a fatal (nonzero exit)
# error instead of only being visible via roundtrip_exact in the output:
build/fbcodec-bench --mode frame --codec u16-xor-lz4 --synthetic gradient-2d \
  --width 1280 --height 720 --iterations 5 --verify
```

Key flags: `--codec NAME|all`, `--synthetic PATTERN`, `--input PATH`,
`--width`/`--height`, `--frames`, `--rect WxH or "full"`, `--iterations`,
`--warmup`, `--usb-mib-s "30,35,40,45"`, `--verify`, `--quality-dir DIR`,
`--csv PATH`, `--json PATH`, `--corpus-tag NAME`, `--seed`, `--tile`,
`--frame-interval-ns`, `--predictor NAME`, `--list`.

`--verify` is a boolean flag (no value): round-trip verification (exact
match for lossless codecs) and quality-metric computation always run
regardless of it -- that is mandatory (PROJECT SPEC section 24). Passing
`--verify` additionally makes any lossless round-trip mismatch a fatal
error (nonzero process exit), rather than being visible only via the
`roundtrip_exact` field in the CSV/JSON output.

## Candidates implemented (32 total; `--list` prints live metadata)

**Baselines:** `raw` (A1), `rgb565-lz4` (A2).

**Spatial predictors + LZ4:** `sub-byte-lz4` (B1, PNG-style byte Sub, bpp=2),
`u16-sub-lz4` (B2), `u16-xor-lz4` (B3), `paeth-byte-lz4` / `paeth-pixel-lz4`
(B4a/B4b), `med-lz4` (B5, JPEG-LS-style MED per channel),
`channel-sub-lz4` / `channel-xor-lz4` (channel-aware experiment),
`shuffle-lz4` (S1, lo/hi byte-plane separation).

**Quantization:** `quant-mild-raw` / `quant-mild-lz4` (Q1, R4G5B4-equivalent),
`quant-moderate-16bit-lz4` / `quant-moderate-packed12-lz4` (Q2, "RGB444"),
`quant-aggressive-raw` / `quant-aggressive-lz4` (Q3, RGB332, 1 byte/pixel).

**Quantization + predictor combinations:** `quant-mild-u16sub-lz4`,
`quant-mild-u16xor-lz4`, `quant-moderate-bestpred-lz4` (moderate quant +
u16-XOR, selected as "best simple predictor" per Phase 1/2 measurements),
`quant-shuffle-lz4` (S2).

**Temporal (stateful):** `prev-xor-lz4` (T1), `prev-sub-lz4` (T2),
`prev-quant-xor-lz4` (T3, closed-loop quantized -- see design note below).

**RLE:** `rle-lz4` (PackBits-style preprocessing before LZ4).

**External codecs:** `qoi` (C1, RGB565<->RGB888 conversion measured),
`qoir-lossless` / `qoir-lossy-l3` / `qoir-lossy-l5` (C2), `charls-lossless` /
`charls-near1` / `charls-near3` (C3, JPEG-LS via CharLS).

## Design notes / scoping decisions

* **Pixel buffers** are packed RGB565, little-endian, row-major, stride =
  `width*2` bytes (no padding). Every transform is bounds-safe for
  arbitrary width/height, including 1x1 and odd dimensions (see
  `tests/test_transforms.c`).
* **Temporal codecs (T1-T3)** operate on whole frames with a single
  persistent reference buffer, not on arbitrary damage rectangles that may
  overlap previous updates at different offsets. Exercising true per-region
  temporal state across arbitrary overlapping damage rectangles would
  require substantially more state-tracking complexity (exactly the kind of
  fragile persistent state the project spec asks to be wary of); this
  harness instead evaluates temporal prediction on whole-frame sequences
  (`--mode sequence`) and evaluates realistic damage-rectangle timing with
  **stateless** codecs only (`--mode damage-stream`). This is a deliberate
  scoping decision, documented here and in `results/summary.md`, not a
  silently-dropped requirement.
* **T3 (quantized temporal)** quantizes each *source* pixel independently
  (a pure function of the original pixel, not of the reference), then
  encodes the fully-known quantized target against the reference with a
  lossless XOR + LZ4 step, and updates both encoder and decoder reference
  state to that same quantized target. This is provably drift-free (the
  "residual" being coded is always between two already fully-determined
  values) and is the deliberate simplification allowed by PROJECT SPEC
  section 27 in place of full closed-loop residual quantization.
* **Encoder/decoder state independence:** every stateful-codec benchmark
  (`run_codec_on_sequence` in `src/main.c`, and every scenario in
  `tests/test_temporal.c`) uses two *separate* `temporal_ctx` instances, one
  for encode and one for decode -- exactly like a real sender/receiver pair.
  An earlier version of this harness shared one context between encode and
  decode, which silently masked drift bugs (the decoder would "see" the
  encoder's just-updated reference); this was caught and fixed by the
  round-trip tests before any measurements were taken.
* **USB modeling** (`compute_usb_models` in `src/main.c`) computes
  `usb_transfer_time = encoded_bytes / throughput`, then
  `FPS_usb_only`, `FPS_encode_usb`, `FPS_total_serial` per PROJECT SPEC
  section 21, for each of the throughput values passed via `--usb-mib-s`
  (default `30,35,40,45` MiB/s). These are clearly MODELED numbers, not
  measured USB hardware throughput (no USB hardware is exercised by this
  harness).
* **CPU/perf counters:** `perf stat` is not available in this environment
  (no root to install it, and no pre-installed binary). CPU time is
  therefore reported only via wall-clock `clock_gettime(CLOCK_MONOTONIC)`
  around each encode/decode call, on an otherwise idle single-threaded
  benchmark process. Cycle/instruction/cache-miss counters are not reported;
  this is a known gap relative to PROJECT SPEC section 19; see
  `results/summary.md` for how this affects interpretation.
* **Target hardware:** no Raspberry Pi Zero 2 W (or equivalent ARM target)
  was available. All measurements are host measurements on an Apple
  Silicon (M1 Pro, Asahi Linux, aarch64) development machine -- useful for
  algorithm/candidate selection, but PROJECT SPEC section 30 explicitly
  requires target-hardware confirmation before a final production decision.
  This is called out in every relevant place in `results/summary.md`.

## Tests

```bash
cd tools/fbcodec-bench
make test
```

`tests/test_transforms.c` round-trips every lossless spatial transform on
1x1/2x1/1x2/odd/large/random/solid/gradient buffers plus 25 randomized
property-style trials per transform, checks Q1/Q2 quantization error bounds
against their documented per-channel bit masks, verifies the RGB444/RGB332
packed representations are idempotent, and round-trips the RLE
preprocessing on empty/short/long-run/noisy inputs.

`tests/test_temporal.c` exercises T1/T2/T3 through: a normal in-order
sequence, a dropped update (documents the expected desync, then verifies
recovery via `reset()`), a reordered update (same), and an automatic
periodic keyframe recovering from a corrupted decoder reference.
