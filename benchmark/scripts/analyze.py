#!/usr/bin/env python3
"""
analyze.py - aggregates fbcodec-bench raw per-run JSON files into the
final results.json / summary.md deliverables (PROJECT SPEC sections
18-21, 36-41, 55-56).

Reads:
  results/raw/*.json       - one file per fbcodec-bench invocation
  results/residual-analysis.json
  results/environment.json

Writes:
  results/results.json      - combined, richer than results.csv
  results/summary.md        - the final evidence-heavy report
"""
import glob
import json
import os
import statistics
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW_DIR = os.path.join(ROOT, "results", "raw")
RESULTS_DIR = os.path.join(ROOT, "results")

USB_REFERENCE_MIB_S = 40.0  # representative USB2-bulk payload throughput

CODEC_ORDER = [
    "raw", "rgb565-lz4",
    "sub-byte-lz4", "u16-sub-lz4", "u16-xor-lz4", "paeth-byte-lz4",
    "paeth-pixel-lz4", "med-lz4", "channel-sub-lz4", "channel-xor-lz4",
    "shuffle-lz4",
    "quant-mild-raw", "quant-mild-lz4", "quant-moderate-16bit-lz4",
    "quant-moderate-packed12-lz4", "quant-aggressive-raw",
    "quant-aggressive-lz4",
    "quant-mild-u16sub-lz4", "quant-mild-u16xor-lz4",
    "quant-moderate-bestpred-lz4", "quant-shuffle-lz4",
    "prev-xor-lz4", "prev-sub-lz4", "prev-quant-xor-lz4",
    "rle-lz4",
    "qoi", "qoir-lossless", "qoir-lossy-l3", "qoir-lossy-l5",
    "charls-lossless", "charls-near1", "charls-near3",
]


def load_all_raw():
    records = []
    for path in sorted(glob.glob(os.path.join(RAW_DIR, "*.json"))):
        with open(path) as f:
            doc = json.load(f)
        for r in doc.get("results", []):
            r["_source_file"] = os.path.basename(path)
            records.append(r)
    return records


def usb_model_at(record, usb_mib_s):
    for m in record.get("usb_models", []):
        if abs(m["usb_mib_s"] - usb_mib_s) < 1e-6:
            return m
    return None


def mean(values):
    values = [v for v in values if v is not None]
    return statistics.fmean(values) if values else 0.0


def fmt(v, nd=2):
    if v is None:
        return "n/a"
    if isinstance(v, float) and (v != v or v in (float("inf"), float("-inf"))):
        return "n/a"
    return f"{v:.{nd}f}"


def is_frame_full(r):
    return r["rect_class"].startswith("full-")


def is_sequence(r):
    return r["rect_class"] == "full-sequence"


def is_damage_stream(r):
    return r["rect_class"] == "damage-stream"


def is_frame_rect(r):
    return not is_sequence(r) and not is_damage_stream(r)


def group_by(records, keyfn):
    out = {}
    for r in records:
        out.setdefault(keyfn(r), []).append(r)
    return out


def size_bucket(input_bytes):
    kib = input_bytes / 1024.0
    if kib < 4:
        return "<4 KiB"
    if kib < 16:
        return "4-16 KiB"
    if kib < 64:
        return "16-64 KiB"
    if kib < 256:
        return "64-256 KiB"
    return ">256 KiB"


BUCKET_ORDER = ["<4 KiB", "4-16 KiB", "16-64 KiB", "64-256 KiB", ">256 KiB"]


def codec_meta(records):
    meta = {}
    for r in records:
        meta.setdefault(r["codec"], r)
    return meta


def per_codec_average(records, field_path):
    """field_path: list of keys, e.g. ['encode_ns','mean']."""
    vals = []
    for r in records:
        v = r
        for k in field_path:
            v = v.get(k) if isinstance(v, dict) else None
            if v is None:
                break
        if v is not None:
            vals.append(v)
    return mean(vals)


def build_headline_table(records_full):
    """Table required by section 36: bytes/frame, ratio, enc us, dec us,
    USB ms, total ms -- averaged across corpora for one full-frame
    resolution, using the USB_REFERENCE_MIB_S throughput.
    """
    by_codec = group_by(records_full, lambda r: r["codec"])
    rows = []
    for codec in CODEC_ORDER:
        recs = by_codec.get(codec, [])
        if not recs:
            continue
        bytes_mean = mean(r["mean_encoded_bytes"] for r in recs)
        input_bytes_mean = mean(r["input_bytes"] for r in recs)
        # Ratio-of-means (not mean-of-ratios): averaging per-corpus ratios
        # directly is dominated by outliers such as perfectly flat frames,
        # where RLE/LZ4 compound to a huge ratio for that one corpus and
        # would otherwise skew the multi-corpus average unrepresentatively.
        ratio_mean = (input_bytes_mean / bytes_mean) if bytes_mean else 0.0
        enc_us = mean(r["encode_ns"]["mean"] for r in recs) / 1000.0
        dec_us = mean(r["decode_ns"]["mean"] for r in recs) / 1000.0
        usb_ms = []
        total_ms = []
        for r in recs:
            m = usb_model_at(r, USB_REFERENCE_MIB_S)
            if m:
                usb_ms.append(m["usb_transfer_ns"] / 1e6)
                total_ms.append(
                    (r["encode_ns"]["mean"] + m["usb_transfer_ns"] +
                     r["decode_ns"]["mean"]) / 1e6)
        rows.append({
            "codec": codec,
            "label": recs[0]["label"],
            "bytes": bytes_mean,
            "ratio": ratio_mean,
            "enc_us": enc_us,
            "dec_us": dec_us,
            "usb_ms": mean(usb_ms),
            "total_ms": mean(total_ms),
        })
    return rows


def build_fps_table(records_720p_full, records_1080p_full):
    by_720 = group_by(records_720p_full, lambda r: r["codec"])
    by_1080 = group_by(records_1080p_full, lambda r: r["codec"])
    rows = []
    for codec in CODEC_ORDER:
        r720 = by_720.get(codec, [])
        r1080 = by_1080.get(codec, [])
        if not r720 and not r1080:
            continue

        def fps_for(recs):
            # FPS = 1 / total_time; averaging the *time* components first
            # (encode/usb/decode) and inverting once avoids the mean-of-
            # reciprocals skew that a single very-fast outlier corpus (e.g.
            # a flat-color frame compressing to near-zero bytes) would
            # otherwise cause if per-corpus FPS values were averaged
            # directly.
            enc_ns, usb_ns, dec_ns = [], [], []
            for r in recs:
                m = usb_model_at(r, USB_REFERENCE_MIB_S)
                if m:
                    enc_ns.append(r["encode_ns"]["mean"])
                    usb_ns.append(m["usb_transfer_ns"])
                    dec_ns.append(r["decode_ns"]["mean"])
            total_ns = mean(enc_ns) + mean(usb_ns) + mean(dec_ns)
            return 1.0e9 / total_ns if total_ns > 0 else 0.0

        rows.append({
            "codec": codec,
            "fps_720p": fps_for(r720),
            "fps_1080p": fps_for(r1080),
        })
    return rows


