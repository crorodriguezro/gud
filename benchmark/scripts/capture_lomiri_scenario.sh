#!/usr/bin/env bash
# capture_lomiri_scenario.sh - streams N raw RGBA frames from the real
# lomiri-system-compositor (Mir) session on the project OnePlus 6 phone
# using mirscreencast, straight over an existing SSH ControlMaster socket
# to a local file (no data is ever written to the phone's own disk).
#
# This is the capture mechanism for PROJECT SPEC (next-phase) Phase 2:
# "capture the framebuffer at the point closest to what will actually be
# fed into the USB codec". mirscreencast is the least invasive capture
# path available in the current Mir/Lomiri integration (see
# docs/lomiri-gud-integration-options.md, option 2 "Screen-capture
# display bridge" -- explicitly retained there "as a useful fallback
# diagnostic and performance baseline"). The GUD-backed DisplayPort shim
# described in that document is an intentionally rolled-back POC and is
# not available for direct capture.
#
# mirscreencast only emits RGBA8888 (or BGRA8888); RGB565 conversion is
# performed afterwards, at ingestion time, by fbcodec-bench's existing
# rgb888_to_rgb565() truncating conversion (src/common.h) -- the same
# conversion this harness already uses for its QOI/QOIR/CharLS codec
# paths. See summary-next-phase.md "Capture methodology" for the full
# MEASURED/derived disclosure.
#
# Usage:
#   PW=<phone-sudo-or-login-password> \
#   capture_lomiri_scenario.sh <scenario> <width> <height> <n_frames> <cap_interval> <out_dir> [socket]
set -euo pipefail

SCENARIO="$1"; WIDTH="$2"; HEIGHT="$3"; NFRAMES="$4"; CAPINT="$5"; OUTDIR="$6"
SOCK="${7:-$HOME/.ssh/cm/phone.sock}"
PHONE_HOST="phablet@192.168.1.120"

mkdir -p "$OUTDIR"
RAW="$OUTDIR/${SCENARIO}.rgba"
ERR="$OUTDIR/${SCENARIO}.stderr.log"
META="$OUTDIR/${SCENARIO}.meta.json"

START_EPOCH_NS=$(date +%s%N)
ssh -F /dev/null -S "$SOCK" "$PHONE_HOST" \
  "mirscreencast -m /run/mir_socket -n $NFRAMES -s $WIDTH $HEIGHT --cap-interval $CAPINT --stdout" \
  > "$RAW" 2> "$ERR" || {
    echo "mirscreencast failed for $SCENARIO" >&2
    cat "$ERR" >&2
    exit 1
  }
END_EPOCH_NS=$(date +%s%N)

BYTES=$(stat -c%s "$RAW")
FRAME_BYTES=$((WIDTH * HEIGHT * 4))
ACTUAL_FRAMES=$((BYTES / FRAME_BYTES))
NOMINAL_HZ=60
FPS=$(python3 -c "print($NOMINAL_HZ / $CAPINT)")

cat > "$META" << JSON
{
  "scenario": "$SCENARIO",
  "capture_tool": "mirscreencast",
  "compositor": "lomiri-system-compositor (Mir 1.8.2, ubports:android2 platform)",
  "device": "OnePlus 6 (Halium 9 / Ubuntu Touch, 4.9.112-g6b190d86b aarch64)",
  "colorspace": "RGBA8888",
  "width": $WIDTH,
  "height": $HEIGHT,
  "frame_bytes": $FRAME_BYTES,
  "requested_frames": $NFRAMES,
  "actual_frames": $ACTUAL_FRAMES,
  "cap_interval": $CAPINT,
  "nominal_fps": $FPS,
  "capture_wall_start_epoch_ns": $START_EPOCH_NS,
  "capture_wall_end_epoch_ns": $END_EPOCH_NS,
  "capture_wall_duration_s": $(python3 -c "print(($END_EPOCH_NS-$START_EPOCH_NS)/1e9)"),
  "raw_file": "$(basename "$RAW")",
  "label": "MEASURED_LOMIRI_CAPTURE"
}
JSON

echo "captured $SCENARIO: $ACTUAL_FRAMES frames ($BYTES bytes) -> $RAW"
