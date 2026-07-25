# XDISP-P0.1 final compressed laptop-session result

Outcome: **PASS as a control; not XDISP-P0.1 verification.**

- Pi boot: `50cf5ffb-6460-4c4c-8044-916a76691f2f`
- FunctionFS read ceiling: 16,384 bytes
- DWC2: `g_dma=1`, `g_dma_desc=0`
- Host: modern upstream GUD/xHCI laptop
- Traffic: LZ4-compressed 1920x1080 updates
- Last completed payload sequence: 4,786
- Short/impossible reads: 0
- Poisoned transitions: 0

The last payload completed and the receive session returned to `Idle`. The
host was physically detached. Its GUD device was absent afterward; the Pi
remained kernel-clean. Although the Pi UDC sysfs state remained `configured`
before service stop, DWC2 was suspended and ep1 had no active transfer. This
was the same stale-session reporting behavior seen on other physical detaches.

The subsequent controlled stop was safe and exited zero. The complete service
journal records UDC unbind, gadget removal, FunctionFS teardown, endpoint-owner
release, then DRM release. Systemd recorded `Deactivated successfully`. The
complete kernel journal contains no DWC2 endpoint-stop timeout, Oops, paging
fault, or panic.

This control proves that sustained 16 KiB-capped FunctionFS reads with DWC2
buffer DMA are not universally broken. It does not exercise the unchanged
OnePlus module and does not count as a mini-cycle or acceptance-matrix entry.

Canonical final records:

- `pi-service-journal-complete.log.gz`
- `pi-kernel-journal-complete.log.gz`
- `pi-pre-stop-state.log`
- `host-kernel.log`
- `host-lsusb-tree-after-detach.log` (empty means the GUD device was absent)
