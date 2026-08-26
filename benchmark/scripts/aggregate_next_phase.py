#!/usr/bin/env python3
"""aggregate_next_phase.py - consolidates the next-phase benchmark outputs
(real Lomiri capture dedup stats, Pi Zero 2 W narrowed-finalist codec
results, Pi temporal-policy sweep) into the machine-readable artifacts and
comparison tables required by the next-phase spec:

  results-next-phase/aggregate.json           -- everything, one file
  results-next-phase/tables/real_corpus.csv    -- "Real full-frame corpus"
  results-next-phase/tables/pi_zero2w.csv      -- "Pi Zero 2 W"
  results-next-phase/tables/usb_model.csv      -- "USB model"
  results-next-phase/tables/lossy_quality.csv  -- "Lossy quality"
  results-next-phase/tables/temporal.csv       -- "Temporal"
  results-next-phase/tables/qoir_net_gain.csv  -- Phase 7 net sender gain

All numbers here are MEASURED_PI (Pi Zero 2 W CPU/encode/decode timing on
the real captured corpus) or MODELED (USB transfer time derived from
--usb-mib-s, i.e. not measured USB hardware bytes-on-the-wire). See
summary-next-phase.md for the full MEASURED/MODELED/ESTIMATED legend.
"""
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PI_JSON_DIR = ROOT / "results-next-phase" / "pi-json-o3"
PI_TEMPORAL_DIR = ROOT / "results-next-phase" / "pi-temporal"
DEDUP_PATH = ROOT / "capture" / "dedup_stats.json"
TABLES_DIR = ROOT / "results-next-phase" / "tables"
AGGREGATE_PATH = ROOT / "results-next-phase" / "aggregate.json"

FINALISTS = [
    "rgb565-lz4",
    "quant-moderate-16bit-lz4",
    "quant-aggressive-lz4",
    "qoir-lossless",
    "qoir-lossy-l3",
    "qoir-lossy-l5",
    "prev-xor-lz4",
    "prev-sub-lz4",
]


def load_pi_results():
    scenarios = {}
    for p in sorted(PI_JSON_DIR.glob("*.json")):
        scenario = p.stem
        d = json.loads(p.read_text())
        scenarios[scenario] = {r["codec"]: r for r in d["results"]}
    return scenarios


def load_temporal():
    out = {}
    for p in sorted(PI_TEMPORAL_DIR.glob("*.json")):
        d = json.loads(p.read_text())
        scenario = d["corpus"]
        key = "adaptive" if d["adaptive"] else f"ki{d['keyframe_interval']}"
        out.setdefault(scenario, {})[key] = d
    return out


