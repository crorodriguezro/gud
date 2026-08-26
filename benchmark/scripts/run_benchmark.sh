#!/usr/bin/env bash
# run_benchmark.sh - orchestrates the full fbcodec-bench sweep required by
# PROJECT SPEC sections 13-22: synthetic + real-asset corpora, multiple
# resolutions, a damage-rectangle size sweep, a real-motion damage-stream
# replay, sequence-mode runs (spatial vs temporal, PROJECT SPEC section 41),
# and residual/entropy analysis (sections 42-43).
#
# Usage: scripts/run_benchmark.sh [--quick]
#   --quick reduces iterations/corpora for a fast smoke pass (used for CI-
#   style validation of the harness itself, not for reported numbers).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH_DIR="$ROOT_DIR/tools/fbcodec-bench"
BIN="$BENCH_DIR/build/fbcodec-bench"
RESULTS_DIR="$ROOT_DIR/results"
RAW_DIR="$RESULTS_DIR/raw"
QUALITY_DIR="$RESULTS_DIR/quality"
CSV="$RESULTS_DIR/results.csv"
REAL_ASSET="$ROOT_DIR/../backport-4.9/env/local/assets/xdisp-motion-1280x720-30fps-10s.rgb565le"

QUICK=0
if [[ "${1:-}" == "--quick" ]]; then
	QUICK=1
fi

echo "== building fbcodec-bench =="
make -C "$BENCH_DIR" -j"$(nproc)"

echo "== running unit tests =="
make -C "$BENCH_DIR" test

echo "== capturing environment =="
"$ROOT_DIR/scripts/capture_environment.sh" "$RESULTS_DIR/environment.json"

mkdir -p "$RAW_DIR" "$QUALITY_DIR"
rm -f "$CSV"

ITERATIONS=20
WARMUP=5
if [[ "$QUICK" == "1" ]]; then
	ITERATIONS=5
	WARMUP=2
fi

USB_LIST="30,35,40,45"

SYNTH_CORPORA=(flat-black flat-white flat-gray flat-ui checker-8 checker-32 \
	checker-64 gradient-h gradient-v gradient-2d text-like noise photo-like)

# Corpora that also get quality/*.png dumps (kept to a representative
# subset -- dumping every codec x every corpus would produce thousands of
# images for no additional evidentiary value).
QUALITY_CORPORA="gradient-h gradient-2d text-like photo-like checker-32 noise"

RESOLUTIONS=("1280x720" "1920x1080")
PORTRAIT_RESOLUTIONS=("720x1280" "1080x1920")

RECT_CLASSES=(16x16 32x32 64x64 128x64 128x128 256x256 640x100 half full)

run_count=0

rect_arg_for() {
	# Echoes the value to pass via --rect for a given rect class name and
	# parent WxH. "full" is passed through literally so fbcodec-bench
	# tags the resulting rect_class as "full-WxH" (unambiguous vs. any
	# fixed-size class); "half" is resolved to an explicit WxH here
	# because fbcodec-bench has no "half" keyword; every other class is
	# already a literal WxH string understood directly by --rect.
	local cls="$1" W="$2" H="$3"
	case "$cls" in
	full) echo "full" ;;
	half) echo "${W}x$((H / 2))" ;;
	*) echo "$cls" ;;
	esac
}

echo "== primary landscape sweep: ${RESOLUTIONS[*]} =="
for res in "${RESOLUTIONS[@]}"; do
	W="${res%x*}"; H="${res#*x}"
	for corpus in "${SYNTH_CORPORA[@]}"; do
		for rect in "${RECT_CLASSES[@]}"; do
			rect_arg="$(rect_arg_for "$rect" "$W" "$H")"
			out_json="$RAW_DIR/frame-${res}-${corpus}-${rect}.json"
			qdir=""
			if [[ " $QUALITY_CORPORA " == *" $corpus "* && "$rect" == "full" ]]; then
				qdir="$QUALITY_DIR/${res}-${corpus}"
			fi
			args=(--mode frame --codec all --synthetic "$corpus" \
				--width "$W" --height "$H" --rect "$rect_arg" \
				--iterations "$ITERATIONS" --warmup "$WARMUP" \
				--usb-mib-s "$USB_LIST" --verify --csv "$CSV" \
				--json "$out_json" --corpus-tag "$corpus")
			if [[ -n "$qdir" ]]; then
				mkdir -p "$qdir"
				args+=(--quality-dir "$qdir")
			fi
			"$BIN" "${args[@]}" > /dev/null
			run_count=$((run_count + 1))
		done
	done

	# Real-motion first-frame corpus across the same rect classes.
	if [[ -f "$REAL_ASSET" ]]; then
		for rect in "${RECT_CLASSES[@]}"; do
			rect_arg="$(rect_arg_for "$rect" "$W" "$H")"
			out_json="$RAW_DIR/frame-${res}-real-motion-${rect}.json"
			"$BIN" --mode frame --codec all --input "$REAL_ASSET" \
				--width "$W" --height "$H" --rect "$rect_arg" \
				--iterations "$ITERATIONS" --warmup "$WARMUP" \
				--usb-mib-s "$USB_LIST" --verify --csv "$CSV" \
				--json "$out_json" --corpus-tag "real-motion" > /dev/null
			run_count=$((run_count + 1))
		done
	else
		echo "WARNING: real asset not found at $REAL_ASSET; skipping real-motion frame corpus" >&2
	fi
