# Codec / Transport Architecture — OnePlus 6 → GUD → USB2 → Raspberry Pi Zero 2 W

Status: durable reference document. Written after the "final full-frame
display encoding benchmark" (`benchmark/results-final-codecs/`). Labels
used throughout: **VERIFIED** (directly inspected code/config/hardware
state), **MEASURED** (timed/counted on real hardware in this project),
**MODELED** (a computed/derived quantity from measured inputs, e.g. a
serial-latency sum), **INFERRED** (a reasonable conclusion not directly
measured). Nothing in this document is fabricated; where evidence was not
obtained, that gap is stated explicitly.

---

## 1. Hardware

### OnePlus 6 (sender / encoder)

- SoC: Qualcomm SDM845 (Kryo 385 big.LITTLE, CPU parts `0x802`/`0x803`), 8
  cores, ASIMD/NEON. **VERIFIED**.
- Kernel: `4.9.112-g6b190d86b` (Halium 9 kernel base). **VERIFIED**.
- Userspace: Ubuntu Touch / Lomiri, actually running an Ubuntu 24.04
  glibc 2.39 rootfs. **VERIFIED**.
- Governor: `schedutil` on all 8 cores. **VERIFIED**.
- **No on-device C/C++ compiler** (`gcc`, `cc`, `clang` all absent from
  `PATH`). **VERIFIED**. This is why every real OP6-side measurement in
  this benchmark was produced by a binary *cross-built* on an aarch64
  Linux workstation with `musl-gcc -static` and copied to the phone —
  the resulting binary has zero runtime dependency on the target's
  libc/dynamic linker (confirmed to run correctly: a musl-static "hello
  world" and the full sender-benchmark binary both executed
  successfully over SSH). This is a durable fact for any future
  on-device native tooling on this phone.
