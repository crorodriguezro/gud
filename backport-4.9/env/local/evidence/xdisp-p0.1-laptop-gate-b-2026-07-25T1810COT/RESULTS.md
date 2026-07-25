# XDISP-P0.1 laptop Gate B result

Outcome: **FAIL, safely contained. XDISP-P0.1 remains blocked.**

- Failed Pi boot: `dbe611fc-4b49-4fde-b337-04bb19edd3c3`
- Recovery boot: `8dfb3cfc-c959-4c57-9d1f-04cfcab3755c`
- Binary SHA-256:
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`
- `GUD_FFS_READ_SIZE=16384`
- `GUD_TEST_COMPRESSION=none`
- `GUD_TEST_MAX_BUFFER_SIZE=64000`
- DWC2: `g_dma=1`, `g_dma_desc=0`

KDE restored 1920x1080 before the intended 1280x720 mode was committed. The
first complete-row SET_BUFFER was therefore 1920x16/61,440 bytes. This remains
a valid and stronger failure-classification result: it reproduced the
impossible DWC2 residual with the laptop's upstream GUD/xHCI host, without the
OnePlus backport, and below the exact 64,000-byte value.

The first SET_BUFFER control transfer completed. Usbmon shows one 61,440-byte
bulk URB submission. It completed 3.033326 seconds later with status `-104`
(`ECONNRESET`) and actual length 18,432 after the host timed out the flush with
`-110`.

The Pi requested 16,384 bytes from FunctionFS and received unsigned
`18446744073709045760`, signed `-505856`. Its preserved ep1 OUT state reported
`DOEPTSIZ=0x0007f800`, or a 522,240-byte transfer residual:

```text
16384 - 522240 = -505856
```

The exact equality ties the userspace result to DWC2 residual accounting,
rather than to an absent or short host submission.

Containment changed the receive session to `Poisoned`, refused further
USB/control processing, ignored automatic teardown, and kept the Pi readable.
Only the USB data cable was physically detached. Evidence was copied before
the Pi was physically reset; the service was never stopped, restarted,
rebooted, shut down, or signalled in the poisoned boot.

The failed and recovery kernel journals contain no endpoint-stop timeout,
Oops, paging fault, or panic. Pre-reset and post-reset pstore archives are
empty. Watchdog bootstatus is zero. After recovery, the service was
disabled/inactive with `MainPID=0`, and the UDC was `not attached`.

The installed Gate B drop-in was quarantined as
`/home/cristian/30-xdisp-p0.1-laptop-gate-b.conf.installed-failed` with
SHA-256
`246f4d89a78b7e0b92f1ca37e43d80fa91fdd711fbf42873f67716febe77050e`.
It is absent from `/etc/systemd/system` and must not be reinstalled for an
unchanged test.

Conclusion: tiling/control cadence alone and the OnePlus backport are not
necessary triggers. Larger uncompressed FunctionFS/DWC2 buffer-DMA OUT
behavior is implicated. The next no-kernel-build gate starts at a
packet-aligned, resolution-independent 15,360-byte maximum.

Canonical records:

- `host-usbmon.pcap`
- `usbmon-summary.txt`
- `host-kernel.log.gz`
- `pi-dwc2-state-pre-reset.log`
- `pi-service-journal-pre-reset.log.gz`
- `pi-kernel-journal-pre-reset.log.gz`
- `pi-service-journal-previous-boot.log.gz`
- `pi-kernel-journal-previous-boot.log.gz`
- `pi-recovery-state.log`
- `pi-pstore-pre-reset.tar.gz`
- `pi-pstore-after-reset.tar.gz`
