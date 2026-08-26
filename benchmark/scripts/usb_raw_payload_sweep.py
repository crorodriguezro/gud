#!/usr/bin/env python3
"""usb_raw_payload_sweep.py - drive the existing xdisp-lz4-12800 raw bulk
payload probe (backport-4.9/env/xdisp-raw-payload-repeat-100.sh) across a
set of target payload sizes to get REAL measured OnePlus6->PiZero2W USB2
bulk-OUT throughput. This reuses the already-built, already-validated
diagnostic gud.ko variant purely as a raw-bulk-transport microbenchmark
(xdisp_probe_payload_length forces an exact-length raw bulk write per
atomic commit); it does NOT evaluate the separate LZ4-12800-cap policy
ticket. Requires the Pi gadget service temporarily reconfigured to
GUD_TEST_MAX_BUFFER_SIZE>=max(sizes) and GUD_TRANSFER_FORMAT=xrgb8888,
GUD_TEST_COMPRESSION=none (matches the diagnostic gud.ko's expected FB
format) -- see benchmark/results-final-codecs/usb/README.md for the exact
temporary drop-in used and the restoration procedure.
"""
import json
import os
import re
import statistics
import subprocess
import sys
import time

REPO = "/home/cristianr/Projects/linux-mobile/gud/backport-4.9"
SCRIPT = os.path.join(REPO, "env/xdisp-raw-payload-repeat-100.sh")

TRACE_RE = re.compile(
    r"GUD trace=\d+ bulk attempt=0 result=0 actual=(\d+) elapsed_us=(\d+)"
)

# (label, bytes) -- bytes chosen as the nearest multiple of 1280*4=5120
# (the atomic-commit test harness always builds a 1280-wide XRGB8888
# dumb buffer/FB; the raw probe requires payload_length % (width*bpp) == 0).
SIZES = [
    ("16KB", 15360),
    ("32KB", 30720),
    ("64KB", 66560),
    ("128KB", 133120),
    ("256KB", 261120),
    ("512KB", 522240),
    ("1MB", 1049600),
    ("2MB", 2099200),
    ("frame-rgb565-equiv-720x1280x2", 1843200),
    ("frame-xrgb8888-full-1280x720x4", 3686400),
]

TRANSACTIONS = int(os.environ.get("USB_SWEEP_TRANSACTIONS", "20"))


def run_one(label, nbytes):
    env = dict(os.environ)
    env["PHONE_HOST"] = "phablet@192.168.1.120"
    env["PHONE_SUDO_PASSWORD"] = "1026"
    env["PAYLOAD_LENGTH"] = str(nbytes)
    env["PAYLOAD_CAP"] = str(nbytes)
    env["TRANSACTION_COUNT"] = str(TRANSACTIONS)
    t0 = time.time()
    proc = subprocess.run(
        ["bash", SCRIPT], cwd=REPO, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120,
    )
    wall_s = time.time() - t0
    evdir = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else None
    result = {
        "label": label,
        "payload_bytes": nbytes,
        "transactions_requested": TRANSACTIONS,
        "wall_s": wall_s,
        "returncode": proc.returncode,
        "evidence_dir": evdir,
        "label_kind": "MEASURED",
    }
    if not evdir or not os.path.isdir(evdir):
        result["error"] = "no evidence dir; stderr=" + proc.stderr[-2000:]
        return result
    klog = os.path.join(evdir, "phone-kernel-full.log")
    elapsed_list = []
    if os.path.isfile(klog):
        with open(klog, "r", errors="replace") as f:
            for line in f:
                m = TRACE_RE.search(line)
                if m and int(m.group(1)) == nbytes:
                    elapsed_list.append(int(m.group(2)))
    result["samples_us"] = elapsed_list
    result["sample_count"] = len(elapsed_list)
    if elapsed_list:
        s = sorted(elapsed_list)
        n = len(s)
        def pct(p):
            idx = min(n - 1, int(round(p * (n - 1))))
            return s[idx]
        result["elapsed_us_mean"] = statistics.mean(s)
        result["elapsed_us_p50"] = pct(0.50)
        result["elapsed_us_p90"] = pct(0.90)
        result["elapsed_us_p95"] = pct(0.95)
        result["elapsed_us_p99"] = pct(0.99)
        result["elapsed_us_max"] = max(s)
        result["elapsed_us_min"] = min(s)
        mib = nbytes / (1024 * 1024)
        result["throughput_mib_s_mean"] = mib / (result["elapsed_us_mean"] / 1e6)
        result["throughput_mib_s_p50"] = mib / (result["elapsed_us_p50"] / 1e6)
        result["throughput_mib_s_p95_case"] = mib / (result["elapsed_us_p95"] / 1e6)
    # keep result-line count from stdout too for sanity cross-check
    stdout_path = os.path.join(evdir, "phone-stdout.log")
    if os.path.isfile(stdout_path):
        with open(stdout_path, errors="replace") as f:
            result["stdout_result_lines"] = sum(
                1 for l in f if "XDISP_RAW_REPEAT_RESULT" in l
            )
    return result


def main():
    out = []
    for label, nbytes in SIZES:
        print(f"== {label} ({nbytes} bytes) ==", file=sys.stderr)
        r = run_one(label, nbytes)
        out.append(r)
        print(json.dumps({k: v for k, v in r.items() if k != "samples_us"}, indent=2),
              file=sys.stderr)
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
