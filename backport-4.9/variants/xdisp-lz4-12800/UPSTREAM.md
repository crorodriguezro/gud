# Upstream LZ4 embedding

`gud.ko` embeds the upstream LZ4 block compressor. It does not depend on,
export, or load a separate `lz4.ko` kernel module.

## Provenance

- Repository: `https://github.com/lz4/lz4`
- Pinned revision: `0774d05537f9762f838f7ab541b7765f1a729cb5`
- Vendored source: `vendor/lz4-1.10.0/lz4.c`, `vendor/lz4-1.10.0/lz4.h`
- License: BSD-2-Clause; see `LICENSE.lz4` and the retained notices in both
  source files.

## Linux 4.9 integration boundary

`gud_xdisp_lz4_upstream.c` supplies the kernel memory operations and embeds
the upstream source in freestanding mode. It retains only caller-provided
state: `LZ4_compress_destSize_extState()`. The ordinary APIs that allocate a
16 KiB state frame on the kernel stack are compiled out. All upstream symbols
have internal linkage; `nm -g gud.ko` must contain only the small
`gud_xdisp_lz4_*` wrapper surface.

The wrapper exposes two operations:

- `gud_xdisp_lz4_compress()` accepts only a complete supplied rectangle. A
  partial bounded-output result is rejected.
- `gud_xdisp_lz4_compress_dest_size()` reports the exact source prefix that
  fitted the requested output target.

## Bounded planner safety rule

The opt-in `xdisp_bounded_discovery=1` module parameter uses the latter call
only to discover a candidate. If upstream stops in the middle of a scanline,
the planner rounds the source length down to full RGB565 rows and recompresses
that aligned rectangle against the hard 12,800-byte cap. If it is not
beneficial or fails validation, it sends the largest cap-safe raw row
rectangle instead.

Consequently every actual `SET_BUFFER` bulk payload still passes the existing
adjacent `payload_length <= GUD_XDISP_PAYLOAD_LIMIT` check. The default policy
is unchanged until this opt-in path has hardware evidence.

## First hardware gate

On 2026-07-27, the opt-in policy completed a OnePlus/Pi static-frame gate and
a 300-frame 1280x720 raw RGB565 clip. The clip reached 29.972 paced updates/s
against a 30-fps target, with a maximum actual payload of 12,797 bytes and no
new phone/Pi transport or kernel fault. This establishes only a candidate
bounded-planner result; it does not replace the default policy, prove a
controlled performance gain, or complete XDISP-P0.1. See the committed
evidence record below.

## Gates

Run before hardware deployment:

```sh
backport-4.9/tests/test-xdisp-lz4.sh
SANITIZE=1 backport-4.9/tests/test-xdisp-lz4.sh
make -C backport-4.9 xdisp-lz4-12800
nm -g backport-4.9/variants/xdisp-lz4-12800/gud.ko
```