done

if [[ "$QUICK" == "0" ]]; then
	echo "== supplementary portrait sweep (rect=full only): ${PORTRAIT_RESOLUTIONS[*]} =="
	for res in "${PORTRAIT_RESOLUTIONS[@]}"; do
		W="${res%x*}"; H="${res#*x}"
		for corpus in "${SYNTH_CORPORA[@]}"; do
			out_json="$RAW_DIR/frame-${res}-${corpus}-full.json"
			"$BIN" --mode frame --codec all --synthetic "$corpus" \
				--width "$W" --height "$H" --rect full \
				--iterations "$ITERATIONS" --warmup "$WARMUP" \
				--usb-mib-s "$USB_LIST" --verify --csv "$CSV" \
				--json "$out_json" --corpus-tag "$corpus" > /dev/null
			run_count=$((run_count + 1))
		done
	done
fi

echo "== sequence mode (spatial vs temporal, section 41) =="
SEQ_FRAMES=60
if [[ "$QUICK" == "1" ]]; then SEQ_FRAMES=10; fi
for pattern in static scroll-h scroll-v small-changes fullscreen-anim; do
	out_json="$RAW_DIR/sequence-1280x720-${pattern}.json"
	"$BIN" --mode sequence --codec all --synthetic-sequence "$pattern" \
		--width 1280 --height 720 --frames "$SEQ_FRAMES" \
		--usb-mib-s "$USB_LIST" --verify --csv "$CSV" --json "$out_json" \
		--corpus-tag "$pattern" > /dev/null
	run_count=$((run_count + 1))
done

if [[ -f "$REAL_ASSET" ]]; then
	REAL_SEQ_FRAMES=90
	if [[ "$QUICK" == "1" ]]; then REAL_SEQ_FRAMES=15; fi
	out_json="$RAW_DIR/sequence-1280x720-real-motion.json"
	"$BIN" --mode sequence --codec all --input "$REAL_ASSET" \
		--width 1280 --height 720 --frames "$REAL_SEQ_FRAMES" \
		--usb-mib-s "$USB_LIST" --verify --csv "$CSV" --json "$out_json" \
		--corpus-tag "real-motion" > /dev/null
	run_count=$((run_count + 1))
fi

echo "== damage-stream replay (section 22), derived from the real asset =="
if [[ -f "$REAL_ASSET" ]]; then
	DS_FRAMES=60
	if [[ "$QUICK" == "1" ]]; then DS_FRAMES=15; fi
	out_json="$RAW_DIR/damage-stream-real-motion.json"
	"$BIN" --mode damage-stream --codec all --input "$REAL_ASSET" \
		--width 1280 --height 720 --frames "$DS_FRAMES" --tile 32 \
		--frame-interval-ns 33333333 --usb-mib-s "$USB_LIST" --verify \
		--csv "$CSV" --json "$out_json" \
		--corpus-tag "real-motion-damage-stream" > /dev/null
	run_count=$((run_count + 1))
fi

echo "== residual/entropy analysis (sections 42-43) =="
RESIDUAL_JSON="$RESULTS_DIR/residual-analysis.json"
{
	echo "["
	first=1
	for predictor in u16-sub u16-xor channel-sub channel-xor paeth-pixel med sub-byte shuffle; do
		for corpus in gradient-h gradient-2d text-like photo-like noise checker-32; do
			line=$("$BIN" --mode residual --predictor "$predictor" \
				--synthetic "$corpus" --width 640 --height 360)
			if [[ "$first" == "0" ]]; then echo ","; fi
			first=0
			printf '%s' "$line"
		done
	done
	echo
	echo "]"
} > "$RESIDUAL_JSON"

echo "== done: $run_count fbcodec-bench invocations =="
echo "results.csv: $CSV"
echo "raw per-run JSON: $RAW_DIR"
echo "quality images: $QUALITY_DIR"
echo "residual analysis: $RESIDUAL_JSON"
