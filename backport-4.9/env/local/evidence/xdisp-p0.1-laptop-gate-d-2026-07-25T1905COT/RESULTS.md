# XDISP-P0.1 laptop Gate D result

Outcome: **PASS as a lower-size control; the simple full-maxpacket
termination hypothesis is rejected. XDISP-P0.1 remains blocked.**

- Pi boot: `cbaadac2-66a7-4686-ad01-dc278ff27a77`
- Binary SHA-256:
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`
- Gate D drop-in SHA-256:
  `7c2bdf239873c5f01386c408a875e0e43ce3ae5ba4fde54da7dc462f8918711b`
- `GUD_FFS_READ_SIZE=16384`
- `GUD_TEST_COMPRESSION=none`
- `GUD_TEST_MAX_BUFFER_SIZE=11520`
- DWC2: `g_dma=1`, `g_dma_desc=0`

The valid first phase completed one full 1920x1080 frame as 360
1920x3/11,520-byte uncompressed transfers. Each Pi FunctionFS read completed
in one call and returned to `Idle`. The 11,520-byte payloads end with a
256-byte short packet.

KDE then selected 1280x720. This produced six complete frames as 1,080
1280x4/10,240-byte transfers. These are exact 20-packet multiples at the
512-byte high-speed maxpacket, yet every read also completed successfully.
Usbmon records zero bulk transfer flags and no zero-length bulk URB, so neither
`URB_ZERO_PACKET` nor a separate ZLP explains the aligned successes.

Across both modes, usbmon captured 1,440 SET_BUFFER submits and 1,440 matched
bulk submit/completion pairs. Every bulk completion had status zero and its
full submitted length. Latency was 248--695 microseconds, averaging 430
microseconds. The Pi journal independently contains 1,440 `frame_stats` and
1,440 transitions back to `Idle`, with no receive error or poisoned session.

After physical USB detach, the Pi initially retained a stale `configured` UDC
label. This was not an in-flight request: the host had stopped seeing the
device, usbmon stopped receiving traffic, the final receive transition was
`Idle`, and ep1 OUT had an empty request list with `DOEPTSIZ=0`. The controlled
stop then exited zero. Teardown completed through gadget removal, endpoint
owner release, and DRM release. The service ended inactive with
`ExecMainStatus=0`, and the UDC became `not attached`.

The host and Pi kernel journals contain no timeout or transfer error, DWC2
endpoint-stop timeout, Oops, paging fault, panic, or watchdog event. Pstore is
empty and watchdog bootstatus is zero.

Conclusion: an uncompressed transfer ending on a full 512-byte packet is not
by itself sufficient to trigger the failure. The failed aligned lengths remain
15,360 and 61,440 bytes; the newly proven aligned clean length is 10,240 bytes.
Do not proceed with the proposed OnePlus ZLP module on this evidence. The next
no-kernel control is an aligned 12,800-byte/1280x5 Gate E, between the clean
10,240 and failed 15,360 values. A Gate E failure would establish a practical
ceiling at or below 10,240 and keep `g_dma=0` as later root-cause isolation. A
Gate E pass would narrow the clean/failing boundary to 12,800--15,360 but
would still require repetition because the historical 64,000-byte behavior
was intermittent.

The installed Gate D override is no longer active. It is preserved on the Pi
as `/home/cristian/30-xdisp-p0.1-laptop-gate-d.conf.installed-pass`. The
service is disabled/inactive and the UDC is detached.

Canonical records:

- `host-usbmon.pcap`
- `usbmon-summary.txt`
- `host-kernel.log.gz`
- `pi-service-journal-complete.log.gz`
- `pi-kernel-journal-complete.log.gz`
- `pi-final-state.txt`
