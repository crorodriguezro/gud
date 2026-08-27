# Upstream-aligned LZ4 integration

`gud.ko` embeds an upstream LZ4 block compressor because the target Linux 4.9
kernel does not export one. It does not depend on or export a separate LZ4
kernel module.

## Provenance

- Repository: `https://github.com/lz4/lz4`
- Revision: `0774d05537f9762f838f7ab541b7765f1a729cb5`
- Files: `vendor/lz4-1.10.0/lz4.c`, `vendor/lz4-1.10.0/lz4.h`
- License: BSD-2-Clause; see `LICENSE.lz4`

The wrapper uses caller-provided compression state and keeps all upstream LZ4
symbols local to the module.

## GUD semantics

The E4-T07 path mirrors current upstream GUD:

1. The device descriptor's `max_buffer_size` determines bulk capacity.
2. An update larger than that capacity is split on complete scanlines before
   compression.
3. Each resulting rectangle is compressed once with output capacity equal to
   its raw length.
4. A failed or non-beneficial compression attempt sends the same rectangle
   raw.
5. One rectangle produces one logical `SET_BUFFER` plus bulk payload.

There is no bounded-prefix discovery, row-fit loop, ratio cache, predictive
policy, target percentage, or 12,800-byte submission guard. The historical
directory name remains only so existing build and evidence scripts can locate
the artifact while E4-T07 is qualified.

## Linux 4.9 / OnePlus delta

Current upstream sends a scatter-gather bulk request. The OnePlus 6 Linux 4.9
xHCI path instead requires a DMA-coherent bounce allocation submitted through
an explicit URB with `URB_NO_TRANSFER_DMA_MAP`; using `usb_bulk_msg()` caused
the USB core to remap the coherent allocation and fail with `-EAGAIN`.

The different host USB mechanism does not change GUD framing: internal USB or
gadget request chunking never creates additional `SET_BUFFER` transactions.

## Async flush

The optional `async_flush` path also follows upstream's single-shadow,
single-framebuffer-reference, bounding-damage, single-work-item design and
defaults off. Linux 4.9 has no `drm_dev_enter()`/`drm_dev_exit()` pair, so the
backport explicitly marks disconnect and synchronously cancels work before DRM
or device teardown. That lifetime adaptation and the OnePlus coherent-URB
transport are the intentional platform deltas; they do not add a frame queue.

## Offline gates

```sh
backport-4.9/tests/test-xdisp-lz4.sh
SANITIZE=1 backport-4.9/tests/test-xdisp-lz4.sh
backport-4.9/tests/test-xdisp-lz4-contract.sh
backport-4.9/tests/test-xdisp-async-contract.sh
make -C backport-4.9 xdisp-full-update
```
