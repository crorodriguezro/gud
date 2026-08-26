#!/usr/bin/env python3
"""frame_dedup_stats.py - PROJECT SPEC (next-phase) "Frame deduplication
statistics" (Phase 2, before codec testing).

For every consecutive frame pair in each real captured scenario computes:
  identical_pixel_percent, changed_pixel_percent, identical_byte_percent,
  XOR_zero_pixel_percent, XOR_zero_byte_percent
plus per-scenario distributions (p50/p90/p95/p99 changed pixels).

All numbers here are MEASURED directly from the real Lomiri/Mir capture
corpus produced by capture_lomiri_scenario.sh + ingest_lomiri_capture.py.

Note (documented, not a bug): for a packed RGB565 pixel format,
XOR_zero_pixel_percent is mathematically identical to
identical_pixel_percent (two u16 pixels are equal iff their XOR is zero),
and XOR_zero_byte_percent is identical to identical_byte_percent for the
same reason at the byte granularity. Both are reported, as the PROJECT
SPEC requests, to make that equivalence explicit and auditable rather
than silently only reporting one of the two names.
"""
import json
import sys
from pathlib import Path

import numpy as np


def pct(n, d):
    return 100.0 * n / d if d else 0.0


def analyze_scenario(scenario_dir: Path, scenario: str) -> dict:
    index = json.loads((scenario_dir / f"{scenario}.index.json").read_text())
    w = index["frames"][0]["width"]
    h = index["frames"][0]["height"]
    n = index["frame_count"]
    path = scenario_dir / index["rgb565_file"]
    data = np.fromfile(path, dtype="<u2", count=w * h * n).reshape(n, h, w)

    per_pair = []
    for i in range(1, n):
        prev = data[i - 1]
        cur = data[i]
        xor = np.bitwise_xor(prev, cur)
        total_pixels = w * h
        changed_pixels = int(np.count_nonzero(xor))
        identical_pixels = total_pixels - changed_pixels

        prev_bytes = prev.view(np.uint8)
        cur_bytes = cur.view(np.uint8)
        total_bytes = prev_bytes.size
        changed_bytes = int(np.count_nonzero(prev_bytes != cur_bytes))
        identical_bytes = total_bytes - changed_bytes

        per_pair.append({
            "pair_index": i - 1,
            "seq_prev": index["frames"][i - 1]["sequence_number"],
            "seq_cur": index["frames"][i]["sequence_number"],
            "identical_pixel_percent": pct(identical_pixels, total_pixels),
            "changed_pixel_percent": pct(changed_pixels, total_pixels),
            "identical_byte_percent": pct(identical_bytes, total_bytes),
            "XOR_zero_pixel_percent": pct(identical_pixels, total_pixels),
            "XOR_zero_byte_percent": pct(identical_bytes, total_bytes),
            "changed_pixels": changed_pixels,
            "total_pixels": total_pixels,
        })

    changed_pct = np.array([p["changed_pixel_percent"] for p in per_pair])
    summary = {
        "scenario": scenario,
        "width": w,
        "height": h,
        "frame_count": n,
        "pair_count": len(per_pair),
        "changed_pixel_percent_mean": float(np.mean(changed_pct)) if len(changed_pct) else None,
        "changed_pixel_percent_p50": float(np.percentile(changed_pct, 50)) if len(changed_pct) else None,
        "changed_pixel_percent_p90": float(np.percentile(changed_pct, 90)) if len(changed_pct) else None,
        "changed_pixel_percent_p95": float(np.percentile(changed_pct, 95)) if len(changed_pct) else None,
        "changed_pixel_percent_p99": float(np.percentile(changed_pct, 99)) if len(changed_pct) else None,
        "changed_pixel_percent_min": float(np.min(changed_pct)) if len(changed_pct) else None,
        "changed_pixel_percent_max": float(np.max(changed_pct)) if len(changed_pct) else None,
        "label": "MEASURED_LOMIRI_CAPTURE",
        "pairs": per_pair,
    }
    return summary


def main():
    if len(sys.argv) < 2:
        print("usage: frame_dedup_stats.py <capture_dir> [out.json]", file=sys.stderr)
        return 2
    capture_dir = Path(sys.argv[1])
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else capture_dir / "dedup_stats.json"

    scenarios = sorted(p.name[: -len(".index.json")] for p in capture_dir.glob("*.index.json"))
    results = {}
    for s in scenarios:
        print(f"analyzing {s}...", file=sys.stderr)
        results[s] = analyze_scenario(capture_dir, s)

    out_path.write_text(json.dumps(results, indent=2))
    print(f"wrote {out_path}")

    print(f"\n{'scenario':22s} {'frames':>6s} {'mean%chg':>9s} {'p50%chg':>8s} {'p90%chg':>8s} {'p95%chg':>8s} {'p99%chg':>8s}")
    for s, r in results.items():
        print(f"{s:22s} {r['frame_count']:6d} "
              f"{r['changed_pixel_percent_mean'] or 0:9.3f} "
              f"{r['changed_pixel_percent_p50'] or 0:8.3f} "
              f"{r['changed_pixel_percent_p90'] or 0:8.3f} "
              f"{r['changed_pixel_percent_p95'] or 0:8.3f} "
              f"{r['changed_pixel_percent_p99'] or 0:8.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