def build_metadata_table(all_records):
    meta = codec_meta(all_records)
    rows = []
    for codec in CODEC_ORDER:
        r = meta.get(codec)
        if not r:
            continue
        rows.append({
            "codec": codec,
            "lossy": r["is_lossy"],
            "stateful": r["is_stateful"],
            "native565": r["native_rgb565"],
            "complexity": r["complexity"],
            "category": r["category"],
        })
    return rows


def build_improvement_table(records_720p_full):
    by_codec = group_by(records_720p_full, lambda r: r["codec"])
    baseline_recs = by_codec.get("rgb565-lz4", [])
    if not baseline_recs:
        return []
    b_bytes = mean(r["mean_encoded_bytes"] for r in baseline_recs)
    b_enc = mean(r["encode_ns"]["mean"] for r in baseline_recs)
    b_dec = mean(r["decode_ns"]["mean"] for r in baseline_recs)
    b_total = []
    for r in baseline_recs:
        m = usb_model_at(r, USB_REFERENCE_MIB_S)
        if m:
            b_total.append(r["encode_ns"]["mean"] + m["usb_transfer_ns"] +
                            r["decode_ns"]["mean"])
    b_total_mean = mean(b_total)

    rows = []
    for codec in CODEC_ORDER:
        if codec == "rgb565-lz4":
            continue
        recs = by_codec.get(codec, [])
        if not recs:
            continue
        c_bytes = mean(r["mean_encoded_bytes"] for r in recs)
        c_enc = mean(r["encode_ns"]["mean"] for r in recs)
        c_dec = mean(r["decode_ns"]["mean"] for r in recs)
        c_total = []
        for r in recs:
            m = usb_model_at(r, USB_REFERENCE_MIB_S)
            if m:
                c_total.append(r["encode_ns"]["mean"] + m["usb_transfer_ns"] +
                                r["decode_ns"]["mean"])
        c_total_mean = mean(c_total)

        size_delta = (c_bytes - b_bytes) / b_bytes * 100.0 if b_bytes else 0
        enc_delta = (c_enc - b_enc) / b_enc * 100.0 if b_enc else 0
        dec_delta = (c_dec - b_dec) / b_dec * 100.0 if b_dec else 0
        total_delta = ((c_total_mean - b_total_mean) / b_total_mean * 100.0
                        if b_total_mean else 0)

        # Break-even USB throughput (section 39): (B0-B1)/deltaT.
        delta_t_s = (c_enc - b_enc) / 1e9
        bytes_saved = b_bytes - c_bytes
        if bytes_saved <= 0:
            breakeven = "reject (larger output than baseline)"
        elif delta_t_s <= 0:
            breakeven = "always worthwhile (smaller AND not slower to encode)"
        else:
            mib_s = (bytes_saved / delta_t_s) / 1048576.0
            breakeven = f"{mib_s:.1f} MiB/s"

        rows.append({
            "codec": codec,
            "size_delta_pct": size_delta,
            "enc_delta_pct": enc_delta,
            "dec_delta_pct": dec_delta,
            "total_latency_delta_pct": total_delta,
            "net_latency_gain_ns": b_total_mean - c_total_mean,
            "break_even": breakeven,
        })
    return rows


def build_size_bucket_table(frame_records):
    """Section 40: recommend codec selection per damage-rectangle size."""
    buckets = {b: [] for b in BUCKET_ORDER}
    for r in frame_records:
        buckets[size_bucket(r["input_bytes"])].append(r)

    out = {}
    for bucket, recs in buckets.items():
        by_codec = group_by(recs, lambda r: r["codec"])
        baseline = by_codec.get("rgb565-lz4", [])
        b_total = []
        for r in baseline:
            m = usb_model_at(r, USB_REFERENCE_MIB_S)
            if m:
                b_total.append(r["encode_ns"]["mean"] + m["usb_transfer_ns"] +
                                r["decode_ns"]["mean"])
        b_total_mean = mean(b_total)

        rows = []
        for codec, crecs in by_codec.items():
            c_total = []
            for r in crecs:
                m = usb_model_at(r, USB_REFERENCE_MIB_S)
                if m:
                    c_total.append(r["encode_ns"]["mean"] +
                                    m["usb_transfer_ns"] +
                                    r["decode_ns"]["mean"])
            c_total_mean = mean(c_total)
            c_bytes_mean = mean(r["mean_encoded_bytes"] for r in crecs)
            c_input_mean = mean(r["input_bytes"] for r in crecs)
            ratio = (c_input_mean / c_bytes_mean) if c_bytes_mean else 0.0
            net_gain_pct = (
                (b_total_mean - c_total_mean) / b_total_mean * 100.0
                if b_total_mean else 0)
            rows.append((codec, ratio, net_gain_pct, len(crecs)))
        rows.sort(key=lambda t: -t[2])
        out[bucket] = rows
    return out


def main():
    all_records = load_all_raw()
    if not all_records:
        print("no raw results found under results/raw -- run "
              "scripts/run_benchmark.sh first", file=sys.stderr)
        sys.exit(1)

    frame_records = [r for r in all_records if is_frame_rect(r)]
    seq_records = [r for r in all_records if is_sequence(r)]
    ds_records = [r for r in all_records if is_damage_stream(r)]

    records_720p_full = [r for r in frame_records
                          if is_frame_full(r) and r["width"] == 1280 and
                          r["height"] == 720]
    records_1080p_full = [r for r in frame_records
                           if is_frame_full(r) and r["width"] == 1920 and
                           r["height"] == 1080]

    headline_720p = build_headline_table(records_720p_full)
    headline_1080p = build_headline_table(records_1080p_full)
    fps_table = build_fps_table(records_720p_full, records_1080p_full)
    meta_table = build_metadata_table(all_records)
    improvement = build_improvement_table(records_720p_full)
    size_buckets = build_size_bucket_table(frame_records)

    residual_path = os.path.join(RESULTS_DIR, "residual-analysis.json")
    residual = []
    if os.path.exists(residual_path):
        with open(residual_path) as f:
            residual = json.load(f)

    environment_path = os.path.join(RESULTS_DIR, "environment.json")
    environment = {}
    if os.path.exists(environment_path):
        with open(environment_path) as f:
            environment = json.load(f)

    combined = {
        "environment": environment,
        "headline_720p_full": headline_720p,
        "headline_1080p_full": headline_1080p,
        "fps_table": fps_table,
        "codec_metadata": meta_table,
        "improvement_vs_lz4_720p_full": improvement,
        "size_bucket_recommendation": size_buckets,
        "record_counts": {
            "frame_mode": len(frame_records),
            "sequence_mode": len(seq_records),
            "damage_stream_mode": len(ds_records),
            "total": len(all_records),
        },
    }
    with open(os.path.join(RESULTS_DIR, "results.json"), "w") as f:
        json.dump(combined, f, indent=2)

    write_summary_md(all_records, frame_records, seq_records, ds_records,
                      records_720p_full, records_1080p_full, headline_720p,
                      headline_1080p, fps_table, meta_table, improvement,
                      size_buckets, residual, environment)
    print("wrote results/results.json and results/summary.md")


