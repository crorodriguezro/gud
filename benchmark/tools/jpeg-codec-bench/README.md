# jpeg-codec-bench

Real OnePlus 6 / Raspberry Pi Zero 2 W sender + decode benchmark tooling
for the final codec/transport benchmark
(`benchmark/results-final-codecs/`). See
`docs/codec-transport-architecture.md` and
`benchmark/results-final-codecs/summary-final-codecs.md` for the results
this tooling produced.

## Why this exists (vs. reusing `fbcodec-bench`)

`benchmark/tools/fbcodec-bench` is built around a `uint16_t` RGB565
in-memory representation and a host-only build (its own `README.md`
documents CharLS as "runtime package only, no static/offline build").
JPEG needs a higher-precision (RGB888) source, an explicit
quality/subsampling parameter space, and — critically for the OnePlus 6
— a **fully static, dependency-free** cross-built binary, because the
phone has no on-device C/C++ compiler at all (verified: `gcc`/`cc`/
`clang` absent). Extending `fbcodec-bench`'s existing build/interface
for that would have been a larger, riskier change than a small,
purpose-built tool; see `benchmark/results-final-codecs/summary-final-codecs.md`
for the full rationale.

## Contents

- `src/op6_sender_bench.c` -- OnePlus 6 sender benchmark: RAW RGB565,
  RGB565+LZ4 (current), R4G4B4+LZ4 (fused), RGB332+LZ4 (fused), and
  JPEG Q95/Q90/Q85 (baseline sequential, libjpeg-turbo, NEON). Reads a
  single raw RGBA8888LE frame; writes per-candidate timing JSON and
  (optionally) the raw encoded artifacts for later decode/quality work.
- `src/pi_lz4_decode_bench.c` -- Raspberry Pi Zero 2 W LZ4 decode
  timing, built natively with the Pi's own `gcc` against the identical
  vendored LZ4 source `gud.ko` uses.
- `vendor/lz4.{c,h}` -- copied from `benchmark/tools/fbcodec-bench/vendor/lz4`
  (upstream LZ4 1.10.0, same provenance).
- `vendor/libjpeg-turbo/` -- upstream libjpeg-turbo **3.0.4** source
  (BSD-style license, see `vendor/libjpeg-turbo/LICENSE.md`), vendored
  in full (git history stripped) so the OnePlus 6 sender benchmark can
  be rebuilt fully offline as a static `musl-gcc` binary. Build
  artifacts (`build-musl-static/`) are not committed; see "Building"
  below to regenerate them.

## Building the OnePlus 6 sender benchmark (musl-static, aarch64)

Requires an aarch64 host (or an aarch64 cross toolchain) with
`musl-gcc`/`musl-devel` and `cmake` installed:

```bash
cd vendor/libjpeg-turbo
mkdir -p build-musl-static && cd build-musl-static
cmake -DCMAKE_C_COMPILER=musl-gcc -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
      -DWITH_TURBOJPEG=OFF -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-static" ..
make -j"$(nproc)" jpeg-static
cd ../../..

musl-gcc -static -O2 -Wall -o build/op6_sender_bench \
  -Ivendor/libjpeg-turbo -Ivendor/libjpeg-turbo/build-musl-static \
  src/op6_sender_bench.c vendor/lz4.c \
  vendor/libjpeg-turbo/build-musl-static/libjpeg.a
```

Verify the result is a fully static binary with no dynamic
dependencies before copying it to the phone:

```bash
file build/op6_sender_bench   # ELF ... statically linked
```

## Building the Pi decode benchmark (native)

Run directly on the Raspberry Pi (it has a working native `gcc`):

```bash
gcc -O2 -Wall -o pi_lz4_decode_bench src/pi_lz4_decode_bench.c vendor/lz4.c
```

## Usage

```bash
# OnePlus 6 (over SSH, after copying the binary + a raw RGBA8888 frame):
./op6_sender_bench <scenario>.rgba <width> <height> <iterations> <out.json> [artifact_prefix]

# Pi Zero 2 W (over SSH, after copying an LZ4-compressed artifact):
./pi_lz4_decode_bench <compressed.bin> <decompressed_len_bytes> <iterations> <out.json>
```
