#!/usr/bin/env python3
"""aggregate_final_codecs.py - consolidates all real-hardware measurements
collected for the final codec/transport benchmark into:

  benchmark/results-final-codecs/tables/{summary,scenarios,latency,quality,usb}.csv
  benchmark/results-final-codecs/aggregate.json

All figures here are read from raw JSON produced directly by real hardware
runs (OnePlus 6 sender, Pi Zero 2 W decoder, real USB bulk transport). No
number in this script is modeled or fabricated; where real coverage was
not obtained the corresponding cell is left absent/null rather than
invented.
"""
import csv
import json
from pathlib import Path

ROOT = Path("/home/cristianr/Projects/linux-mobile/gud-benchmark/benchmark/results-final-codecs")
SCENARIOS = ["idle-desktop", "app-switching", "mixed-web-content", "app-launch-close"]

def load(p):
    return json.loads(Path(p).read_text())

# ---- OP6 sender ----
op6 = {}
for s in SCENARIOS:
    op6[s] = load(ROOT / f"isolated/op6/{s}.sender.json")

# ---- Pi LZ4 decode ----
pi_lz4 = {}
for s in SCENARIOS:
    pi_lz4[s] = {
        "rgb565-lz4-current": load(ROOT / f"isolated/pi/lz4-decode/{s}.lz4decode.json"),
        "r4g4b4-lz4-fused": load(ROOT / f"isolated/pi/lz4-decode/{s}.r4g4b4.lz4decode.json"),
        "rgb332-lz4-fused": load(ROOT / f"isolated/pi/lz4-decode/{s}.rgb332.lz4decode.json"),
    }

# ---- Pi HW JPEG decode (resolution/depth sweep) ----
hwdec = {}
for res in ["1280x720", "1920x1080", "720x1280", "1080x1920"]:
    for depth in [2, 4, 8]:
        p = ROOT / f"isolated/pi/jpeg-hw-decode/hwdec_{res}_d{depth}.json"
        if p.exists():
            hwdec[f"{res}_d{depth}"] = load(p)

# ---- USB raw payload sweep ----
usb = load(ROOT / "usb/raw_payload_sweep.json")

# ---- Quality ----
quality = load(ROOT / "quality/quality.json")

# =========================================================================
# tables/summary.csv -- PROJECT SPEC section 29 primary comparison table
# =========================================================================
def op6_stat(scenario, candidate, field):
    for r in op6[scenario]:
        if r["candidate"] == candidate:
            return r.get(field)
    return None

summary_rows = []
candidates = [
    ("RAW RGB565", "raw-rgb565-conversion", None),
    ("RGB565 + LZ4 (current)", "rgb565-lz4-current", "rgb565-lz4-current"),
    ("R4G4B4 + LZ4", "r4g4b4-lz4-fused", "r4g4b4-lz4-fused"),
    ("RGB332 + LZ4", "rgb332-lz4-fused", "rgb332-lz4-fused"),
    ("JPEG Q95 4:4:4 + HW decode", "jpeg-q95-444", None),
    ("JPEG Q90 4:4:4 + HW decode", "jpeg-q90-444", None),
    ("JPEG Q85 4:2:0 + HW decode", "jpeg-q85-420", None),
]

# representative scenario for the headline table: mixed-web-content (most
# "typical" real UI content among the 4 captured; see scenarios.csv for
# every scenario's own row)
REP = "mixed-web-content"
usb_by_bytes = {r["payload_bytes"]: r for r in usb}

def nearest_usb_p50_mib_s(nbytes):
    if not nbytes:
        return None
    closest = min(usb_by_bytes.keys(), key=lambda b: abs(b - nbytes))
    return usb_by_bytes[closest].get("throughput_mib_s_p50")

for label, op6_name, pi_lz4_name in candidates:
    prep_p50 = op6_stat(REP, op6_name, "p50_ns")
    bytes_out = op6_stat(REP, op6_name, "bytes_out")
    usb_mib_s = nearest_usb_p50_mib_s(bytes_out)
    if pi_lz4_name:
        pi_dec = pi_lz4[REP][pi_lz4_name]
        pi_p50_ns = pi_dec["p50_ns"]
    elif "jpeg" in op6_name:
        # Use the 720x1280 depth=4 sustained-throughput HW decode entry
        # (matches this table's primary resolution) for a representative
        # per-frame decode time (1/fps), and note serial-vs-pipelined
        # separately in scenarios.csv / summary-final-codecs.md.
        hd = hwdec.get("720x1280_d4")
        pi_p50_ns = (1e9 / hd["sustained_fps"]) if hd else None
    else:
        pi_p50_ns = None
    quality_entry = quality.get(REP, {})
    qkey = {
        "rgb565-lz4-current": None,
        "r4g4b4-lz4-fused": "r4g4b4-lz4-fused",
        "rgb332-lz4-fused": "rgb332-lz4-fused",
        "jpeg-q95-444": "jpeg-q95-444-hwdecode",
        "jpeg-q90-444": "jpeg-q90-444-hwdecode",
        "jpeg-q85-420": "jpeg-q85-420-hwdecode",
    }.get(op6_name)
    psnr = None
    ssim_v = None
    if qkey and qkey in quality_entry:
        m = quality_entry[qkey].get("vs_original_rgba_source")
        if m:
            psnr, ssim_v = m["psnr_db"], m["ssim"]
    serial_total_ns = None
    if prep_p50 is not None and usb_mib_s and bytes_out and pi_p50_ns is not None:
        usb_ns = (bytes_out / (usb_mib_s * 1024 * 1024)) * 1e9
        serial_total_ns = prep_p50 + usb_ns + pi_p50_ns
    summary_rows.append({
        "candidate": label,
        "mir_source_format": "RGBA8888 (mirscreencast capture) -> derived per-candidate",
        "op6_prep_p50_us": round(prep_p50 / 1000, 1) if prep_p50 else "",
        "op6_encode_p50_us": round(prep_p50 / 1000, 1) if prep_p50 else "",
        "bytes_per_frame": bytes_out,
        "usb_p50_mib_s_nearest_measured": round(usb_mib_s, 2) if usb_mib_s else "",
        "pi_decode_p50_us": round(pi_p50_ns / 1000, 1) if pi_p50_ns else "",
        "serial_total_us": round(serial_total_ns / 1000, 1) if serial_total_ns else "",
        "quality_psnr_db_vs_source": round(psnr, 2) if psnr is not None else "",
        "quality_ssim_vs_source": round(ssim_v, 4) if ssim_v is not None else "",
        "scenario": REP,
        "label": "MEASURED (mixed real-device components combined arithmetically for serial_total; see summary-final-codecs.md for the exact formula and caveats)",
    })

