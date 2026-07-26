# XDISP-P0.1 laptop Gate F result

Outcome: **FAIL, safely contained. Do not repeat unchanged. XDISP-P0.1
remains blocked.**

- Failed Pi boot: `bce5d643-9420-4334-88ed-0d700406f598`
- Recovery Pi boot: `35033b83-ce6e-4bc2-9593-a33f6a95d658`
- Binary SHA-256:
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`
- Gate F drop-in SHA-256:
  `d53946b4d371607acbab5b6be5dc479f1b4856537bf124147091ce810f7d7f1a`
- Normal descriptor defaults: LZ4 enabled and natural framebuffer-sized
  maximum buffer
- Internal `GUD_FFS_READ_SIZE=12800`
- DWC2: `g_dma=1`, `g_dma_desc=0`

## Purpose

Gate E proved that complete 12,800-byte host transfers are repeatable, but its
small advertised maximum required 144 SET_BUFFER operations for each
1280x720 frame and produced visibly unusable cadence. Gate F tested whether
normal large compressed host rectangles could retain normal control cadence
while userspace consumed their bulk payload in multiple reads no larger than
the Gate E boundary.

## First-payload failure

Fresh enumeration succeeded at high speed. The normal laptop driver submitted
one full-screen 1920x1080 LZ4 update:

```text
uncompressed length: 4,147,200
compressed payload:     16,274
first read request:     12,800
```

The first FunctionFS read returned after 395 microseconds with unsigned
`18446744073709042176`, which is signed `-509440`. Live ep1 OUT state retained
`DOEPTSIZ=0x0007f800`, or 522,240 bytes:

```text
12800 - 522240 = -509440
```

The exact equality again identifies DWC2 buffer-DMA residual accounting as
the immediate failure. The service entered `Poisoned`, refused all further
USB/control work, and did not tear down.

Usbmon independently recorded the 16,274-byte bulk submit. The host cancelled
it 3.052803 seconds later with status `-104` and actual length 14,848, after
the DRM driver reported framebuffer-flush `-110`.

## Interpretation

Gate E's 12,800-byte success does not generalize to a 12,800-byte FunctionFS
read that ends before the host bulk transfer. Gate E worked when the complete
host payload and the single userspace read were both 12,800 bytes. Gate F
failed when the host payload continued beyond the first 12,800-byte read.
Therefore the advertised-buffer ceiling and internal read ceiling cannot be
decoupled as a `g_dma=1` userspace performance workaround.

The 12,800-byte descriptor ceiling remains a laptop-qualified correctness
control, but its visible performance is not acceptable as a normal
configuration. Do not proceed to the OnePlus gate or verification matrix with
Gate F, and do not reinstall this drop-in unchanged.

The next root-cause isolation should disable DWC2 buffer DMA on a separately
identified Pi test kernel/configuration. This result increases the value of
`g_dma=0`, but does not authorize starting P0.2 or marking P0.1 verified.

## Recovery

The laptop USB path was physically detached. The poisoned service was never
stopped, restarted, rebooted, shut down, or signalled. Current-boot evidence
was copied before physical reset. The recovery boot retained the previous
boot journals. Both pstore snapshots were empty, watchdog bootstatus was zero,
and neither journal contains a DWC2 endpoint-stop timeout, Oops, paging fault,
or panic.

The failed drop-in is preserved on the Pi as
`/home/cristian/30-xdisp-p0.1-laptop-gate-f.conf.failed-poisoned`. It has been
removed from active systemd configuration. The service is disabled/inactive,
the UDC is detached, and the effective baseline is again
`GUD_FFS_READ_SIZE=16384`.

## Canonical records

- `host-usbmon.pcap`
- `usbmon-summary.txt`
- `host-kernel.log.gz`
- `pi-service-journal-pre-reset.log.gz`
- `pi-kernel-journal-pre-reset.log.gz`
- `pi-service-journal-previous-boot.log.gz`
- `pi-kernel-journal-previous-boot.log.gz`
- `pi-pstore-pre-reset.tar.gz`
- `pi-pstore-after-reset.tar.gz`
- `pi-recovery-final-state.txt`
