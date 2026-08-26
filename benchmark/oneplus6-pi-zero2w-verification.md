# OnePlus 6 → GUD → Pi Zero 2 W pre-benchmark verification

Verified on 2026-08-26.

## Observed environment

| Device | Kernel | CPU / ISA | Governor | Notes |
| --- | --- | --- | --- | --- |
| OnePlus 6 | 4.9.112-g6b190d86b | SDM845 Kryo-3XX Gold/Silver, aarch64, `asimd` present | `schedutil` | `libjpeg-turbo8` and `libturbojpeg0` installed; `/dev/dri/card0` is the Android/Mir display card. |
| Raspberry Pi Zero 2 W | 6.12.47+rpt-rpi-v8-ffs-xfercompltrace | Cortex-A53, aarch64, `asimd` present | `ondemand` | `throttled=0x0`; current `cpu-thermal` was 40.78 C. |

## Verified pipeline facts

- The current Lomiri capture path on the OnePlus uses `mirscreencast` and emits raw RGBA8888 frames.
- The benchmark ingest step in [ingest_lomiri_capture.py](</home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/scripts/ingest_lomiri_capture.py>) converts that RGBA8888 source once into packed RGB565LE.
- The stored benchmark corpus is therefore RGB565LE, row-major, with stride `width * 2` bytes and no padding.
- The fair common starting buffer for the codec benchmark is the RGB565LE corpus, not the raw RGBA source stream.

## USB / GUD evidence

- The Pi gadget enumerates as `1d50:614d` (`Generic USB Display`) at high speed, bulk endpoint 1 OUT.
- The Pi-side gadget identity in [usb-devices.txt](</home/cristianr/Projects/linux-mobile/gud-benchmark/backport-4.9/env/local/pi-usb/usb-devices.txt>) is `3-1.3`.
- The OnePlus host-side gate still must be re-run on the physically cabled rig before any KMS or codec benchmark.

## JPEG and scanout capability on the Pi

- The Pi has `libjpeg62-turbo` and `libturbojpeg0` installed.
- `/dev/video10` (`bcm2835-codec-decode`) accepts `MJPG` input and can output `RGBP` (RGB565), `AB24`, `BGR4`, and YUV formats.
- The Pi `vc4` DRM primary plane supports `RG16` and `BG16`, so RGB565 can scan out directly.
- Result: a JPEG candidate can decode in hardware on the Pi and land in RGB565 without an extra CPU color-conversion step on the display side.

## Answers to the primary questions

1. **Pixel format/stride on the OnePlus path:** RGBA8888 from `mirscreencast`; benchmark storage uses RGB565LE with `stride = width * 2`.
2. **Where RGB565 conversion occurs:** in [ingest_lomiri_capture.py](</home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/scripts/ingest_lomiri_capture.py>), once at capture ingestion.
3. **Common starting buffer:** packed RGB565LE frames.
4. **Can libjpeg-turbo consume the OnePlus source directly?** Yes for the current RGBA8888 capture source.
5. **Can libjpeg-turbo on the Pi decompress directly to RGB565?** Yes; the installed TurboJPEG runtime is the right library family, and the Pi display path also accepts RGB565.
6. **Final Pi display/scanout format:** RGB565 is supported directly (`RG16`/`BG16`).
7. **Usable hardware JPEG/MJPEG decoder?** Yes, via `bcm2835-codec-decode`.
8. **Accepted/output formats on that decoder:** `MJPG` input; `RGBP`, `AB24`, `BGR4`, and YUV outputs.
9. **Can hardware JPEG output feed display directly?** Yes, if output is `RGBP`/`RG16`.
10. **Required conversions:** RGBA8888 → RGB565LE at ingest on the OnePlus capture side; JPEG can avoid a second RGB conversion on the Pi when outputting RGB565.
11. **LZ4 implementation used on each side:** OnePlus host-side GUD vendors upstream LZ4 1.10.0 privately; the benchmark harness uses its vendored LZ4 copy under [benchmark/tools/fbcodec-bench/vendor/lz4](</home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/tools/fbcodec-bench/vendor/lz4>).
12. **Active CPU/library baseline:** see the table above; the Pi currently has `libjpeg62-turbo` 2.1.5-4 and `libturbojpeg0` 2.1.5-4.