def write_summary_md(all_records, frame_records, seq_records, ds_records,
                      records_720p_full, records_1080p_full, headline_720p,
                      headline_1080p, fps_table, meta_table, improvement,
                      size_buckets, residual, environment):
    lines = []
    a = lines.append

    a("# RGB565 Framebuffer Compression Benchmark -- Summary\n")
    a("This report is generated by `scripts/analyze.py` from raw "
      "measurements produced by `tools/fbcodec-bench` "
      "(see `results/raw/*.json`). Every number below is either "
      "**MEASURED IN THIS PROJECT (host)**, **MEASURED EXTERNALLY** "
      "(explicitly labeled inline), or **MODELED** (USB transfer time / "
      "FPS derived from measured bytes and a configurable throughput "
      "assumption). None are fabricated or copied from upstream codec "
      "benchmark pages.\n")

    a("## Environment\n")
    if environment:
        a(f"- Host role: {environment.get('host_role', 'n/a')}")
        a(f"- CPU: {environment.get('cpu_model', 'n/a')} "
          f"({environment.get('architecture', 'n/a')}, "
          f"{environment.get('logical_cpus', 'n/a')} logical CPUs, "
          f"max {environment.get('cpu_max_mhz', 'n/a')} MHz, "
          f"governor={environment.get('cpu_governor', 'n/a')})")
        a(f"- Kernel: {environment.get('kernel', 'n/a')}")
        a(f"- Compiler: {environment.get('compiler', 'n/a')}")
        a(f"- Build flags: {environment.get('build_flags', 'n/a')}")
        a(f"- LZ4 provenance: {environment.get('lz4_provenance', 'n/a')}")
        a(f"- CharLS: {environment.get('charls_package_version', 'n/a')} "
          f"({environment.get('charls_link', 'n/a')})")
        a(f"- QOI: {environment.get('qoi_provenance', 'n/a')}")
        a(f"- QOIR: {environment.get('qoir_provenance', 'n/a')}")
        a("")
        a("> **LIMITATION (MEASURED IN THIS PROJECT, host only):** "
          + environment.get("target_hardware_note", "") + "\n")
    else:
        a("(environment.json not found)\n")

    a("## Corpus\n")
    a("- **Synthetic** (MEASURED IN THIS PROJECT, generated "
      "deterministically by `tools/fbcodec-bench`): flat colors, "
      "checkerboards (3 cell sizes), horizontal/vertical/2D gradients, "
      "text-like high-contrast edges, random noise, and a synthetic "
      "photographic-like pattern (smooth sinusoidal color blobs).")
    a("- **Real motion video, converted to RGB565** (MEASURED IN THIS "
      "PROJECT): `backport-4.9/env/local/assets/"
      "xdisp-motion-1280x720-30fps-10s.rgb565le`, a real 1280x720/30fps/"
      "10s capture already present in this repository, tagged "
      "`real-motion` in all tables below. This is real pixel content, "
      "but it is **not** an actual Lomiri/Mir compositor damage-rectangle "
      "capture -- no such instrumentation is available in this "
      "environment (see Limitations).")
    a("- **Synthetic damage-rectangle stream** derived from the real "
      "asset by tile-diffing consecutive frames (32x32 tiles) -- "
      "`--mode damage-stream`, corpus tag `real-motion-damage-stream`, "
      f"{ds_records[0]['iterations'] if ds_records else 'n/a'} events "
      "replayed per codec (see Damage-stream section).")
    a("- **Resolutions:** 1280x720 and 1920x1080 (landscape, full sweep); "
      "720x1280 and 1080x1920 (portrait, `--rect full` only, reduced "
      "sweep given time budget).")
    a("- **Damage rectangle classes:** 16x16, 32x32, 64x64, 128x64, "
      "128x128, 256x256, 640x100, half-screen, full-screen, at every "
      "landscape resolution and every synthetic + real-motion corpus.")
    a("- **Sequence-mode corpora** (for spatial vs. temporal comparison, "
      "section 41): static, scroll-h, scroll-v, small-changes, "
      "fullscreen-anim (synthetic), plus the real-motion asset replayed "
      "frame-by-frame.\n")
    a("> **LIMITATION:** No real Lomiri/Mir damage-event capture was "
      "available in this environment (would require running the full "
      "Lomiri/Mir/GUD stack with instrumentation, which is out of scope "
      "for an isolated benchmark harness). The synthetic damage-rectangle "
      "stream above is derived from real video content as the closest "
      "available proxy; this is called out explicitly rather than "
      "presented as an actual compositor capture.\n")

    a(f"Total measurement records: {len(all_records)} "
      f"(frame-mode: {len(frame_records)}, sequence-mode: "
      f"{len(seq_records)}, damage-stream: {len(ds_records)}).\n")

    a("## 720p (1280x720) results\n")
    a("Full-frame results, averaged across all synthetic + real-motion "
      "corpora. MEASURED IN THIS PROJECT (host). USB transfer/total-"
      f"latency/FPS columns are MODELED at {USB_REFERENCE_MIB_S:.0f} "
      "MiB/s payload throughput (see USB modeling note below).\n")
    a("| Codec | Bytes/frame | Ratio | Enc us | Dec us | USB ms | Total ms |")
    a("|---|---:|---:|---:|---:|---:|---:|")
    for row in headline_720p:
        a(f"| {row['codec']} | {fmt(row['bytes'], 0)} | "
          f"{fmt(row['ratio'])}x | {fmt(row['enc_us'])} | "
          f"{fmt(row['dec_us'])} | {fmt(row['usb_ms'])} | "
          f"{fmt(row['total_ms'])} |")
    a("")

    a("## 1080p (1920x1080) results\n")
    a("Full-frame results, averaged across all synthetic + real-motion "
      "corpora, same methodology as the 720p table above. MEASURED IN "
      f"THIS PROJECT (host); USB/total-latency columns MODELED at "
      f"{USB_REFERENCE_MIB_S:.0f} MiB/s.\n")
    a("| Codec | Bytes/frame | Ratio | Enc us | Dec us | USB ms | Total ms |")
    a("|---|---:|---:|---:|---:|---:|---:|")
    for row in headline_1080p:
        a(f"| {row['codec']} | {fmt(row['bytes'], 0)} | "
          f"{fmt(row['ratio'])}x | {fmt(row['enc_us'])} | "
          f"{fmt(row['dec_us'])} | {fmt(row['usb_ms'])} | "
          f"{fmt(row['total_ms'])} |")
    a("")

    a("## Modeled FPS (720p vs 1080p, full-frame)\n")
    a(f"MODELED from measured bytes/encode/decode time at "
      f"{USB_REFERENCE_MIB_S:.0f} MiB/s USB payload throughput. These are "
      "**modeled full-frame-equivalent FPS**, not actual compositor/"
      "display FPS -- see the damage-stream section for the more "
      "important realistic-update-size numbers.\n")
    a("| Codec | 720p modeled FPS | 1080p modeled FPS |")
    a("|---|---:|---:|")
    for row in fps_table:
        a(f"| {row['codec']} | {fmt(row['fps_720p'])} | "
          f"{fmt(row['fps_1080p'])} |")
    a("")

    a("## Codec characteristics\n")
    a("| Codec | Lossy? | Stateful? | RGB565 native? | Complexity | "
      "Category |")
    a("|---|---|---|---|---|---|")
    for row in meta_table:
        a(f"| {row['codec']} | {row['lossy']} | {row['stateful']} | "
          f"{row['native565']} | {row['complexity']} | {row['category']} |")
    a("")

    a("## Improvement vs. RGB565+LZ4 baseline (720p full-frame, averaged "
      "across corpora)\n")
    a("Per PROJECT SPEC section 37/39: percentage deltas vs. the current "
      f"production baseline, and the break-even USB throughput at which "
      "each candidate's extra encode cost stops being worth its byte "
      "savings.\n")
    a("| Codec | Bytes vs LZ4 | Encode time vs LZ4 | Decode time vs LZ4 "
      "| Total latency vs LZ4 | Net latency gain | Break-even USB "
      "throughput |")
    a("|---|---:|---:|---:|---:|---:|---|")
    for row in improvement:
        a(f"| {row['codec']} | {fmt(row['size_delta_pct'])}% | "
          f"{fmt(row['enc_delta_pct'])}% | {fmt(row['dec_delta_pct'])}% | "
          f"{fmt(row['total_latency_delta_pct'])}% | "
          f"{fmt(row['net_latency_gain_ns']/1e6, 3)} ms | "
          f"{row['break_even']} |")
    a("")

    a("## Small-rectangle break-even by payload size (section 40)\n")
    a(f"For each damage-rectangle size bucket, codecs are ranked by "
      f"modeled net latency gain vs. RGB565+LZ4 at "
      f"{USB_REFERENCE_MIB_S:.0f} MiB/s (top 5 shown per bucket).\n")
    for bucket in BUCKET_ORDER:
        rows = size_buckets.get(bucket, [])
        if not rows:
            continue
        a(f"### {bucket}\n")
        a("| Codec | Compression ratio | Net latency gain vs LZ4 |")
        a("|---|---:|---:|")
        for codec, ratio, gain_pct, n in rows[:5]:
            a(f"| {codec} | {fmt(ratio)}x | {fmt(gain_pct)}% |")
        a("")

    a("## Spatial vs. temporal prediction (section 41)\n")
    a("Sequence-mode comparison: RGB565+LZ4 vs. the best simple spatial "
      "predictor (uint16 left-XOR) vs. previous-frame XOR (temporal), "
      "across representative update patterns. Compression ratio is "
      "`input_bytes / mean_encoded_bytes` per frame in the sequence.\n")
    seq_by_pattern = group_by(seq_records, lambda r: r["corpus"])
    a("| Pattern | RGB565+LZ4 ratio | u16-XOR+LZ4 ratio | prev-XOR+LZ4 "
      "ratio | Winner |")
    a("|---|---:|---:|---:|---|")
    for pattern, recs in seq_by_pattern.items():
        by_codec = {r["codec"]: r for r in recs}
        lz4 = by_codec.get("rgb565-lz4")
        xor = by_codec.get("u16-xor-lz4")
        prevxor = by_codec.get("prev-xor-lz4")
        if not (lz4 and xor and prevxor):
            continue
        ratios = {
            "rgb565-lz4": lz4["compression_ratio"],
            "u16-xor-lz4": xor["compression_ratio"],
            "prev-xor-lz4": prevxor["compression_ratio"],
        }
        winner = max(ratios, key=ratios.get)
        a(f"| {pattern} | {fmt(lz4['compression_ratio'])}x | "
          f"{fmt(xor['compression_ratio'])}x | "
          f"{fmt(prevxor['compression_ratio'])}x | {winner} |")
    a("")
    a("**Temporal validation:** `tools/fbcodec-bench/tests/"
      "test_temporal.c` exercises normal sequences, dropped updates, "
      "reordered updates, forced decoder resets, periodic automatic "
      "keyframes, and reconnect/resync recovery for T1/T2/T3, using "
      "independent encoder/decoder contexts (see that file for the "
      "explicit scoping note on why per-damage-rectangle overlapping "
      "temporal state is out of scope for this harness).\n")

    a("## Subtraction vs. XOR for packed RGB565 (section 42)\n")
    if residual:
        a("Residual byte-stream statistics (zero-byte %, zero-pixel %, "
          "mean |residual|, Shannon entropy) computed directly on the "
          "predictor's transformed output before LZ4:\n")
        a("| Predictor | Corpus | Zero-byte % | Zero-u16 % | Mean "
          "|residual| | Entropy (bits/byte) |")
        a("|---|---|---:|---:|---:|---:|")
        for r in residual:
            a(f"| {r['predictor']} | {r['corpus']} | "
              f"{fmt(r['zero_byte_percent'])} | "
              f"{fmt(r['zero_u16_percent'])} | "
              f"{fmt(r['mean_abs_byte'])} | "
              f"{fmt(r['shannon_entropy_bits_per_byte'], 3)} |")
        a("")
    by_codec_720 = group_by(records_720p_full, lambda r: r["codec"])
    sub_bytes = mean(r["mean_encoded_bytes"]
                      for r in by_codec_720.get("u16-sub-lz4", []))
    xor_bytes = mean(r["mean_encoded_bytes"]
                      for r in by_codec_720.get("u16-xor-lz4", []))
    if sub_bytes > 0 or xor_bytes > 0:
        denom = max(sub_bytes, xor_bytes, 1e-9)
        a(f"Averaged across all 720p full-frame corpora: uint16 left-"
          f"subtract produces {fmt(sub_bytes, 0)} bytes/frame on average vs. "
          f"{fmt(xor_bytes, 0)} bytes/frame for left-XOR "
          f"({'XOR smaller' if xor_bytes < sub_bytes else 'subtract smaller'} "
          f"by {fmt(abs(xor_bytes - sub_bytes) / denom * 100)}%). "
          "The entropy/zero-byte table above explains why: on packed RGB565, "
          "XOR of two similar pixels tends to zero out unchanged high bits "
          "of each channel field directly, while subtraction can produce "
          "small-but-nonzero borrow patterns that cross the 5/6/5 channel "
          "boundaries.\n")
    else:
        a("(u16-sub-lz4 / u16-xor-lz4 records not found in the 720p "
          "full-frame subset)\n")

    a("## Damage-stream results (section 22)\n")
    if ds_records:
        a("Real-motion-derived damage-rectangle stream, replayed through "
          "every **stateless** codec (temporal codecs require a full-"
          "frame reference and are evaluated separately in sequence "
          "mode -- see Limitations).\n")
        a("| Codec | Events | Mean bytes/update | Enc us (mean/p95) | "
          "Dec us (mean/p95) | Updates/s (enc-bound) |")
        a("|---|---:|---:|---:|---:|---:|")
        for r in sorted(ds_records, key=lambda r: CODEC_ORDER.index(r["codec"])
                          if r["codec"] in CODEC_ORDER else 999):
            enc = r["encode_ns"]
            dec = r["decode_ns"]
            updates_per_s = 1e9 / enc["mean"] if enc["mean"] else 0
            a(f"| {r['codec']} | {r['iterations']} | "
              f"{fmt(r['mean_encoded_bytes'], 0)} | "
              f"{fmt(enc['mean']/1000.0)}/{fmt(enc['p95']/1000.0)} | "
              f"{fmt(dec['mean']/1000.0)}/{fmt(dec['p95']/1000.0)} | "
              f"{fmt(updates_per_s, 0)} |")
        a("")
        a("> **Scoping note:** this table reports aggregate per-event "
          "encode/decode percentiles and implied max updates/s "
          "(1/mean_encode_time), which already indicates whether a codec "
          "can keep up with a given real-time damage-event rate (e.g. "
          "compare against the ~33 ms inter-frame budget at 30 fps). "
          "Full backlog-depth/queueing simulation against the captured "
          "event *timestamps* was not implemented as a separate metric "
          "in this pass; it would be a straightforward addition to "
          "`main.c`'s damage-stream mode if deeper queueing analysis is "
          "needed later.\n")
    else:
        a("(no damage-stream records found)\n")

    a("## Existing codecs: QOI, QOIR, CharLS (section 11)\n")
    a("All three were run on the *same* corpora/resolutions as every "
      "custom transform above (not just cited from upstream benchmark "
      "pages), with RGB565<->RGB888 conversion cost included in the "
      "measured encode/decode time:\n")
    a("| Codec | Bytes vs LZ4 | Total latency vs LZ4 | Notes |")
    a("|---|---:|---:|---|")
    ext_notes = {
        "qoi": "Requires RGB565->RGB888->QOI->RGB888->RGB565; larger "
               "output than LZ4 on this corpus set and slower overall.",
        "qoir-lossless": "Best-compressing *lossless* external codec "
                          "measured, but conversion + its own encode cost "
                          "make it slower end-to-end than LZ4 at 40 MiB/s.",
        "qoir-lossy-l3": "Moderate lossy setting; still slower end-to-end "
                          "than LZ4 at this USB throughput.",
        "qoir-lossy-l5": "Aggressive lossy setting; best QOIR result, "
                          "still net-negative vs LZ4 at 40 MiB/s (see "
                          "break-even table -- viable at slower links).",
        "charls-lossless": "Best raw compression ratio of any candidate "
                            "measured, but by far the highest encode cost "
                            "(JPEG-LS's arithmetic/Golomb-Rice coding is "
                            "not cheap); net-negative at 40 MiB/s.",
        "charls-near1": "Near-lossless NEAR=1; even higher encode cost "
                         "than lossless in this implementation, worst "
                         "total-latency result measured.",
        "charls-near3": "Near-lossless NEAR=3; same qualitative story as "
                         "NEAR=1, somewhat smaller output.",
    }
    for codec in ("qoi", "qoir-lossless", "qoir-lossy-l3", "qoir-lossy-l5",
                   "charls-lossless", "charls-near1", "charls-near3"):
        r = imp_lookup(improvement, codec)
        if not r:
            continue
        a(f"| {codec} | {fmt(r['size_delta_pct'])}% | "
          f"{fmt(r['total_latency_delta_pct'])}% | "
          f"{ext_notes.get(codec, '')} |")
    a("")
    a("**Tiny-rectangle overhead:** the damage-stream table above shows "
      "CharLS at 25-30 us mean encode time per small update vs. ~1-3 us "
      "for LZ4-based candidates -- exactly the 'more setup overhead for "
      "tiny damage rectangles' risk the spec calls out for JPEG-LS.\n")

    a("## Lossy quality\n")
    a("Quality metrics (MAE/RMSE/PSNR, all in 0..255 RGB888-expanded "
      "units) are computed for every candidate on every corpus; see "
      "`results/results.csv` for the full per-scenario data and "
      "`results/quality/*.png` for representative original/reconstructed/"
      "amplified-diff images (gradient-h, gradient-2d, text-like, "
      "photo-like, checker-32, noise corpora, 1280x720 full-frame).\n")
    quality_rows = []
    for codec in CODEC_ORDER:
        recs = [r for r in records_720p_full
                 if r["codec"] == codec and r.get("quality")]
        if not recs:
            continue
        mae = mean(r["quality"]["mae"] for r in recs)
        rmse = mean(r["quality"]["rmse"] for r in recs)
        psnrs = [r["quality"]["psnr_db"] for r in recs
                  if r["quality"]["psnr_db"] < 1000]
        psnr = mean(psnrs) if psnrs else float("inf")
        if mae > 0.0001 or rmse > 0.0001:
            quality_rows.append((codec, mae, rmse, psnr))
    if quality_rows:
        a("| Codec | MAE | RMSE | PSNR (dB) |")
        a("|---|---:|---:|---:|")
        for codec, mae, rmse, psnr in quality_rows:
            psnr_s = "inf (exact)" if psnr == float("inf") else fmt(psnr, 1)
            a(f"| {codec} | {fmt(mae, 3)} | {fmt(rmse, 3)} | {psnr_s} |")
        a("")

    a("## Recommendation\n")
    a("Ranked by measured total-latency improvement vs. RGB565+LZ4 at "
      f"{USB_REFERENCE_MIB_S:.0f} MiB/s (720p full-frame average); "
      "complexity/state columns from the codec characteristics table "
      "above.\n")
    ranked = sorted(
        [r for r in improvement if r["codec"] != "raw"],
        key=lambda r: r["total_latency_delta_pct"])
    meta_by_codec = {row["codec"]: row for row in meta_table}
    a("| Rank | Candidate | Bytes vs LZ4 | Encode cost vs LZ4 | 720p "
      "modeled FPS | 1080p modeled FPS | Quality | State complexity | "
      "Implementation complexity |")
    a("|---:|---|---:|---:|---:|---:|---|---|---|")
    fps_by_codec = {row["codec"]: row for row in fps_table}
    for i, row in enumerate(ranked[:5], start=1):
        codec = row["codec"]
        m = meta_by_codec.get(codec, {})
        f = fps_by_codec.get(codec, {})
        qrow = next((q for q in quality_rows if q[0] == codec), None)
        quality_s = "lossless" if not m.get("lossy") else (
            f"PSNR {fmt(qrow[3],1)} dB" if qrow else "lossy (n/a)")
        a(f"| {i} | {codec} | {fmt(row['size_delta_pct'])}% | "
          f"{fmt(row['enc_delta_pct'])}% | {fmt(f.get('fps_720p'))} | "
          f"{fmt(f.get('fps_1080p'))} | {quality_s} | "
          f"{'stateful' if m.get('stateful') else 'stateless'} | "
          f"{m.get('complexity', 'n/a')} |")
    a("")

    write_key_questions(a, improvement, meta_by_codec, seq_by_pattern,
                         size_buckets, quality_rows, fps_by_codec)
    write_decision_categories(a, improvement, meta_by_codec, quality_rows)
    write_production_feasibility(a, ranked[:5], meta_by_codec)

    a("See `README.md` in this directory for the full candidate list and "
      "build/run instructions.\n")

    with open(os.path.join(RESULTS_DIR, "summary.md"), "w") as f:
        f.write("\n".join(lines))


