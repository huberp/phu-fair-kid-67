#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# sweep_global_calibration.sh
#
# Unix/Linux equivalent of sweep_global_calibration.ps1.
# Runs phu_calibrate for every combination of sidechain_gain × cv_soft_knee ×
# cv_max × threshold curve, with up to $TASK_PARALLELISM concurrent processes.
#
# All CSV result files are written under $OUTPUT_DIR, one sub-directory per
# parameter set (gain_X_knee_Y_cvMax_Z/).  Two JSON summaries are produced at
# the top of $OUTPUT_DIR:
#   sweep_results.json    – full ranked table
#   protocol_summary.json – best candidate and protocol thresholds
#
# Requirements:
#   bash >= 4.3  (for 'wait -n')
#   python3      (for CSV analysis and JSON output)
#
# Usage:
#   ./calibration/scripts/sweep_global_calibration.sh [options]
#
# Options:
#   --build-dir DIR        Directory containing the phu_calibrate binary
#                          (default: build/linux-release/tools)
#   --output-dir DIR       Root folder for all sweep output
#                          (default: calibration/outputs/calibration_sweep)
#   --quick                Use a smaller parameter grid
#   --parallelism N        Max concurrent calibration processes (default: 10)
#   --mono-slack DB        Monotonicity slack in dB (default: 0.10)
#   --tail-min-slope DB    Minimum allowed tail slope in dB (default: -0.25)
#   --precision MODE       quick|confirm (default: quick)
#   --help                 Show this help and exit
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
BUILD_DIR="build/linux-release/tools"
OUTPUT_DIR="calibration/outputs/calibration_sweep"
QUICK=0
TASK_PARALLELISM=10
MONO_SLACK_DB=0.10
TAIL_MIN_SLOPE_DB=-0.25
CHECKPOINT1_DB=-9.0
CHECKPOINT2_DB=-6.0
BASELINE_REL_MIN_CP1_DB=-0.10
BASELINE_REL_MIN_CP2_DB=-0.45
MIN_SEP_35_TO_20_CP1_DB=-0.35
MIN_SEP_35_TO_20_CP2_DB=0.20
MIN_SEP_CP1_DB=0.50
MIN_SEP_CP2_DB=0.50
REGULARIZATION_WEIGHT=0.2
TRANSFER_MIN_DBFS=-60.0
TRANSFER_MAX_DBFS=6.0
TRANSFER_STEP_DB=3.0
TRANSFER_MEASURE_SAMPLES=1024
TRANSFER_SETTLE_MULT=10
TRANSFER_MIN_SETTLE_SEC=2.0
TRANSFER_SWEEP_MODE=up
TRANSFER_RESET_PER_LEVEL=0
PRECISION_MODE=quick

# ── Argument parsing ──────────────────────────────────────────────────────────
usage() {
    cat << 'HELP'
Usage: sweep_global_calibration.sh [options]

Options:
  --build-dir DIR        Directory containing the phu_calibrate binary
                         (default: build/linux-release/tools)
  --output-dir DIR       Root folder for all sweep output
                         (default: calibration/outputs/calibration_sweep)
  --quick                Use a smaller parameter grid
  --parallelism N        Max concurrent calibration processes (default: 10)
  --mono-slack DB        Monotonicity slack in dB (default: 0.10)
  --tail-min-slope DB    Minimum allowed tail slope in dB (default: -0.25)
  --precision MODE       quick|confirm (default: quick)
  --transfer-min-dbfs DB Minimum transfer input dBFS (default: -60)
  --transfer-max-dbfs DB Maximum transfer input dBFS (default: +6)
  --transfer-step-db DB  Transfer step in dB (default: 3)
  --help                 Show this help and exit
HELP
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)      BUILD_DIR="$2";       shift 2 ;;
        --output-dir)     OUTPUT_DIR="$2";      shift 2 ;;
        --quick)          QUICK=1;              shift   ;;
        --parallelism)    TASK_PARALLELISM="$2"; shift 2 ;;
        --mono-slack)     MONO_SLACK_DB="$2";   shift 2 ;;
        --tail-min-slope) TAIL_MIN_SLOPE_DB="$2"; shift 2 ;;
        --precision)      PRECISION_MODE="$2"; shift 2 ;;
        --transfer-min-dbfs) TRANSFER_MIN_DBFS="$2"; shift 2 ;;
        --transfer-max-dbfs) TRANSFER_MAX_DBFS="$2"; shift 2 ;;
        --transfer-step-db) TRANSFER_STEP_DB="$2"; shift 2 ;;
        --help|-h)        usage ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