with open(ROOT / "tables/summary.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(summary_rows[0].keys()))
    w.writeheader()
    w.writerows(summary_rows)

# =========================================================================
# tables/scenarios.csv -- per-scenario OP6 sender + Pi decode
# =========================================================================
scenario_rows = []
for s in SCENARIOS:
    for r in op6[s]:
        row = {
            "scenario": s,
            "candidate": r["candidate"],
            "op6_mean_us": round(r["mean_ns"] / 1000, 1),
            "op6_p50_us": round(r["p50_ns"] / 1000, 1),
            "op6_p95_us": round(r["p95_ns"] / 1000, 1),
            "op6_max_us": round(r["max_ns"] / 1000, 1),
            "bytes_out": r["bytes_out"],
            "mpixels_s": round(r["mpixels_s"], 1),
        }
        scenario_rows.append(row)
with open(ROOT / "tables/scenarios.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(scenario_rows[0].keys()))
    w.writeheader()
    w.writerows(scenario_rows)

# =========================================================================
# tables/latency.csv -- Pi decode-side latency (LZ4 + HW JPEG)
# =========================================================================
latency_rows = []
for s in SCENARIOS:
    for cand, d in pi_lz4[s].items():
        latency_rows.append({
            "scope": "pi-lz4-decode", "scenario": s, "candidate": cand,
            "mean_us": round(d["mean_ns"] / 1000, 1),
            "p50_us": round(d["p50_ns"] / 1000, 1),
            "p95_us": round(d["p95_ns"] / 1000, 1),
            "max_us": round(d["max_ns"] / 1000, 1),
        })
for key, d in hwdec.items():
    latency_rows.append({
        "scope": "pi-jpeg-hw-decode", "scenario": key, "candidate": f"depth={d['pipeline_depth']}",
        "mean_us": "", "p50_us": round(d.get("latency_ns_p50", 0) / 1000, 1) if d.get("latency_ns_p50") else "",
        "p95_us": round(d.get("latency_ns_p95", 0) / 1000, 1) if d.get("latency_ns_p95") else "",
        "max_us": round(d.get("latency_ns_max", 0) / 1000, 1) if d.get("latency_ns_max") else "",
        "sustained_fps": round(d["sustained_fps"], 2),
        "completed_frames": d["completed_frames"], "decode_errors": d["decode_errors"],
    })
all_keys = set()
for r in latency_rows:
    all_keys.update(r.keys())
with open(ROOT / "tables/latency.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=sorted(all_keys))
    w.writeheader()
    w.writerows(latency_rows)

# =========================================================================
# tables/quality.csv
# =========================================================================
quality_rows = []
for scenario, cands in quality.items():
    for cand, d in cands.items():
        if "vs_original_rgba_source" in d:
            m = d["vs_original_rgba_source"]
            quality_rows.append({
                "scenario": scenario, "candidate": cand,
                "mae": round(m["mae"], 3), "rmse": round(m["rmse"], 3),
                "psnr_db": round(m["psnr_db"], 2) if m["psnr_db"] != float("inf") else "inf",
                "ssim": round(m["ssim"], 4), "max_channel_error": m["max_channel_error"],
            })
with open(ROOT / "tables/quality.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=["scenario", "candidate", "mae", "rmse", "psnr_db", "ssim", "max_channel_error"])
    w.writeheader()
    w.writerows(quality_rows)

# =========================================================================
# tables/usb.csv
# =========================================================================
usb_rows = []
for r in usb:
    usb_rows.append({
        "label": r["label"], "payload_bytes": r["payload_bytes"],
        "sample_count": r["sample_count"],
        "elapsed_us_p50": r.get("elapsed_us_p50"), "elapsed_us_p95": r.get("elapsed_us_p95"),
        "elapsed_us_max": r.get("elapsed_us_max"),
        "throughput_mib_s_mean": round(r.get("throughput_mib_s_mean", 0), 2),
        "throughput_mib_s_p50": round(r.get("throughput_mib_s_p50", 0), 2),
    })
with open(ROOT / "tables/usb.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(usb_rows[0].keys()))
    w.writeheader()
    w.writerows(usb_rows)

# =========================================================================
# aggregate.json
# =========================================================================
aggregate = {
    "op6_sender": op6,
    "pi_lz4_decode": pi_lz4,
    "pi_jpeg_hw_decode": hwdec,
    "usb_raw_payload_sweep": usb,
    "quality": quality,
    "label": "MEASURED (real OnePlus 6 + real Raspberry Pi Zero 2 W + real USB2 bulk transport); see summary-final-codecs.md for narrative interpretation and benchmark/results-final-codecs/environment/*.json for exact hardware/software provenance",
}
(ROOT / "aggregate.json").write_text(json.dumps(aggregate, indent=2))
print("wrote tables/*.csv and aggregate.json")