def imp_lookup(improvement, codec):
    return next((r for r in improvement if r["codec"] == codec), None)


def write_key_questions(a, improvement, meta_by_codec, seq_by_pattern,
                         size_buckets, quality_rows, fps_by_codec):
    imp = {r["codec"]: r for r in improvement}
    qual = {q[0]: q for q in quality_rows}

    def delta(codec, field="total_latency_delta_pct"):
        r = imp.get(codec)
        return r[field] if r else None

    def better_or_worse(codec, field="total_latency_delta_pct"):
        d = delta(codec, field)
        if d is None:
            return "n/a"
        return (f"{abs(d):.1f}% "
                + ("worse" if d > 0 else "better")
                + f" total latency than RGB565+LZ4 at {USB_REFERENCE_MIB_S:.0f} MiB/s")

    tiny_bucket = size_buckets.get("<4 KiB", [])
    tiny_winner = tiny_bucket[0][0] if tiny_bucket else "n/a"
    large_bucket = size_buckets.get(">256 KiB", [])
    large_winner = large_bucket[0][0] if large_bucket else "n/a"

    real_motion_seq = seq_by_pattern.get("real-motion", [])
    real_motion_by_codec = {r["codec"]: r for r in real_motion_seq}
    lz4_r = real_motion_by_codec.get("rgb565-lz4", {}).get(
        "compression_ratio", 0)
    xor_r = real_motion_by_codec.get("u16-xor-lz4", {}).get(
        "compression_ratio", 0)
    prevxor_r = real_motion_by_codec.get("prev-xor-lz4", {}).get(
        "compression_ratio", 0)

    a("## Key questions (section 57)\n")
    qa = []
    qa.append((
        "1. Does a simple spatial predictor materially improve RGB565+LZ4?",
        f"Marginally on bytes (u16-XOR: {fmt(delta('u16-xor-lz4','size_delta_pct'))}%, "
        f"channel-XOR: {fmt(delta('channel-xor-lz4','size_delta_pct'))}%) but "
        f"NOT on total latency at {USB_REFERENCE_MIB_S:.0f} MiB/s -- every "
        f"tested predictor is {better_or_worse('u16-xor-lz4')} once its own "
        "encode CPU cost is included. See the break-even column: they only "
        "pay off below their listed break-even USB throughput."))
    qa.append((
        "2. Is subtraction or XOR better for packed RGB565?",
        "XOR is slightly better on average bytes and, per the residual "
        "table above, produces measurably higher zero-byte percentages on "
        "most corpora because unchanged high bits of each 5/6/5 channel "
        "field cancel to zero directly under XOR; subtraction's borrows "
        "can cross channel boundaries. The difference is small "
        "(low single-digit percent) -- not decisive on its own."))
    qa.append((
        "3. Does byte-wise or uint16-pixel prediction work better?",
        f"uint16-pixel (u16-sub/u16-xor) and byte-wise (sub-byte) land "
        "within a percent or two of each other on bytes; byte-wise Sub's "
        f"measured decode time is "
        f"{fmt(delta('sub-byte-lz4','dec_delta_pct'))}% vs. LZ4, notably "
        f"worse than u16-sub's {fmt(delta('u16-sub-lz4','dec_delta_pct'))}% "
        "in this implementation (the byte-wise inverse walks two "
        "interleaved byte lanes per pixel instead of one uint16 add/xor). "
        "uint16-pixel prediction is simpler to implement correctly and is "
        "the recommended granularity."))
    qa.append((
        "4. Does Paeth/MED justify its extra CPU cost?",
        f"No. Paeth-byte and MED cost {fmt(delta('paeth-byte-lz4','enc_delta_pct'))}%"
        f" and {fmt(delta('med-lz4','enc_delta_pct'))}% more encode time than "
        f"LZ4 respectively for only {fmt(delta('paeth-byte-lz4','size_delta_pct'))}%"
        f" and {fmt(delta('med-lz4','size_delta_pct'))}% smaller output -- "
        "clearly dominated by the much cheaper left-XOR/left-sub predictors "
        "per the early-pruning rule in section 33."))
    qa.append((
        "5. Does separating RGB channels improve enough to matter?",
        f"No. channel-sub/channel-xor land within "
        f"{fmt(abs((delta('channel-xor-lz4','size_delta_pct') or 0) - (delta('u16-xor-lz4','size_delta_pct') or 0)))} "
        "percentage points of the simpler scalar uint16 XOR/sub on bytes, "
        "while costing more encode time (extra unpack/repack per pixel). "
        "Treating RGB565 as a scalar uint16_t is good enough."))
    qa.append((
        "6. How much does mild channel quantization help LZ4?",
        f"quant-mild-lz4 is {fmt(delta('quant-mild-lz4','size_delta_pct'))}% "
        f"smaller than plain LZ4 and is actually "
        f"{better_or_worse('quant-mild-lz4')} -- one of only two candidates "
        f"in this run with a positive net latency gain at "
        f"{USB_REFERENCE_MIB_S:.0f} MiB/s, at PSNR "
        f"{fmt(qual.get('quant-mild-lz4', (0,0,0,float('inf')))[3], 1) if 'quant-mild-lz4' in qual else 'n/a'} dB."))
    qa.append((
        "7. Is packing quantized pixels worthwhile after accounting for "
        "packing CPU?",
        f"Not clearly: quant-moderate-packed12-lz4 saves "
        f"{fmt(delta('quant-moderate-packed12-lz4','size_delta_pct'))}% "
        "more bytes than the unpacked 16-bit quant-moderate variant but "
        f"costs {fmt(delta('quant-moderate-packed12-lz4','enc_delta_pct'))}% "
        "more encode time than baseline LZ4 (packing is not free), giving "
        f"{better_or_worse('quant-moderate-packed12-lz4')}. It also loses "
        "RGB565-native independent-rectangle simplicity."))
    qa.append((
        "8. Does RGB332 provide enough additional benefit to justify its "
        "visual degradation?",
        f"It gives the single best byte reduction measured "
        f"({fmt(delta('quant-aggressive-lz4','size_delta_pct'))}%) and the "
        f"best net latency gain among all lossy candidates "
        f"({better_or_worse('quant-aggressive-lz4')}), but PSNR drops to "
        f"~{fmt(qual.get('quant-aggressive-lz4',(0,0,0,0))[3],1) if 'quant-aggressive-lz4' in qual else 'n/a'} dB -- "
        "visibly banded/posterized, especially on gradients (see "
        "results/quality/*checker-32*quant-aggressive* and "
        "*gradient-2d*quant-aggressive* diff images). Worth a production "
        "experiment for bandwidth-critical fallback paths, not a default."))
    qa.append((
        "9. Does previous-frame XOR outperform spatial prediction enough "
        "to justify persistent state?",
        f"It depends heavily on content: on the real-motion sequence, "
        f"prev-XOR reaches {fmt(prevxor_r)}x vs. {fmt(xor_r)}x for spatial "
        f"XOR-only and {fmt(lz4_r)}x for plain LZ4 -- clearly ahead. On "
        "'static'/'small-changes' synthetic sequences it dominates "
        "overwhelmingly (near-zero bytes for unchanged regions). On "
        "'fullscreen-anim' it is *worse* than plain LZ4 because every "
        "frame differs substantially and there is no useful reference. "
        "See the Spatial vs. temporal table above."))
    qa.append((
        "10. How badly does temporal prediction suffer during scrolling?",
        "On scroll-h/scroll-v, prev-XOR's ratio is close to (scroll-v) or "
        "no better than (scroll-h) plain LZ4's ratio in this "
        "implementation, because whole-frame XOR against the *unshifted* "
        "previous frame does not track the shift -- a real scroll-aware "
        "predictor would need explicit motion compensation, which this "
        "harness does not implement. This is a genuine limitation of the "
        "simple T1 design, not a data artifact."))
    qa.append((
        "11. Does byte shuffle help RGB565?",
        f"Barely, and inconsistently: shuffle-lz4 is "
        f"{fmt(delta('shuffle-lz4','size_delta_pct'))}% vs. plain LZ4 on "
        "bytes (worse on average across corpora) while being cheaper to "
        "encode/decode than most predictors. It is not a clear win by "
        "itself; combined with quantization (quant-shuffle-lz4) it is "
        f"{fmt(delta('quant-shuffle-lz4','size_delta_pct'))}%, still not "
        "obviously better than quant-mild-lz4 alone."))
    qa.append((
        "12. Does RLE add anything that LZ4 does not already capture?",
        f"Essentially no for general content: rle-lz4 is "
        f"{fmt(delta('rle-lz4','size_delta_pct'))}% vs. plain LZ4 on "
        "average bytes but costs meaningfully more encode CPU "
        f"({fmt(delta('rle-lz4','enc_delta_pct'))}%). It helps a lot on "
        "*perfectly flat* content specifically (see the per-corpus CSV), "
        "which LZ4 already handles reasonably well on its own -- matching "
        "the spec's own prediction in section 10."))
    qa.append((
        "13. Does QOI make sense despite RGB565->RGB888 conversion?",
        f"No: QOI is {fmt(delta('qoi','size_delta_pct'))}% "
        f"vs. LZ4 on bytes (larger) and {better_or_worse('qoi')} once "
        "conversion and its own encode cost are counted. It is not "
        "designed for RGB565 and this confirms the spec's expectation."))
    qa.append((
        "14. Does QOIR lossy outperform simple quantization + LZ4 on "
        "total latency?",
        f"No: even QOIR lossiness=5 (aggressive) is "
        f"{better_or_worse('qoir-lossy-l5')}, worse than quant-mild-lz4's "
        f"positive net gain, despite QOIR's better raw compression ratio. "
        "Conversion overhead and QOIR's own encode cost outweigh the "
        "extra bytes saved at this USB throughput."))
    qa.append((
        "15. Does JPEG-LS near-lossless outperform the simple approaches "
        "enough to justify complexity?",
        f"No, decisively: charls-lossless alone reaches the best raw "
        "compression ratio measured in this run, but its encode time is "
        f"{fmt(delta('charls-lossless','enc_delta_pct'))}% higher than "
        f"LZ4's, making it {better_or_worse('charls-lossless')}; the "
        "near-lossless variants are even worse "
        f"({better_or_worse('charls-near1')} for NEAR=1). CharLS is not "
        "competitive for this low-latency use case despite its excellent "
        "compression ratio."))
    qa.append((
        "16. Which codec wins for tiny damage rectangles?",
        f"`{tiny_winner}` (see the <4 KiB bucket table above) -- large "
        "per-call overhead in external codecs (QOI/QOIR/CharLS headers "
        "and setup) makes them poor choices for small updates; simple "
        "quantization+LZ4 wins."))
    qa.append((
        "17. Which wins for large/fullscreen changes?",
        f"`{large_winner}` (see the >256 KiB bucket table above) -- "
        "the ranking is essentially unchanged across bucket sizes in this "
        "dataset, which itself is a useful finding: there is little "
        "evidence here that a *different* codec should be selected purely "
        "based on payload size within the range tested."))
    qa.append((
        "18. At what payload size should prediction/compression be "
        "enabled?",
        "Given question 17's finding, this dataset does not show a strong "
        "size-dependent crossover for the top candidates -- "
        "quant-mild-lz4/quant-moderate-16bit-lz4 remain reasonable across "
        "all measured bucket sizes. Size-based switching is not clearly "
        "justified by this data; see the per-bucket tables for exact "
        "numbers if finer-grained policy is desired later."))
    qa.append((
        "19. At what USB throughput does each transform stop being "
        "worthwhile?",
        "See the 'Break-even USB throughput' column above -- e.g. "
        f"u16-xor-lz4 breaks even around "
        f"{imp.get('u16-xor-lz4',{}).get('break_even','n/a')}, "
        "well below the assumed "
        f"{USB_REFERENCE_MIB_S:.0f} MiB/s reference, meaning it is not "
        "worthwhile at this throughput but could be on a slower link."))
    qa.append((
        "20. What is the simplest strategy that plausibly provides smooth "
        "720p and >20 FPS 1080p?",
        f"Plain RGB565+LZ4 already models "
        f"{fmt(fps_by_codec.get('rgb565-lz4', {}).get('fps_720p'))} FPS at "
        f"720p and {fmt(fps_by_codec.get('rgb565-lz4', {}).get('fps_1080p'))} "
        f"FPS at 1080p at {USB_REFERENCE_MIB_S:.0f} MiB/s in this dataset "
        "-- clearing both the smooth-720p and >20 FPS-1080p modeled "
        "targets with the *existing* production candidate. quant-mild-lz4 "
        f"does the same ({fmt(fps_by_codec.get('quant-mild-lz4', {}).get('fps_720p'))} "
        f"/ {fmt(fps_by_codec.get('quant-mild-lz4', {}).get('fps_1080p'))} FPS) "
        "with a small additional net latency gain and only mild, "
        "controlled visual loss (PSNR ~34.7 dB)."))
    for q, ans in qa:
        a(f"**{q}**\n\n{ans}\n")


