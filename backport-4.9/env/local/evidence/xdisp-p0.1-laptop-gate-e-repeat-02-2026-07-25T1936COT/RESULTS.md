# XDISP-P0.1 Gate E fresh-boot repeat 2

Outcome: **PASS. The 12,800-byte ceiling is laptop-qualified across three
fresh boots. XDISP-P0.1 remains blocked pending the OnePlus gate.**

- Pi boot: `fb058097-688b-43b6-8854-e609ef0cef94`
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

Together with Gate E run 1 and repeat 1, this qualifies 12,800 bytes as the
userspace ceiling candidate for one unchanged-normal-module OnePlus frame and
safe restart. It does not verify XDISP-P0.1 or prove the DWC2 root cause.

The Gate E override has been removed from active Pi configuration and
preserved as
`/home/cristian/30-xdisp-p0.1-laptop-gate-e.conf.laptop-qualified-3-boots`.
