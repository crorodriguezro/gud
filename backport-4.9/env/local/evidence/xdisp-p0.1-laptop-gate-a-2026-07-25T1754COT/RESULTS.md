# XDISP-P0.1 laptop Gate A result

Outcome: **PASS as a transfer-shape control; not XDISP-P0.1 verification.**

- Pi boot: `dc5c57e6-9ca0-478d-b1a9-4a5b8a7ece27`
- Binary SHA-256:
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`
- `GUD_FFS_READ_SIZE=16384`
- `GUD_TEST_COMPRESSION=lz4`
- `GUD_TEST_MAX_BUFFER_SIZE=64000`
- DWC2: `g_dma=1`, `g_dma_desc=0`

Usbmon captured 23,580 records with zero packet drops. It contains 5,671
SET_BUFFER submits, 5,671 bulk submits, and 5,671 matching successful bulk
completions. There were no control or bulk completion errors. The capture
contains 15 complete RGB565 1280x720 frames, each split into 28 1280x25
rectangles plus one 1280x20 tail.

All actual compressed bulk URBs across the capture were 131--12,600 bytes;
none reached the 16,384-byte userspace read ceiling. This is the key comparison
against Gate B's first uncompressed 61,440-byte URB.

After physical detach, the controlled stop exited zero. The complete service
journal records teardown through endpoint-owner and DRM release, and the
complete kernel journal contains no DWC2 stop timeout, Oops, paging fault, or
panic.

Canonical records:

- `host-usbmon.pcap`
- `usbmon-summary.txt`
- `pi-service-journal-complete.log.gz`
- `pi-kernel-journal-complete.log.gz`
- `pi-post-detach-state.log`