## Update: final codec/transport benchmark (real hardware, this session)

The pre-benchmark verification above was confirmed and extended by the
final-codec benchmark (`benchmark/results-final-codecs/`). New facts
established with real hardware that materially refine or correct the
statements above:

- **VERIFIED, refined:** the Pi gadget enumerated as `1d50:614d` at
  `/sys/bus/usb/devices/1-1.3` on the OnePlus 6 this session (not `1-1.4`
  or `3-1.3` from earlier sessions), confirming the existing doc's own
  warning not to hardcode a USB topology.
- **MEASURED, new:** `bcm2835-codec-decode`'s MMAL/VCHIQ `video_decode`
  component fails to initialize at the Pi's default `gpu_mem=64` split
  (`dmesg`: "vchiq_mmal_component_init: failed to create component -62
  (Not enough GPU mem?)"). Raising `gpu_mem=128` in
  `/boot/firmware/config.txt` (one reboot) fixed this; this is now the
  Pi's running configuration (see `environment/pi-zero2w.json`). This
  was not previously verified/documented anywhere in this repository.
- **MEASURED, new:** with `gpu_mem=128`, real repeated (10s, independent
  JPEG frames) hardware decode via `/dev/video10` sustains roughly
  **40-49 FPS at 1280x720 and 720x1280**, but **stalls indefinitely (0
  completed frames) at 1920x1080** in this exact test configuration
  while succeeding at the pixel-count-equivalent **1080x1920** (~18
  FPS); see `benchmark/results-final-codecs/isolated/pi/jpeg-hw-decode/`.
  This is a real, reproduced limitation, not a modeled one; root cause
  not fully isolated (see `summary-final-codecs.md` limitations).
- **MEASURED, new:** the previous doc's claim ("JPEG candidate can
  decode in hardware...without an extra CPU color-conversion step") is
  correct mechanically, but real interop testing (feeding actual OP6-
  encoded JPEG bytes through the Pi's hardware decoder) shows the
  resulting RGB565 has a measurable quality gap versus a software
  (libjpeg-turbo) decode of the *same* JPEG bytes -- PSNR ~24-26 dB
  (hardware) vs ~40-51 dB (software), most visible as crushed dark
  tones, consistent with the firmware applying a limited-range YCbCr
  conversion to full-range JFIF input. See `quality/quality.json` and
  `summary-final-codecs.md`.
- **MEASURED, new:** real USB2 bulk-OUT throughput (raw payload probe,
  actual OnePlus 6 -> Pi Zero 2 W cable/hub, `1d50:614d`) rises from
  ~29 MiB/s at 16 KB payloads to a ~37-38 MiB/s plateau at >=256 KB,
  confirming the previously-modeled 30-45 MiB/s sensitivity range
  bounds real behavior. See `usb/raw_payload_sweep.json`.
- **MEASURED, new:** real OnePlus 6 sender timings (no on-device
  compiler; a statically-linked `musl-gcc` binary was cross-built and
  executed on-device) for RAW RGB565, RGB565+LZ4, R4G4B4+LZ4 (fused),
  RGB332+LZ4 (fused), and JPEG Q95/Q90/Q85 are now available in
  `isolated/op6/*.sender.json` -- the first real (non-workstation-only)
  OP6-side encode measurements for this project.

## Source anchors

- [docs/oneplus6-usb-host-gud-troubleshooting.md](</home/cristianr/Projects/linux-mobile/gud-benchmark/docs/oneplus6-usb-host-gud-troubleshooting.md>)
- [benchmark/scripts/capture_lomiri_scenario.sh](</home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/scripts/capture_lomiri_scenario.sh>)
- [benchmark/scripts/ingest_lomiri_capture.py](</home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/scripts/ingest_lomiri_capture.py>)
- [backport-4.9/env/local/pi-usb/lsusb-v.txt](</home/cristianr/Projects/linux-mobile/gud-benchmark/backport-4.9/env/local/pi-usb/lsusb-v.txt>)
- [backport-4.9/env/local/pi-usb/usb-devices.txt](</home/cristianr/Projects/linux-mobile/gud-benchmark/backport-4.9/env/local/pi-usb/usb-devices.txt>)