def main():
    TABLES_DIR.mkdir(parents=True, exist_ok=True)
    pi_results = load_pi_results()
    temporal = load_temporal()
    dedup = json.loads(DEDUP_PATH.read_text()) if DEDUP_PATH.exists() else {}

    aggregate = {
        "label_legend": {
            "MEASURED_PI": "Measured on real Raspberry Pi Zero 2 W hardware "
                            "(see environment.json for CPU/kernel/governor)",
            "MEASURED_LOMIRI_CAPTURE": "Real pixel data captured from the "
                            "actual lomiri-system-compositor session on the "
                            "project OnePlus 6 via mirscreencast",
            "MODELED": "Derived from measured encode/decode bytes and "
                            "times using an assumed USB throughput; no USB "
                            "hardware bytes-on-the-wire were measured for "
                            "this number",
            "ESTIMATED": "Derived quantity (e.g. linearly interpolated "
                            "per-frame timestamp) that is not itself a "
                            "direct hardware measurement",
        },
        "finalist_codecs": FINALISTS,
        "scenarios": {},
        "temporal_policy": {},
    }

    # ---- Real full-frame corpus table -----------------------------------
    with open(TABLES_DIR / "real_corpus.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["codec", "scenario", "encoded_bytes_mean",
                     "ratio_vs_raw", "ratio_vs_lz4", "encode_ms_mean",
                     "decode_ms_mean", "conversion_encode_ms_mean",
                     "conversion_decode_ms_mean", "roundtrip_exact",
                     "label"])
        for scenario, codecs in pi_results.items():
            lz4_bytes = codecs.get("rgb565-lz4", {}).get(
                "mean_encoded_bytes")
            for codec in FINALISTS:
                r = codecs.get(codec)
                if not r:
                    continue
                ratio_vs_lz4 = (lz4_bytes / r["mean_encoded_bytes"]
                                 if lz4_bytes and r["mean_encoded_bytes"]
                                 else None)
                conv = r.get("conversion_breakdown")
                w.writerow([
                    codec, scenario, round(r["mean_encoded_bytes"], 1),
                    round(r["compression_ratio"], 3),
                    round(ratio_vs_lz4, 3) if ratio_vs_lz4 else "",
                    round(r["encode_ns"]["mean"] / 1e6, 4),
                    round(r["decode_ns"]["mean"] / 1e6, 4),
                    round(conv["encode_conversion_ns"]["mean"] / 1e6, 4)
                        if conv else "",
                    round(conv["decode_conversion_ns"]["mean"] / 1e6, 4)
                        if conv else "",
                    r["roundtrip_exact"], "MEASURED_PI",
                ])
                aggregate["scenarios"].setdefault(scenario, {})[codec] = r

    # ---- Pi Zero 2 W table (encode/decode percentiles + CPU proxy) ------
    with open(TABLES_DIR / "pi_zero2w.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["codec", "scenario", "resolution", "encode_ms_p50",
                     "encode_ms_p95", "encode_ms_p99", "decode_ms_p50",
                     "decode_ms_p95", "encode_MiBps", "decode_MiBps",
                     "label"])
        for scenario, codecs in pi_results.items():
            for codec in FINALISTS:
                r = codecs.get(codec)
                if not r:
                    continue
                w.writerow([
                    codec, scenario, f"{r['width']}x{r['height']}",
                    round(r["encode_ns"]["median"] / 1e6, 4),
                    round(r["encode_ns"]["p95"] / 1e6, 4),
                    round(r["encode_ns"]["p99"] / 1e6, 4),
                    round(r["decode_ns"]["median"] / 1e6, 4),
                    round(r["decode_ns"]["p95"] / 1e6, 4),
                    round(r["encode_MiBps"], 2),
                    round(r["decode_MiBps"], 2),
                    "MEASURED_PI",
                ])

    # ---- USB model table (modeled FPS at each throughput point) ---------
    with open(TABLES_DIR / "usb_model.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["codec", "scenario", "usb_mib_s", "bytes_per_frame",
                     "usb_transfer_ms", "encode_plus_usb_ms",
                     "fps_usb_only", "fps_encode_usb", "fps_total_serial",
                     "label"])
        for scenario, codecs in pi_results.items():
            for codec in FINALISTS:
                r = codecs.get(codec)
                if not r:
                    continue
                for m in r["usb_models"]:
                    encode_plus_usb_ms = (r["encode_ns"]["mean"] / 1e6 +
                                            m["usb_transfer_ns"] / 1e6)
                    w.writerow([
                        codec, scenario, m["usb_mib_s"],
                        round(r["mean_encoded_bytes"], 1),
                        round(m["usb_transfer_ns"] / 1e6, 4),
                        round(encode_plus_usb_ms, 4),
                        round(m["fps_usb_only"], 3),
                        round(m["fps_encode_usb"], 3),
                        round(m["fps_total_serial"], 3),
                        "MODELED",
                    ])

    # ---- Lossy quality table ---------------------------------------------
    with open(TABLES_DIR / "lossy_quality.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["codec", "scenario", "bytes_per_frame", "psnr_db",
                     "max_abs_error", "encode_ms", "decode_ms", "label"])
        for scenario, codecs in pi_results.items():
            for codec in ["quant-moderate-16bit-lz4", "quant-aggressive-lz4",
                           "qoir-lossy-l3", "qoir-lossy-l5"]:
                r = codecs.get(codec)
                if not r or not r.get("quality"):
                    continue
                w.writerow([
                    codec, scenario, round(r["mean_encoded_bytes"], 1),
                    round(r["quality"]["psnr_db"], 2),
                    round(r["quality"]["max_abs_error"], 2),
                    round(r["encode_ns"]["mean"] / 1e6, 4),
                    round(r["decode_ns"]["mean"] / 1e6, 4),
                    "MEASURED_PI",
                ])

    # ---- Temporal table (Phase 8) -----------------------------------------
    with open(TABLES_DIR / "temporal.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["scenario", "lz4_bytes_mean", "xor_lz4_bytes_mean",
                     "saving_percent", "keyframe_interval_or_policy",
                     "keyframe_count", "delta_count", "total_bytes",
                     "adaptive_delta_wins", "total_frames", "label"])
        for scenario, codecs in pi_results.items():
            lz4_r = codecs.get("rgb565-lz4")
            xor_r = codecs.get("prev-xor-lz4")
            if lz4_r and xor_r:
                saving = (1 - xor_r["mean_encoded_bytes"] /
                           lz4_r["mean_encoded_bytes"]) * 100
                w.writerow([
                    scenario, round(lz4_r["mean_encoded_bytes"], 1),
                    round(xor_r["mean_encoded_bytes"], 1),
                    round(saving, 2), "always-delta (T1, no keyframe cadence)",
                    "", "", "", "", lz4_r["iterations"], "MEASURED_PI",
                ])
            for policy, d in temporal.get(scenario, {}).items():
                s = d["summary"]
                w.writerow([
                    scenario, "", "", "", policy,
                    s.get("keyframe_count", ""), s.get("delta_count", ""),
                    s.get("total_bytes", ""),
                    s.get("adaptive_delta_wins", "") if policy == "adaptive" else "",
                    s["total_frames"],
                    "MEASURED_PI",
                ])
                aggregate["temporal_policy"].setdefault(scenario, {})[policy] = s

    # ---- Phase 7 QOIR net sender gain -------------------------------------
    with open(TABLES_DIR / "qoir_net_gain.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["scenario", "qoir_codec", "usb_mib_s",
                     "lz4_encode_ms", "lz4_usb_ms", "qoir_conversion_ms",
                     "qoir_codec_encode_ms", "qoir_usb_ms",
                     "net_sender_gain_ms", "qoir_wins", "label"])
        for scenario, codecs in pi_results.items():
            lz4_r = codecs.get("rgb565-lz4")
            if not lz4_r:
                continue
            for qcodec in ["qoir-lossless", "qoir-lossy-l3", "qoir-lossy-l5"]:
                r = codecs.get(qcodec)
                if not r or not r.get("conversion_breakdown"):
                    continue
                conv_ms = r["conversion_breakdown"]["encode_conversion_ns"]["mean"] / 1e6
                codec_ms = r["conversion_breakdown"]["encode_codec_ns"]["mean"] / 1e6
                for m_lz4, m_q in zip(lz4_r["usb_models"], r["usb_models"]):
                    lz4_encode_ms = lz4_r["encode_ns"]["mean"] / 1e6
                    lz4_usb_ms = m_lz4["usb_transfer_ns"] / 1e6
                    qoir_usb_ms = m_q["usb_transfer_ns"] / 1e6
                    net_gain = (lz4_encode_ms + lz4_usb_ms) - \
                        (conv_ms + codec_ms + qoir_usb_ms)
                    w.writerow([
                        scenario, qcodec, m_lz4["usb_mib_s"],
                        round(lz4_encode_ms, 4), round(lz4_usb_ms, 4),
                        round(conv_ms, 4), round(codec_ms, 4),
                        round(qoir_usb_ms, 4), round(net_gain, 4),
                        net_gain > 0, "MODELED (USB) + MEASURED_PI (CPU)",
                    ])

    aggregate["dedup_stats"] = dedup
    AGGREGATE_PATH.write_text(json.dumps(aggregate, indent=2))
    print(f"wrote {AGGREGATE_PATH}")
    for f in sorted(TABLES_DIR.glob("*.csv")):
        print(f"wrote {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
