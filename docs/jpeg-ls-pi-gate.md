# JPEG-LS Pi go/no-go gate

This is a codec-selection gate only; it does not change the USB/GUD host-mode setup tracked in `gud/docs/oneplus6-usb-host-gud-troubleshooting.md` and it does not modify production GUD/gadget code.

## Measured evidence

This was validated on the actual Raspberry Pi target by running the project benchmark harness on the Pi itself (`~/fbcodec-bench`), with `libcharls-dev` installed and the benchmark rebuilt with CharLS enabled.

Measured on the real Pi using the `photo-viewer.rgb565le` 720x1280 corpus (20 iterations, 5 warmup):

- `rgb565-lz4`: decode mean 3.42 ms
- `charls-lossless`: decode mean 96.16 ms
- `charls-near1`: decode mean 121.27 ms
- `charls-near3`: decode mean 95.70 ms

This is a full hardware result on the actual target, not a host-only extrapolation.

## Gate rule

From the benchmark brief:

- ~4–8 ms decode: KEEP
- ~8–15 ms: KEEP only if compression improvement is substantial
- ~20+ ms: DROP unless results reveal an exceptional bandwidth advantage

## Recommendation

The actual Pi hardware measurement is decisively in the DROP band for every JPEG-LS variant. Even the best JPEG-LS decode time on the real target (~95.7 ms) is roughly 28× slower than the current `rgb565-lz4` baseline (~3.4 ms), while the encode path is also far more expensive on the Pi. The compression gain is not sufficient to justify the decode cost for the real Pi Zero 2 W workload.

Decision: DROP JPEG-LS for the Pi Zero 2 W full benchmark.
