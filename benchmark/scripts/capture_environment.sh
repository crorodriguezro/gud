#!/usr/bin/env bash
# capture_environment.sh - records the exact host/software environment the
# benchmark was run under (PROJECT SPEC section 17: repeatability
# requirements; section 30: target-hardware caveats).
#
# IMPORTANT LIMITATION (see summary.md): this environment is an Apple
# Silicon (M1 Pro, Asahi Linux) aarch64 host, NOT a Raspberry Pi Zero 2 W
# or other in-project target device. Host measurements here are useful for
# algorithm/candidate *selection* but are explicitly NOT sufficient for
# final target-hardware conclusions (PROJECT SPEC section 30). No target
# ARM board was available in this environment.
set -euo pipefail

OUT="${1:-results/environment.json}"
mkdir -p "$(dirname "$OUT")"

json_escape() {
	python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'
}

CPU_MODEL=$(lscpu 2>/dev/null | grep "Model name" | head -1 | sed 's/Model name:\s*//' || echo "unknown")
ARCH=$(uname -m)
KERNEL=$(uname -r)
GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
MAXFREQ=$(lscpu 2>/dev/null | grep "CPU max MHz" | head -1 | sed 's/CPU max MHz:\s*//' || echo "unknown")
NPROC=$(nproc)
GCC_VERSION=$(gcc --version | head -1)
LZ4_REV="0774d05537f9762f838f7ab541b7765f1a729cb5 (vendored 1.10.0, see tools/fbcodec-bench/vendor/lz4)"
CHARLS_VERSION=$(rpm -q CharLS 2>/dev/null || echo "unknown")
CHARLS_LIB_PATH="/usr/lib64/libcharls.so.2 (no -devel package installed; headers vendored from upstream tag 2.4.3)"

cat > "$OUT" <<EOF
{
  "captured_at_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "host_role": "development/algorithm-selection host -- NOT the Raspberry Pi Zero 2 W target",
  "cpu_model": $(printf '%s' "$CPU_MODEL" | json_escape),
  "architecture": "$ARCH",
  "kernel": "$KERNEL",
  "cpu_governor": "$GOVERNOR",
  "cpu_max_mhz": "$MAXFREQ",
  "logical_cpus": $NPROC,
  "compiler": $(printf '%s' "$GCC_VERSION" | json_escape),
  "build_flags": "-std=gnu11 -O2 -Wall -Wextra (release/optimized build, see tools/fbcodec-bench/Makefile)",
  "lz4_provenance": $(printf '%s' "$LZ4_REV" | json_escape),
  "charls_package_version": $(printf '%s' "$CHARLS_VERSION" | json_escape),
  "charls_link": $(printf '%s' "$CHARLS_LIB_PATH" | json_escape),
  "qoi_provenance": "https://github.com/phoboslab/qoi (vendored master snapshot, MIT)",
  "qoir_provenance": "https://github.com/nigeltao/qoir (vendored main snapshot, Apache-2.0)",
  "target_hardware_available": false,
  "target_hardware_note": "No Raspberry Pi Zero 2 W (or equivalent ARM target) was available in this environment. All measurements below are HOST measurements on Apple Silicon (M1 Pro, Asahi Linux) and are marked MEASURED IN THIS PROJECT (host) in summary.md -- they are useful for candidate selection but section 30 of the spec explicitly requires target-hardware confirmation before a final production decision."
}
EOF

echo "wrote $OUT"
