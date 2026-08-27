# Venus V4L2 capability probe

This dependency-free probe is intended to be statically built on an AArch64
Linux machine and copied to the OnePlus 6. It inventories the actual formats
and controls exposed by the Halium 9 Venus encoder (`/dev/video33`) without
requiring `v4l2-ctl` on the phone.

```sh
cc -O2 -Wall -Wextra -static -o venus-v4l2-probe venus_v4l2_probe.c
./venus-v4l2-probe /dev/video33
```

`venus_h264_encode.c` is the exercised encoder POC. The Halium 9 Qualcomm
driver requires CAPTURE format negotiation before OUTPUT, legacy ION buffers,
`V4L2_MEMORY_USERPTR`, the mapped virtual address in `plane.m.userptr`, and the
ION dma-buf fd in `plane.reserved[0]`. Run it as root because the phone's
`/dev/ion` is not writable by `phablet`.

It accepts generated NV12 by default, converted Mir RGBA with `--rgba-stdin`,
or direct Venus `RGB4` input with `--rgba-direct`. The optional final argument
is the frame count.

```sh
mirscreencast -m /run/mir_socket -n 90 -s 1920 1080 \
  --cap-interval 2 --stdout |
sudo ./venus-h264-encode /dev/video33 /tmp/mir.h264 --rgba-direct 90
```
