# XDISP-P0.1 12,800-byte laptop qualification

Outcome: **PASS across three fresh Pi boots.**

| Run | Pi boot | Target frames | Target transfers | URB/read errors | Controlled stop |
| --- | --- | ---: | ---: | ---: | --- |
| Gate E run 1 | `fcb99c5d-ae00-4f79-bf2c-605014469c6b` | 6 | 864 | 0 | exit 0 |
| Repeat 1 | `a26ca875-aef8-4b28-abae-25e2d3205ed4` | 6 | 864 | 0 | exit 0 |
| Repeat 2 | `fb058097-688b-43b6-8854-e609ef0cef94` | 6 | 864 | 0 | exit 0 |

Each run used:

- compression disabled;
- `max_buffer_size=12800`;
- 16,384-byte FunctionFS read ceiling;
- DWC2 `g_dma=1`, `g_dma_desc=0`;
- the same Pi binary SHA-256
  `cd0995c99a1ab6ae5f073fd1ef86684e45f909fbc9228eeb7706a39c3b17189d`.

Each target frame was 1280x720 RGB565 split into 144
1280x5/12,800-byte transfers, exactly 25 high-speed packets each. Across the
three target phases, 2,592 target transfers completed without one host URB
error, length mismatch, Pi read anomaly, poisoned session, or kernel fault.
All three controlled stops exited zero after an empty ep1 request list and
completed gadget/FunctionFS/DRM teardown.

Decision: 12,800 bytes is the laptop-qualified userspace ceiling candidate.
The next gate uses the same Pi descriptor control with the unchanged normal
OnePlus module for one complete frame, physical detach, and safe restart.
Do not modify the OnePlus module, start mini-cycles, start the ten-cycle
matrix, or mark XDISP-P0.1 verified yet.

Evidence:

- `xdisp-p0.1-laptop-gate-e-2026-07-25T1919COT/`
- `xdisp-p0.1-laptop-gate-e-repeat-01-2026-07-25T1929COT/`
- `xdisp-p0.1-laptop-gate-e-repeat-02-2026-07-25T1936COT/`