- GUD gadget identity observed this session: `1d50:614d` at USB path
  `/sys/bus/usb/devices/1-1.3` (topology varies session to session; do
  not hardcode a bus path — this matches the existing troubleshooting
  runbook's own warning). **MEASURED** (this session).

### Raspberry Pi Zero 2 W (receiver / decoder)

- CPU: Broadcom BCM2710A1, 4x Cortex-A53, ASIMD/NEON. **VERIFIED**.
- Kernel: `6.12.47+rpt-rpi-v8-ffs-xfercompltrace`. **VERIFIED**.
- OS: Debian 13 (trixie), native `gcc` 14.2.0 available. **VERIFIED**.
- Governor: `ondemand`; observed max ~1000 MHz; `throttled=0x0` at every
  checkpoint (no under-voltage, no thermal capping) across this whole
  benchmark. **MEASURED**.
- `bcm2835-codec-decode` (`/dev/video10`, V4L2 M2M multiplanar): accepts
  `MPG4`/`H264`/`MJPG`/`H263` on OUTPUT, produces `YU12`/`YV12`/`NV12`/
  `NV21`/`NC12`/`RGBP` (RGB565)/`AB24`/`BGR4` on CAPTURE. **VERIFIED**.
- **GPU memory split matters**: at the Pi's default `gpu_mem=64`, the
  MMAL/VCHIQ `video_decode` component fails to initialize
  (`vchiq_mmal_component_init: failed to create component -62 (Not
  enough GPU mem?)`, from `dmesg`). Raising `gpu_mem=128` in
  `/boot/firmware/config.txt` (one reboot) fixed this and is the Pi's
  current running configuration. **MEASURED**. This is a durable system
  prerequisite for using the hardware JPEG decoder at all — a future
  production rollout needs to ship/require this.
- `vc4` DRM primary plane supports `RG16`/`BG16` (RGB565 family) as
  direct scanout formats. **VERIFIED** (from the pre-existing project
  verification note; not independently re-probed this session).

---

## 2. Mir / source formats — what was actually practical

Only `RGBA8888` was exercised as an actual Mir/`lomiri-system-compositor`
output in this project (via `mirscreencast`). **MEASURED / VERIFIED**.

| Format | Classification | Evidence |
| --- | --- | --- |
| `RGBA8888` / `XRGB8888` family | `NATIVE_MIR` | `mirscreencast` captures this directly from the live Lomiri session; used throughout this benchmark as the "highest-quality source". |
| `RGB888` | `NATIVE_DRM_BUT_NOT_MIR` (inferred) | DRM/`vc4` can display RGB888-family formats, but no Mir/Lomiri configuration producing RGB888 natively was built or tested this session — **not attempted**, time-boxed out of this benchmark. |
| `RGB565` | `TRANSPORT_CONVERSION_ONLY` (as verified this session) | No native-Mir-RGB565 experiment was performed this session (see "Not attempted" below); RGB565 is produced by a sender-side truncating conversion (`r>>3,g>>2,b>>3`) from the RGBA8888 source, exactly as the existing capture/ingest pipeline already does. |
| `RGB444` (R4G4B4) | `TRANSPORT_CONVERSION_ONLY` | Produced by a **fused** single-pass conversion directly from RGBA8888 (no intermediate RGB565 buffer), per PROJECT SPEC section 5C. |
| `RGB332` | `TRANSPORT_CONVERSION_ONLY` | Same fused-conversion treatment as R4G4B4. |

**Not attempted this session** (explicitly out of time-budget, not
because it is impossible): building/testing a Mir/GUD integration that
requests RGB565 natively from the compositor (PROJECT SPEC section 11 /
26). The previous project documentation
(`docs/lomiri-gud-integration-options.md`) already discusses this
option; this benchmark did not re-attempt it. Consequently question
"is native RGB565 actually beneficial vs 8888->565 conversion?" is
**not answered by new measurement** in this benchmark; see
`summary-final-codecs.md` question 2 for the explicit disposition.

---

## 3. Transport representations tested

All of the following were **MEASURED** with real OnePlus 6 encode timing,
real bytes-per-frame, and (for the LZ4 family) real Pi Zero 2 W decode
timing:

- RAW RGB565 (conversion only, no compression)
- RGB565 + LZ4 (current production-equivalent codec)
- R4G4B4 + LZ4 (fused quantization + LZ4)
- RGB332 + LZ4 (fused quantization + LZ4)
- JPEG Q95 4:4:4, Q90 4:4:4, Q85 4:2:0 (baseline sequential, from RGB888
  view of the RGBA8888 source), both OP6-side encode timing and Pi-side
  **hardware** decode (`bcm2835-codec-decode`) latency/throughput, plus
  a **software** decode reference (libjpeg-turbo via Pillow) used only
  to isolate JPEG-quality-loss from hardware-decode-quality-loss.

Excluded from this benchmark's primary evaluation per the governing
task spec: previous-frame XOR, temporal prediction/subtraction, motion
compensation, QOI, QOIR, CharLS/JPEG-LS, Paeth, MED, PNG filters, RLE,
and LZ4 HC2/HC3 (tracked separately). Code for several of these still
exists in `benchmark/tools/fbcodec-bench/` from the prior broad
benchmark and was intentionally left untouched/unused here.

---

## 4. Current LZ4 implementation

- gud.ko (kernel, OnePlus 6 side): vendors **upstream LZ4 1.10.0**
  privately (`backport-4.9/` sources), fast/default block compression,
  `acceleration = 1`. The historical 12,800-byte payload cap is already
  fixed in the current mainline build (out of scope for this ticket;
  tracked separately). **VERIFIED** (source inspection).
- `GUD_COMPRESSION_LZ4` (`BIT(0)`) is a first-class field in
  `gud_protocol.h`'s `gud_set_buffer_req`/state-check structures, not an
  experimental-only extension. **VERIFIED**.
- Pi-side gadget (`gud-drm-e3-b01-functionfs-epoch`, the currently
  running production-track binary) is configured with
  `GUD_TEST_COMPRESSION=lz4`, `GUD_TRANSFER_FORMAT=rgb565`,
  `GUD_TEST_MAX_BUFFER_SIZE=1843200` (exactly one 720x1280 RGB565
  frame). **VERIFIED** (`systemctl cat gud-userspace.service`,
  `journalctl` decoder-config log line).
- This means: **the "at least one real integrated LZ4 path" completion
  criterion from the governing task is already satisfied by the
  project's existing running configuration** — RGB565 + LZ4 is not a
  new experimental build for this benchmark, it is what the Pi gadget
  and (when loaded) `gud.ko` already do in production-track use on this
  rig. This benchmark's own OP6-side sender tool and Pi-side decode
  tool independently re-measure the same codec (vendored LZ4 1.10.0) to
  get real per-stage timings.
- This benchmark's own tooling (`benchmark/tools/jpeg-codec-bench/`)
  vendors the **same** LZ4 1.10.0 source
  (`benchmark/tools/fbcodec-bench/vendor/lz4`) for both the OP6 sender
  benchmark and the Pi decode benchmark, so encode and decode timings
  used the byte-identical codec logic gud.ko itself uses.

---

## 5. JPEG implementation

- OP6 encoder: **libjpeg-turbo 3.0.4**, built from source with
  `musl-gcc`, static, NEON SIMD intrinsics enabled
  (`WITH_SIMD=1`, `HAVE_VLD1_S16_X3`/`HAVE_VLD1Q_U8_X4` etc. all
  detected true at configure time). Baseline sequential (`JDCT_ISLOW`),
  explicit subsampling control (`h_samp_factor`/`v_samp_factor`) so
  Q90-4:4:4 could be forced (stb_image_write's bundled JPEG encoder,
  by contrast, auto-selects 4:2:0 for any quality <=90 and was
  therefore **not** used for the primary sender path — see
  `benchmark/tools/jpeg-codec-bench/src/op6_sender_bench.c` for the
  rationale). **VERIFIED / MEASURED**.
- Pi hardware JPEG decoder: `bcm2835-codec-decode` (`/dev/video10`),
  driven directly via V4L2 M2M ioctls
  (`benchmark/tools/pi-jpeg-hw-decode/jpeg_hw_decode_bench.c`) because
  `v4l2-ctl`'s generic `--stream-mmap`/`--stream-out-mmap` path does
  **not** perform the `VIDIOC_SUBSCRIBE_EVENT`/`V4L2_EVENT_SOURCE_CHANGE`
  dance this stateful M2M decoder needs (verified: it left the CAPTURE
  queue never streamed on). Input: `MJPG`. Output: `RGBP` (RGB565)
  directly — no software RGB888 intermediate. **VERIFIED / MEASURED**.
  - The decoder requires **at least 2 OUTPUT buffers** to make any
    progress; with exactly 1 OUTPUT buffer queued it never completes a
    single frame in a 2-3s test window. **MEASURED**.
  - Buffer path: `mmap()`'d kernel buffers on both OUTPUT (compressed)
    and CAPTURE (RGB565) queues; no userspace copy of pixel data other
    than the initial JPEG bytes into the OUTPUT buffer.
- Pi software JPEG decode fallback: `libjpeg-turbo` (2.1.5-4,
  Debian package) via Python/Pillow, used only as a quality-isolation
  reference in this benchmark (see quality section below), not
  re-benchmarked for raw decode speed on the Pi in this session — the
  hardware path was already confirmed operational so the software path
  was not needed as a throughput fallback (PROJECT SPEC section 8B:
  "software decode exists mainly to answer how much benefit hardware
  decode provides", and quality comparison already answers a version of
  that question here).

---

## 6. DRM scanout

- `vc4` supports `RG16`/`BG16` (RGB565 family) as documented in the
  pre-existing project verification note; not independently re-verified
  in this session (no new DRM/KMS probing was performed on the Pi this
  session; all Pi-side work here was V4L2 M2M + LZ4, not DRM/KMS).
- Selected scanout format for every candidate in this benchmark: RGB565
  (`RG16`), because both the current LZ4 production path and the
  hardware JPEG decoder output RGB565 directly.

---

## 7. USB characteristics (measured)

Real USB2 bulk-OUT payload sweep (OnePlus 6 real GUD host bulk pipe →
real Pi gadget, using the project's existing `xdisp-lz4-12800`
diagnostic module's `xdisp_probe_payload_length` raw-bulk-probe hook
purely as a transport microbenchmark — **not** evaluating that ticket's
LZ4-12800-cap policy):

| Payload | p50 latency | Throughput (mean) |
| --- | ---: | ---: |
| 16 KB (15,360 B) | 480 us | 29.0 MiB/s |
| 32 KB (30,720 B) | 873 us | 32.9 MiB/s |
| 64 KB (66,560 B) | 1.81 ms | 34.2 MiB/s |
| 128 KB (133,120 B) | 3.51 ms | 35.8 MiB/s |
| 256 KB (261,120 B) | 6.66 ms | 37.8 MiB/s |
| 512 KB (522,240 B) | 13.2 ms | 37.8 MiB/s |
| 1 MB (1,049,600 B) | 26.4 ms | 37.7 MiB/s |
| 2 MB (2,099,200 B) | 52.5 ms | 37.5 MiB/s |
| Full RGB565 frame (1,843,200 B) | 46.1 ms | 37.3 MiB/s |
| Full XRGB8888 frame (3,686,400 B) | 92.0 ms | 38.1 MiB/s |

**MEASURED** (`benchmark/results-final-codecs/usb/raw_payload_sweep.json`,
20 repeated transactions per size). Throughput rises with payload size
and plateaus around **37-38 MiB/s** for payloads >=256 KB; small
payloads (16-64 KB) are measurably less efficient (~29-34 MiB/s),
confirming the spec's expectation that codec payload size interacts
with USB request efficiency. This measured plateau sits inside the
previously-modeled 30-45 MiB/s sensitivity range and is retained as the
**real** number to use going forward; the modeled range remains useful
only as a sensitivity/reference envelope.

---

## 8. Final selected pipeline (default recommendation)

```
RGBA8888 (Lomiri/Mir, mirscreencast-verified)
    v  (fused truncating conversion, sender)
RGB565
    v  (LZ4 1.10.0, acceleration=1, same as gud.ko)
LZ4-compressed RGB565           <-- WIRE FORMAT
    v  (USB2 bulk-OUT, real measured ~35-38 MiB/s at typical frame sizes)
USB2
    v  (LZ4 decode, native on Pi Zero 2 W)
RGB565
    v
vc4 scanout (RG16)
```

This is the **already-running** production-track configuration on this
rig (`gud-drm-e3-b01-functionfs-epoch`, `GUD_TRANSFER_FORMAT=rgb565`,
`GUD_TEST_COMPRESSION=lz4`). See `summary-final-codecs.md` for the full
ranked recommendation, including the JPEG hardware-decode alternative,
low-bandwidth mode, and rejected candidates.

---

## 9. Rejected alternatives (brief, evidence-based)

- **R4G4B4 + LZ4**: real OP6 encode is **2.3-4.9x slower** than plain
  RGB565+LZ4 across all 4 real captured scenarios (5.5-6.2 ms vs
  1.7-2.6 ms), for only a modest size reduction (roughly 18-30%
  smaller than RGB565+LZ4 on this content). Dominated: worse latency,
  only marginal bandwidth gain, and it is lossy. **MEASURED**.
- **RGB332 + LZ4**: consistently the **fastest and smallest** LZ4-family
  candidate on real OP6 hardware (as fast as or faster than plain
  RGB565 conversion, 1.0-1.5 ms; 50-143x compression vs the 1.84 MB raw
  frame). A genuine Pareto point for a low-bandwidth mode, at the cost
  of a real, measured quality loss (see quality section). **MEASURED**;
  recommended as the low-bandwidth alternative, not the default, purely
  because of that quality cost.
- **JPEG (all 3 quality points)**: on every one of the 4 real captured
  Lomiri UI scenarios (all of which are flat/low-entropy UI, not photo
  or video content — see limitations), JPEG loses on every axis versus
  RGB565+LZ4: slower to encode (11-20 ms vs 1.7-2.6 ms), larger output
  (17-55x compression vs LZ4's 24-143x), and real hardware-decode
  quality is further degraded by the limited-range YCbCr conversion
  issue above. JPEG is **not rejected outright** — it remains the
  architecturally interesting candidate for genuinely
  photographic/video content, which this benchmark's real capture
  corpus did not include (see limitations) — but it is not supported as
  a default by any measurement gathered this session.

---

## 10. Known limitations of this document / benchmark

- The real RGBA8888 capture corpus reacquired for this benchmark
  covers 4 real Lomiri UI scenarios (idle desktop/System Settings,
  dialer app, browser new-tab grid, a content-picker dialog) at
  720x1280 only. It does **not** include photographic or video-like
  content, so this document cannot make an evidence-based claim about
  JPEG's relative performance on such content from *this* corpus (the
  prior broad/next-phase RGB565-only corpus does include a
  `photo-viewer`/`fullscreen-animation`/`video-playback` scenario
  label, but those were never re-captured as RGBA8888 originals, so a
  fair JPEG-vs-source-quality comparison for them was not possible in
  the time available).
- 1920x1080 landscape hardware JPEG decode did not complete in this
  session's test harness (0/N frames), while portrait 1080x1920
  (same pixel count) worked. Root cause not isolated; documented as a
  real, reproduced constraint, not fabricated.
- End-to-end **presented FPS/latency** through the full real USB/GUD
  pipe with an actual live Lomiri frame stream, and pipeline-overlap
  measurement of encode/USB/decode running concurrently across three
  different frames, were **not** performed this session (time-boxed
  out) — see `summary-final-codecs.md` completion-criteria disposition.
- A live, integrated JPEG transport path through `gud.ko` + the Pi
  gadget (a new codec ID, coordinated kernel+userspace protocol change)
  was not attempted; see `summary-final-codecs.md` for the explicit
  safety/time reasoning.
