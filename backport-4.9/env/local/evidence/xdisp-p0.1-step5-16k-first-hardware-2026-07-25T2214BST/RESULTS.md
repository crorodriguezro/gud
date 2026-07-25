# XDISP-P0.1 Step 5 first 16 KiB hardware result

Date: 2026-07-25

Pi boot ID: `0f3257d9-7d1b-45d6-a3fc-7098f068fda2`

Result: **failed; service poisoned; no restart attempted**

## Preconditions

- The Pi service was already failed/inactive from the separate boot-time
  `set_crtc`/`EACCES` race, so deployment required no stop.
- Auto-start was disabled before installation.
- Deployed `gud-drm`:
  `0c5961daf65a543101bb1727c2c6909a19b5a48ae398cadd94c4c6ebd044b662`.
- Preserved prior binary:
  `/home/cristian/gud-drm.pre-xdisp-p0.1-step5-7053d5b`,
  `7053d5b1cf3f7cc94da776a97f64d3471382df9b8f40b00b511eb9ef3bcf1e12`.
- The containment and read-size drop-ins matched their documented hashes.
- Effective service settings included `Restart=no`, `SendSIGKILL=no`,
  `GUD_FFS_READ_SIZE=16384`, and `RUST_LOG=debug`.
- Manual service start acquired DRM, bound the UDC, and stayed active.
- The first host-only role write did not enumerate the Pi. The documented
  OnePlus `device -> host` role reset did; `1d50:614d` appeared at `1-1.2`,
  `gud.ko` probed, `/dev/dri/card1` appeared, and the Pi reported
  `configured` at `high-speed`.
- The preserved normal OnePlus module hash was
  `bd15c2c1bc4cd941bcac88bb13276b67620d9e2eec515973ff815add68f3630c`.

## Payload result

The isolated RGB565 utility printed `atomic modeset succeeded` and
`PAYLOAD_RC=0`, but the fresh phone kernel log recorded:

```text
gud bulk transfer failed after 0 retries: -110
gud atomic update failed: -110
gud request 0x64 failed: -110
```

The Pi accepted the first 64,000-byte `SET_BUFFER`, entered `InFlight`, and
requested the first 16,384-byte FunctionFS read. That read returned after
476 microseconds with the impossible userspace value
`18446744073709045760`, which is signed `-505856`. The service rejected it as
a non-exact completion, entered `Poisoned`, refused further USB/control work,
and did not tear down the gadget.

The poisoned DWC2 debugfs state made the arithmetic explicit:

```text
configured parameters: g_dma=1, g_dma_desc=0
requested/loaded bytes: 0x4000 = 16384
ep1out DOEPTSIZ:        0x0007f800 = 522240 bytes remaining
unsigned difference:   0x4000 - 0x7f800 = 0xfff84800
signed difference:     -505856
```

This matches the DWC2 buffer-DMA completion calculation
`size_done = size_loaded - size_left + last_load`. FunctionFS then propagated
the invalid `req->actual` value through the synchronous endpoint read.

The Pi remained reachable, its boot ID did not change, the UDC remained
`configured`, and the kernel journal contained no new Oops, DWC2 stop timeout,
allocator warning, or pstore record. The main `gud-drm` thread was parked in
`hrtimer_nanosleep`, confirming that userspace containment worked.

## Conclusions

1. The 16 KiB strategy did not make the first payload reliable. It failed on
   the first of the intended four reads, so no frame tile completed.
2. The result is more specific than the old 512-byte stall: it identifies
   invalid DWC2 buffer-DMA residual/actual-length accounting as the immediate
   failure path.
3. `PAYLOAD_RC=0` is not transfer proof for this asynchronous host update.
   Host kernel `-110` and missing Pi frame completion override it.
4. The Step 5 containment behavior succeeded and prevented the known-risk
   FunctionFS/DWC2/DRM teardown.
5. Do not retry 16 KiB, 64 KiB, or 512 bytes on this boot. Do not stop,
   restart, reboot, or shut down the poisoned service. Recover by physical
   power cycle, hardware reset, or watchdog reset.
6. The next diagnostic is DWC2 gadget DMA isolation with `g_dma=0` on a fresh
   boot. Do not start the three mini-cycles or ten-cycle matrix before that
   experiment has a clean complete-frame and safe-teardown result.

## Raw evidence hashes

```text
42a8844989c37bb1f865febd909bdc801cbcd882f2ed566d1eaa5250f1b0889b  oneplus-dmesg-full.log
b9295fc4a474b7eab77647b9e3410ba5a4cbd97f66d54dedbf69ce7a9b84cf15  oneplus-payload.log
b66a291e45b9d82293934c1c4007ff294bc54f177fb6c1de464b54b8533b208d  pi-after-payload.log
af71b898f5507b3e31752310aa56ccc5f0340f484c0f5b24493333c5cf74c92a  pi-baseline.log
36521e54b92b716333ad86b278e8a7241e7698306401b41142473ca4400bef21  pi-dwc2-debugfs.log
98c8345a03c312e736c405cd05d909fffbdca96d1fde9ab00525f260c5cd422e  pi-poisoned-endpoint-state.log
```
