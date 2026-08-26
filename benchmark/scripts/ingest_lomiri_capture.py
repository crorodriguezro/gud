#!/usr/bin/env python3
"""ingest_lomiri_capture.py - convert a raw RGBA8888 mirscreencast capture
(see capture_lomiri_scenario.sh) plus its .meta.json sidecar into:

  1. <scenario>.rgb565le  - concatenated width*height*2-byte RGB565LE frames,
     in exactly the format fbcodec-bench's existing capture_read_raw_frames()
     already reads (src/corpus/capture.c/.h) -- no new C container format is
     needed; this reuses the tested reader instead of inventing one.
  2. <scenario>.index.json - PROJECT SPEC (next-phase) Phase 2 "capture
     metadata" for every frame: sequence_number, timestamp_ns, width,
     height, stride, pixel_format, frame_size_bytes.

RGB565 conversion: mirscreencast only emits RGBA8888 (see
docs/lomiri-gud-integration-options.md -- no RGB565-producing Mir/GUD
integration is active on this phone; the DisplayPort-shim POC that would
create one is intentionally rolled back). We therefore capture the native
RGBA8888 source and perform the RGB565 conversion here at ingestion time
(PROJECT SPEC Phase 2 option 2), using the *same* truncating conversion
fbcodec-bench already uses for its QOI/QOIR/CharLS codec paths
(rgb888_to_rgb565: r>>3, g>>2, b>>3 -- see src/common.h). Alpha is dropped
(the real GUD RGB565 plane has no alpha channel).

Per-frame timestamps are not available from mirscreencast (it emits a
flat pixel stream, no per-frame timing). We linearly interpolate
timestamp_ns across the measured wall-clock capture span recorded in the
.meta.json (capture_wall_start_epoch_ns/capture_wall_end_epoch_ns) and
mark this in the index as ESTIMATED, not MEASURED -- see
summary-next-phase.md.
"""
import json
import sys
import numpy as np
from pathlib import Path


def convert(scenario_dir: Path, scenario: str) -> None:
    meta_path = scenario_dir / f"{scenario}.meta.json"
    raw_path = scenario_dir / f"{scenario}.rgba"
    meta = json.loads(meta_path.read_text())

    w, h = meta["width"], meta["height"]
    n = meta["actual_frames"]
    frame_rgba_bytes = w * h * 4
    stride = w * 2
    frame_rgb565_bytes = h * stride

    data = np.fromfile(raw_path, dtype=np.uint8, count=frame_rgba_bytes * n)
    data = data.reshape(n, h, w, 4)
    r = data[..., 0].astype(np.uint16)
    g = data[..., 1].astype(np.uint16)
    b = data[..., 2].astype(np.uint16)
    # NOTE: mirscreencast's --query reported "Colorspace: RGBA" for this
    # server; byte order in the raw stream is R,G,B,A per pixel, verified
    # visually against a PNG re-encode of a captured frame during capture
    # tooling development (see summary-next-phase.md "Capture methodology").
    rgb565 = (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).astype("<u2")

    out_path = scenario_dir / f"{scenario}.rgb565le"
    rgb565.tofile(out_path)

    start_ns = meta["capture_wall_start_epoch_ns"]
    end_ns = meta["capture_wall_end_epoch_ns"]
    frames_index = []
    for i in range(n):
        ts = start_ns if n <= 1 else int(start_ns + (end_ns - start_ns) * i / (n - 1))
        frames_index.append({
            "sequence_number": i,
            "timestamp_ns": ts,
            "timestamp_ns_basis": "ESTIMATED_LINEAR_INTERPOLATION",
            "width": w,
            "height": h,
            "stride": stride,
            "pixel_format": "RGB565LE",
            "frame_size_bytes": frame_rgb565_bytes,
        })

    index = {
        "scenario": scenario,
        "source_capture": meta,
        "conversion": "RGBA8888 -> RGB565 via rgb888_to_rgb565 (r>>3,g>>2,b>>3), alpha dropped",
        "frame_count": n,
        "rgb565_file": out_path.name,
        "frames": frames_index,
        "label": "MEASURED_LOMIRI_CAPTURE (pixels); ESTIMATED (per-frame timestamp interpolation)",
    }
    (scenario_dir / f"{scenario}.index.json").write_text(json.dumps(index, indent=2))
    print(f"{scenario}: {n} frames -> {out_path} ({out_path.stat().st_size} bytes), "
          f"index -> {scenario}.index.json")


def main():
    if len(sys.argv) < 2:
        print("usage: ingest_lomiri_capture.py <scenario_dir> [scenario ...]", file=sys.stderr)
        return 2
    scenario_dir = Path(sys.argv[1])
    scenarios = sys.argv[2:]
    if not scenarios:
        scenarios = sorted(
            p.name[: -len(".meta.json")] for p in scenario_dir.glob("*.meta.json")
        )
    for s in scenarios:
        convert(scenario_dir, s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