# ── Path resolution ───────────────────────────────────────────────────────────
# Script lives under calibration/scripts after refactor.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$PROJECT_ROOT/$BUILD_DIR"
fi
if [[ "$OUTPUT_DIR" != /* ]]; then
    OUTPUT_DIR="$PROJECT_ROOT/$OUTPUT_DIR"
fi

# ── Validate ──────────────────────────────────────────────────────────────────
EXE="$BUILD_DIR/phu_calibrate"
if [[ ! -x "$EXE" ]]; then
    echo "Error: phu_calibrate not found or not executable at: $EXE" >&2
    echo "Build the project first or pass --build-dir." >&2
    exit 1
fi

if [[ $TASK_PARALLELISM -lt 1 ]]; then
    echo "Error: --parallelism must be >= 1" >&2
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo "Error: python3 is required for the analysis phase." >&2
    exit 1
fi

ANALYZER="$SCRIPT_DIR/analyze_calibration_sweep.py"
if [[ ! -f "$ANALYZER" ]]; then
    echo "Error: analyzer script not found at $ANALYZER" >&2
    exit 1
fi

case "$PRECISION_MODE" in
    quick)
        TRANSFER_MEASURE_SAMPLES=1024
        TRANSFER_SETTLE_MULT=10
        TRANSFER_MIN_SETTLE_SEC=2.0
        TRANSFER_SWEEP_MODE=up
        TRANSFER_RESET_PER_LEVEL=0
        ;;
    confirm)
        TRANSFER_MEASURE_SAMPLES=4096
        TRANSFER_SETTLE_MULT=20
        TRANSFER_MIN_SETTLE_SEC=4.0
        TRANSFER_SWEEP_MODE=both
        TRANSFER_RESET_PER_LEVEL=0
        ;;
    *)
        echo "Error: --precision must be quick or confirm" >&2
        exit 1
        ;;
esac

# ── Parameter grids ───────────────────────────────────────────────────────────
if [[ $QUICK -eq 1 ]]; then
    GAIN_VALUES=(0.6 0.8 1.0 1.2)
    KNEE_VALUES=(0.5 0.75 1.25)
    CVMAX_VALUES=(8.0 9.0 10.0)
else
    GAIN_VALUES=(0.6 0.7 0.8 0.9 1.0 1.1 1.2)
    KNEE_VALUES=(0.5 0.75 1.0 1.25 1.5)
    CVMAX_VALUES=(8.0 9.0 10.0)
fi

CURVE_NAMES=(thresh10v0 thresh3v5 thresh2v8 thresh2v0 thresh0v0)
CURVE_THRESHOLDS_AC=(10.0 8.0 3.0 5.0 0.5)
CURVE_THRESHOLDS_DC=(0.0 1.5 0.2 1.0 1.8)
CURVE_WEIGHTS=(1.0 1.0 1.0 1.5 2.0)

# ── Setup output directory ────────────────────────────────────────────────────
mkdir -p "$OUTPUT_DIR"
OUTPUT_ROOT="$(cd "$OUTPUT_DIR" && pwd)"

echo "Starting global calibration sweep..."
echo "Configuration:"
echo "  Executable:       $EXE"
echo "  Output dir:       $OUTPUT_ROOT"
echo "  Quick mode:       $QUICK"
echo "  Task parallelism: $TASK_PARALLELISM"
echo "  Gain values:      [${GAIN_VALUES[*]}]"
echo "  Knee values:      [${KNEE_VALUES[*]}]"
echo "  CV Max values:    [${CVMAX_VALUES[*]}]"
echo "  Curves:           [${CURVE_NAMES[*]}]"
echo "  Precision mode:   $PRECISION_MODE"
echo "  Transfer range:   [$TRANSFER_MIN_DBFS, $TRANSFER_MAX_DBFS] step $TRANSFER_STEP_DB dB"
echo "  Sweep mode:       $TRANSFER_SWEEP_MODE"
echo "  Measure samples:  $TRANSFER_MEASURE_SAMPLES"
echo ""

# ── Build flat task list (parallel arrays, no word-splitting issues) ──────────
# Each "task" is a set of corresponding entries in these arrays.
TASK_GAINS=()
TASK_KNEES=()
TASK_CVMAXES=()
TASK_CURVE_NAMES=()
TASK_THRESHOLDS_AC=()
TASK_THRESHOLDS_DC=()
TASK_CSV_OUTS=()

for gain in "${GAIN_VALUES[@]}"; do
    for knee in "${KNEE_VALUES[@]}"; do
        for cvMax in "${CVMAX_VALUES[@]}"; do
            param_dir="$OUTPUT_ROOT/gain_${gain}_knee_${knee}_cvMax_${cvMax}"
            mkdir -p "$param_dir"
            for i in "${!CURVE_NAMES[@]}"; do
                TASK_GAINS+=("$gain")
                TASK_KNEES+=("$knee")
                TASK_CVMAXES+=("$cvMax")
                TASK_CURVE_NAMES+=("${CURVE_NAMES[$i]}")
                TASK_THRESHOLDS_AC+=("${CURVE_THRESHOLDS_AC[$i]}")
                TASK_THRESHOLDS_DC+=("${CURVE_THRESHOLDS_DC[$i]}")
                TASK_CSV_OUTS+=("$param_dir/${CURVE_NAMES[$i]}.csv")
            done
        done
    done
done

TOTAL=${#TASK_GAINS[@]}
N_PARAM_SETS=$(( TOTAL / ${#CURVE_NAMES[@]} ))
echo "Total tasks: $TOTAL ($N_PARAM_SETS param sets × ${#CURVE_NAMES[@]} curves)"
echo ""

# ── Sliding parallel window ───────────────────────────────────────────────────
# Uses a simple counter: increment on launch, decrement via 'wait -n' (bash 4.3+).
pending=0

for (( idx=0; idx<TOTAL; idx++ )); do
    gain="${TASK_GAINS[$idx]}"
    knee="${TASK_KNEES[$idx]}"
    cvMax="${TASK_CVMAXES[$idx]}"
    curve_name="${TASK_CURVE_NAMES[$idx]}"
    threshold_ac="${TASK_THRESHOLDS_AC[$idx]}"
    threshold_dc="${TASK_THRESHOLDS_DC[$idx]}"
    csv_out="${TASK_CSV_OUTS[$idx]}"
    reset_arg=()
    if [[ $TRANSFER_RESET_PER_LEVEL -eq 1 ]]; then
        reset_arg=(--transfer-reset-per-level)
    fi

    pct=$(( 100 * (idx + 1) / TOTAL ))
    echo "[$pct%] Queue ($((idx+1))/$TOTAL): $curve_name ac=$threshold_ac dc=$threshold_dc gain=$gain knee=$knee cvMax=$cvMax"

    (
        "$EXE" --measure-transfer --position 1 \
            --threshold-ac "$threshold_ac" \
            --threshold-dc "$threshold_dc" \
            --sidechain-gain "$gain" \
            --cv-soft-knee   "$knee" \
            --cv-max         "$cvMax" \
            --transfer-min-dbfs "$TRANSFER_MIN_DBFS" \
            --transfer-max-dbfs "$TRANSFER_MAX_DBFS" \
            --transfer-step-db "$TRANSFER_STEP_DB" \
            --transfer-measure-samples "$TRANSFER_MEASURE_SAMPLES" \
            --transfer-settle-multiplier "$TRANSFER_SETTLE_MULT" \
            --transfer-min-settle-sec "$TRANSFER_MIN_SETTLE_SEC" \
            --transfer-sweep-mode "$TRANSFER_SWEEP_MODE" \
            "${reset_arg[@]}" \
            --output         "$csv_out" 2>&1 \
        || echo "WARN: phu_calibrate failed for $curve_name gain=$gain knee=$knee cvMax=$cvMax" >&2
    ) &

    pending=$(( pending + 1 ))

    # When the window is full, wait for one job to finish before launching more.
    if [[ $pending -ge $TASK_PARALLELISM ]]; then
        wait -n 2>/dev/null || true
        pending=$(( pending - 1 ))
    fi
done

# Drain all remaining background jobs.
wait

echo ""
echo "All tasks finished. Running analysis..."
echo ""

python3 "$ANALYZER" \
    --output-root "$OUTPUT_ROOT" \
    --checkpoint1-db "$CHECKPOINT1_DB" \
    --checkpoint2-db "$CHECKPOINT2_DB" \
    --mono-slack-db "$MONO_SLACK_DB" \
    --tail-min-slope-db "$TAIL_MIN_SLOPE_DB" \
    --baseline-rel-min-cp1-db "$BASELINE_REL_MIN_CP1_DB" \
    --baseline-rel-min-cp2-db "$BASELINE_REL_MIN_CP2_DB" \
    --min-sep-35-to-20-cp1-db "$MIN_SEP_35_TO_20_CP1_DB" \
    --min-sep-35-to-20-cp2-db "$MIN_SEP_35_TO_20_CP2_DB" \
    --min-sep-cp1-db "$MIN_SEP_CP1_DB" \
    --min-sep-cp2-db "$MIN_SEP_CP2_DB" \
    --regularization-weight "$REGULARIZATION_WEIGHT"
