# XDISP-P0.1 Gate E fresh-boot repeat 1

Outcome: **PASS. XDISP-P0.1 remains blocked.**

- Pi boot: `a26ca875-aef8-4b28-abae-25e2d3205ed4`
- Compression: none
- Maximum buffer: 12,800 bytes
- DWC2: `g_dma=1`, `g_dma_desc=0`

Usbmon matched 1,224/1,224 full-length, status-zero bulk transfers. The valid
target phase contained six complete 1280x720 frames as 864 aligned
1280x5/12,800-byte transfers. The preceding 360 transfers were one complete
1920 frame in the already clean 11,520-byte shape.

The Pi recorded 1,224 completed payloads and zero receive errors. After
physical detach, the last session state was `Idle`, ep1 OUT had no queued
request and `DOEPTSIZ=0`, and controlled stop exited zero. The service ended
inactive with `ExecMainStatus=0`; the UDC was `not attached`. Kernel journals
contain no DWC2 stop timeout or Oops, pstore is empty, and watchdog bootstatus
is zero.

This is the first of two required fresh-boot repeats of the 12,800-byte
ceiling candidate.
