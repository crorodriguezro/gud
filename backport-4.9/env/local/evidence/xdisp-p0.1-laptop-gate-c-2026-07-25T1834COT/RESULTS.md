# XDISP-P0.1 laptop Gate C result

Outcome: **FAIL, safely contained. XDISP-P0.1 remains blocked.**

- Failed Pi boot: `8dfb3cfc-c959-4c57-9d1f-04cfcab3755c`
- Recovery boot: `cbaadac2-66a7-4686-ad01-dc278ff27a77`
- Binary SHA-256:
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`
- `GUD_FFS_READ_SIZE=16384`
- `GUD_TEST_COMPRESSION=none`
- `GUD_TEST_MAX_BUFFER_SIZE=15360`
- DWC2: `g_dma=1`, `g_dma_desc=0`

The first SET_BUFFER requested a 1920x4 RGB565 rectangle of exactly 15,360
bytes. Usbmon captured its single bulk URB completing successfully with
`status=0` and `actual_length=15360` after 774 microseconds.

The Pi's matching single 15,360-byte FunctionFS read never returned. Live DWC2
state showed the request still in progress with zero bytes completed and
`DOEPTSIZ=0x00200000`: transfer-size residual zero and packet-count residual
four. The request remained in flight after physical USB detach. The host later
reported framebuffer flush `-110`.

Containment prevented teardown. The service was never stopped, restarted,
rebooted, shut down, or signalled in the failed boot. Evidence was collected
before and after USB detach, then the Pi was physically reset. The previous
boot journal ends at read start; no teardown ran. Both pstore archives are
empty, watchdog bootstatus is zero, and there is no DWC2 endpoint-stop timeout,
Oops, paging fault, or panic.

The installed Gate C drop-in was quarantined as
`/home/cristian/30-xdisp-p0.1-laptop-gate-c.conf.installed-failed` with
SHA-256
`e7de946e8c76b3af578c8470a211d9d8ef0ef347ca208111759cd8d2241ee9cb`.
It is absent from `/etc/systemd/system` and must not be reinstalled unchanged.

Conclusion: this is not a simple large-transfer threshold. Gate A passed 5,671
compressed bulk transfers whose lengths were all nonmultiples of 512. Gate B
failed at 61,440 bytes and Gate C failed at 15,360 bytes; both are exact
multiples of the 512-byte high-speed maxpacket. Gate C additionally proves
that the host can complete every requested byte while the Pi read remains
blocked. This strongly implicates DWC2 buffer-DMA/FunctionFS OUT completion
when a payload ends on a full maxpacket without a short packet or ZLP.

The next no-kernel-build control is Gate D: keep compression disabled but use
an 11,520-byte 1920x3 rectangle, whose final USB packet is 256 bytes. A Gate D
pass would isolate full-maxpacket termination and justify testing a separately
named OnePlus diagnostic module that requests a zero-length packet for aligned
bulk writes. A Gate D failure would make alignment insufficient and promote a
Pi `g_dma=0` test kernel to the next isolation.

Canonical records:

- `host-usbmon.pcap`
- `usbmon-summary.txt`
- `pi-dwc2-state-inflight.log`
- `pi-dwc2-state-post-detach.log`
- `pi-service-journal-inflight.log.gz`
- `pi-kernel-journal-inflight.log.gz`
- `pi-service-journal-previous-boot.log.gz`
- `pi-kernel-journal-previous-boot.log.gz`
- `pi-recovery-state.log`
- `pi-pstore-inflight.tar.gz`
- `pi-pstore-after-reset.tar.gz`