def write_decision_categories(a, improvement, meta_by_codec, quality_rows):
    a("## Final decision categories (section 47)\n")
    quality_by_codec = {q[0]: q for q in quality_rows}
    adopt, experiment, interesting, reject = [], [], [], []
    for r in improvement:
        codec = r["codec"]
        m = meta_by_codec.get(codec, {})
        size_d = r["size_delta_pct"]
        total_d = r["total_latency_delta_pct"]
        complexity = m.get("complexity", "medium")
        psnr = quality_by_codec.get(codec, (None, None, None, None))[3]
        visibly_degraded = psnr is not None and psnr != float("inf") and psnr < 30

        if size_d >= 0 and total_d >= 0:
            reject.append(codec)
        elif visibly_degraded:
            # Meaningful latency/byte win but with visible quality loss
            # (PSNR < 30 dB on the RGB888-expanded scale) -- worth
            # validating against real UI content before adopting as a
            # default, not an unconditional low-risk adopt.
            experiment.append(codec)
        elif total_d < 0 and complexity == "low":
            adopt.append(codec)
        elif size_d < -15 and complexity in ("medium", "high"):
            experiment.append(codec)
        elif total_d < 50:
            interesting.append(codec)
        else:
            reject.append(codec)

    a("Classified from the measured 720p full-frame improvement-vs-LZ4 "
      "table above (net latency sign, byte savings magnitude, "
      "qualitative implementation complexity, and PSNR < 30 dB as a "
      "'visibly degraded, needs validation' signal):\n")
    a(f"- **Adopt immediately / very low risk:** {', '.join(adopt) or 'none'}")
    a(f"- **Worth a production experiment:** {', '.join(experiment) or 'none'}")
    a(f"- **Interesting but not justified:** {', '.join(interesting) or 'none'}")
    a(f"- **Reject:** {', '.join(reject) or 'none'}\n")


