# XDISP-P0.1 laptop Gate E result

Outcome: **PASS as the first 12,800-byte boundary run. XDISP-P0.1 remains
blocked pending fresh-boot repetition and an OnePlus gate.**

- Pi boot: `fcb99c5d-ae00-4f79-bf2c-605014469c6b`
- Binary SHA-256:
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`
- Gate E drop-in SHA-256:
  `7a4ee586994a5b6daf7b931aef62a9c74dead75597de4941c620943ae8569fba`
- `GUD_FFS_READ_SIZE=16384`
- `GUD_TEST_COMPRESSION=none`
- `GUD_TEST_MAX_BUFFER_SIZE=12800`
- DWC2: `g_dma=1`, `g_dma_desc=0`

The host first completed one full 1920x1080 frame as 360
1920x3/11,520-byte transfers, repeating the known-clean Gate D shape. KDE then
selected 1280x720, making the Gate E target valid.

The valid target phase completed six full 1280x720 frames as 864
1280x5/12,800-byte uncompressed transfers. Each transfer is exactly 25
high-speed 512-byte packets. Usbmon matched every SET_BUFFER to one
full-length, status-zero bulk completion. No submit had a transfer flag and
there was no zero-length bulk URB.

Across both phases, usbmon captured 1,224 SET_BUFFER submits and 1,224 matched
bulk submit/completion pairs with zero error or mismatch. Bulk latency was
280--1,659 microseconds, averaging 482 microseconds. The Pi journal contains
the same 1,224 `frame_stats` and 1,224 transitions back to `Idle`, with no
receive error or poisoned session.

After physical USB detach, the host stopped seeing the device and usbmon
stopped receiving traffic. The final Pi receive transition was `Idle`, and
ep1 OUT had an empty request list with `DOEPTSIZ=0`. The controlled stop exited
zero and completed gadget removal, endpoint-owner release, and DRM release.

The service ended inactive with `ExecMainStatus=0`, and the UDC became
`not attached`. The host and Pi kernel journals contain no transfer timeout,
DWC2 endpoint-stop timeout, Oops, paging fault, panic, or watchdog event.
Pstore is empty and watchdog bootstatus is zero.

Conclusion: the observed aligned boundary is now 12,800 bytes clean versus
15,360 bytes failed. This is a userspace ceiling candidate, not verification
or root-cause proof. Because the historical 64,000-byte behavior was
intermittent, repeat this exact 12,800-byte gate on fresh Pi boots before
testing it with the unchanged normal OnePlus module. `g_dma=0` remains
deferred as later root-cause isolation.

The Gate E override remains installed while the service is disabled/inactive
so the next fresh-boot repetition uses identical parameters.

Canonical records:

- `host-usbmon.pcap`
- `usbmon-summary.txt`
- `host-kernel.log.gz`
- `pi-service-journal-complete.log.gz`
- `pi-kernel-journal-complete.log.gz`
- `pi-final-state.txt`
