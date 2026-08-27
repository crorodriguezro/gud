# H.264 USB display hardware POC — 2026-08-27

Outcome: **partial live end-to-end POC**. The complete live hardware path ran,
but sustained unique-frame cadence was 5.23 FPS rather than the 1080p30 target.

## Exercised live path

Lomiri/Mir `mirscreencast` RGBA8888 1920x1080 -> direct Venus `RGB4` input ->
Qualcomm `msm_vidc_venc` H.264 -> bounded FIFO -> USB2 ECM/TCP test interface ->
Pi `bcm2835-codec-decode` (`/dev/video10`) -> DRM PRIME -> VC4 DRM ->
1920x1080@59.94 HDMI.

The ECM function was a temporary, isolated USB-only transport POC (`1d50:6150`)
and was removed after testing. The production `1d50:614d` GUD implementation
was not modified.

## Measured results

- USB enumeration: high speed, 480 Mb/s.
- Exact transport check: 18,732,556 bytes, matching SHA-256
  `c0974f8b749d80ecb7acc02a887b58b781ffa993e1d55c746d82f898ca9bb1d8`.
  Sender wall time 0.903 s: about 166 Mb/s (20.8 MB/s).
- Synthetic Venus encode: 90 1920x1080 NV12 frames, 18.65 FPS, 3.93 Mb/s.
- Live Mir RGBA -> CPU NV12 -> Venus: 90 frames, 9.39 FPS staged.
- Live Mir RGBA direct to Venus `RGB4`: 90 frames, 9.75 FPS staged,
  1.84 Mb/s.
- Fastest simultaneous live run: 90 frames, 17.213 s, 5.23 FPS,
  2,121,552 H.264 bytes, 0.99 Mb/s.
- Pi hardware decode: 90 synthetic frames decoded at about 56-61 FPS;
  89 live frames decoded at about 59 FPS (the test encoder currently omits
  its final drain frame).
- HDMI: VLC selected `drm_prime:yuv420p`, opened `drm_vout`, received first
  picture, and selected 1920x1080@59.94.
- Pi throttling: `throttled=0x0`.
- Receiver live pacing: 150 ms configured cache; observed PCR jitter ranged
  from tens of milliseconds to about 1 second.

## Experimentally established blockers

- Phone Venus is available and functional; absence of hardware encoding is
  not a blocker.
- `gst-hybris` on the phone is decoder-only and skips encoder codecs.
- Venus's vendor V4L2 ABI rejects MMAP and standard DMABUF queues. It requires
  ION-backed `V4L2_MEMORY_USERPTR` with fd in `plane.reserved[0]`.
- Pi hardware decode is not the throughput blocker (~59 FPS at 1080p).
- USB is not the throughput blocker (20.8 MB/s exact transfer in this POC).
- Current cadence is limited by full-frame `mirscreencast` stdout copies,
  one-frame-at-a-time Venus queueing, and VLC raw-stream pacing/buffer stalls.
  A production candidate needs direct Mir buffer integration, asynchronous
  multi-buffer Venus queueing, explicit timestamps/framing, and a receiver
  loop that controls V4L2/DRM directly.
