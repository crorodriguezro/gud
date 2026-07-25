# XDISP-P0.1 modern-laptop 16 KiB live control

## Scope

This is a non-disruptive control observation, not an `XDISP-P0.1` acceptance
cycle. The Raspberry Pi was physically recovered from the prior poisoned boot,
service auto-start remained disabled, and `gud-userspace.service` was started
exactly once from `inactive`. The user then connected the Pi gadget to the
development laptop and used it as a KDE extended monitor at 1920x1080.

No service stop/restart, UDC rebind, display change, payload injection, or
kernel change was performed while collecting this snapshot.

## Systems

- Host: `cristian-macbookpro`
- Host kernel: `7.0.13-400.asahi.fc44.aarch64+16k`
- Host USB controller/driver: xHCI, upstream in-tree `gud` 1.0.0
- Host GUD DRM connector: `/sys/class/drm/card3-USB-2`
- Negotiated USB speed: 480 Mbit/s high speed
- Pi boot ID: `50cf5ffb-6460-4c4c-8044-916a76691f2f`
- Pi service PID: 974, zero restarts
- Pi DWC2: `g_dma=1`, `g_dma_desc=0`
- Pi FunctionFS read ceiling: 16,384 bytes
- Pi UDC: `configured`, `high-speed`

The host kernel log contained only successful enumeration and DRM
registration for `1d50:614d`; it contained no GUD timeout, `-110`, reset,
stall, disconnect, or USB error after enumeration.

## Aggregate result

The Pi persistent service journal from service start through the aggregate
snapshot reported:

```text
frames=953
last_seq=953
compressed=953
total_transfer_bytes=432606856
transfer_bytes_min=16274
transfer_bytes_max=591573
transfer_bytes_avg=453942
read_calls_min=1
read_calls_max=37
read_calls_avg=28.14
recv_ms_min=0
recv_ms_max=21
recv_ms_avg=14.86
total_ms_min=10
total_ms_max=43
total_ms_avg=32.48
read_starts=27053
read_completions=27053
short_reads=0
poisoned=0
impossible_results=0
```

A representative repeated full-screen update was:

```text
payload_seq=739
rect=1920x1080+0,0
transfer_bytes=420584
output_bytes=4147200
read_size=16384
read_calls=26
first_request_bytes=16384
last_request_bytes=10984
compression=1
ratio=9.86
recv_ms=13
decompress_ms=9
copy_ms=4
total_ms=27
usb_mib_s=30.85
```

Its 25 full 16,384-byte reads and exact 10,984-byte tail all completed without
a short read. Sampled full-screen updates generally completed the receive
phase in 13--17 ms and all Pi processing in 27--35 ms. Process lifetime CPU
at the first snapshot was 5.9%, RSS was 7,572 KiB, and the Pi reported
193,484 KiB available memory.

One ten-second wall-clock sample observed 11 completed payloads, but that is a
damage/activity cadence sample rather than a maximum refresh-rate benchmark.
The user subjectively estimated video near 20 frames/s. Per-payload processing
times show service capacity; they do not prove compositor presentation rate.

The only Pi service warning was the known startup
`Failed to flush test pattern: Function not implemented (os error 38)`.
There was no strict kernel Oops, BUG, paging fault, allocator/slab corruption,
DWC2 endpoint-stop timeout, `GOUTNAKEFF`, `EPDisable`, or pstore record.

## Conclusions

1. The configurable 16 KiB FunctionFS receive implementation works with
   `g_dma=1` under sustained traffic from this modern upstream GUD/xHCI host.
   The read size alone does not reproduce the OnePlus failure.
2. DWC2 buffer DMA is not universally broken on this Pi: 27,053 of 27,053
   observed reads completed exactly while transferring 432.6 MB.
3. The OnePlus `-110` and impossible DWC2 residual result are conditional on
   a difference in host, transfer shape, timing, or their interaction. This
   control used upstream Linux 7.0 GUD, xHCI, and LZ4-compressed full-screen
   payloads; the OnePlus test used the Linux 4.9 backport and uncompressed
   64,000-byte tiles.
4. The four-read strategy remains the candidate and the 125-read loop remains
   rejected as a fix. The absence of a noticeable performance change is
   expected because userspace read count does not change the number of USB
   packets and was not shown to be the visible-frame bottleneck.
5. `g_dma=0` remains a useful OnePlus-specific isolation test, but it is no
   longer justified as a general requirement for laptop use. This control
   does not verify `XDISP-P0.1`; the item remains blocked until the defined
   OnePlus acceptance sequence passes.