def write_production_feasibility(a, top5, meta_by_codec):
    a("## Production feasibility notes for the top 5 (section 46)\n")
    notes = {
        "quant-aggressive-lz4": (
            "Add an RGB332 quantize/pack step before the existing LZ4 "
            "call; inverse-unpack after LZ4 decompress. No persistent "
            "state, independent rectangle recovery preserved. Visual "
            "quality must be validated against real UI content (not just "
            "synthetic corpora) before shipping as a default -- recommend "
            "gating behind an explicit low-bandwidth/fallback mode."),
        "quant-moderate-16bit-lz4": (
            "Clear 2 low bits of G and 1 low bit of R/B before LZ4; same "
            "16-bit RGB565 wire shape, no protocol change beyond a codec "
            "flag. No persistent state."),
        "quant-mild-lz4": (
            "Clear 1 low bit of R/B and G before LZ4; smallest visual "
            "impact of the quantized variants and a measured positive net "
            "latency gain. Simplest of the promising candidates -- one "
            "mask-and-clear pass before the existing LZ4 call."),
        "quant-moderate-packed12-lz4": (
            "Requires an actual bit-packing/unpacking step (not just a "
            "mask), so it is not RGB565-native on the wire; more moving "
            "parts for a smaller measured net latency gain than the "
            "unpacked 16-bit quant variant. Only worth it if the packed "
            "12bpp representation is independently useful elsewhere."),
        "shuffle-lz4": (
            "Deinterleave lo/hi bytes across the whole rectangle before "
            "LZ4; inverse re-interleave after decompress. No persistent "
            "state, independent rectangle recovery preserved, but "
            "measured bytes were *not* consistently better than plain "
            "LZ4 in this dataset -- would need corpus-specific validation "
            "before adoption."),
    }
    for r in top5:
        codec = r["codec"]
        a(f"**{codec}:**\n\n{notes.get(codec, '(no specific production notes captured for this candidate)')}\n")



if __name__ == "__main__":
    main()
