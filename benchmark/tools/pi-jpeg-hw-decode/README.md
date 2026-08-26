# pi-jpeg-hw-decode

Real Raspberry Pi Zero 2 W `bcm2835-codec-decode` (`/dev/video10`) V4L2
memory-to-memory hardware JPEG decode benchmark, built for the final
codec/transport benchmark (`benchmark/results-final-codecs/`). See
`docs/codec-transport-architecture.md` section 5 and
`benchmark/results-final-codecs/summary-final-codecs.md` for the results
this tool produced.

## Why a purpose-built V4L2 client instead of `v4l2-ctl`

`v4l2-ctl --stream-mmap --stream-out-mmap` (the generic streaming test
path in `v4l-utils` 1.30.1) does not perform the
`VIDIOC_SUBSCRIBE_EVENT` / `V4L2_EVENT_SOURCE_CHANGE` handshake this
stateful M2M decoder needs: verified empirically -- it left the CAPTURE
queue never streamed on (only one `REQBUFS`/`STREAMON` sequence,
corresponding to the OUTPUT queue, ever appeared in `--verbose` trace
output), so no frame was ever decoded. `jpeg_hw_decode_bench.c` performs
the full V4L2 stateful-decoder sequence directly.

## Real hardware constraints discovered

- The Pi's default `gpu_mem=64` split is **not enough** for the
  MMAL/VCHIQ `video_decode` component used by `bcm2835-codec-decode`;
  raising it to `gpu_mem=128` in `/boot/firmware/config.txt` (one
  reboot) was required. See `benchmark/results-final-codecs/environment/pi-zero2w.json`.
- This decoder needs **at least 2** OUTPUT/CAPTURE buffers to make any
  progress; with exactly 1 buffer per queue it never completes a
  single frame.
- 1920x1080 (landscape) did not complete a single frame in this
  session's testing, while the pixel-count-equivalent 1080x1920
  (portrait) worked; see the summary report for the exact numbers and
  the explicit statement that root cause was not isolated.

## Building (native, on the Pi)

```bash
gcc -O2 -Wall -o jpeg_hw_decode_bench jpeg_hw_decode_bench.c
```

## Usage

```bash
./jpeg_hw_decode_bench <device> <jpeg_file> <width> <height> \
    <duration_seconds> <out_json> <pipeline_depth 1..8> \
    [--dump-first-frame <path>]
```

`pipeline_depth` sets the number of OUTPUT/CAPTURE buffers used; `1` is
rejected by this hardware (see above), `2` is the minimum functional
depth, `4`/`8` were used for the pipelined-throughput measurements.
