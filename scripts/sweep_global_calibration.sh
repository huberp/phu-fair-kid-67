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
#   ./scripts/sweep_global_calibration.sh [options]
#
# Options:
#   --build-dir DIR        Directory containing the phu_calibrate binary
#                          (default: build/linux-release/tools)
#   --output-dir DIR       Root folder for all sweep output
#                          (default: tmp/calibration_sweep)
#   --quick                Use a smaller parameter grid
#   --parallelism N        Max concurrent calibration processes (default: 10)
#   --mono-slack DB        Monotonicity slack in dB (default: 0.10)
#   --tail-min-slope DB    Minimum allowed tail slope in dB (default: -0.25)
#   --help                 Show this help and exit
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
BUILD_DIR="build/linux-release/tools"
OUTPUT_DIR="tmp/calibration_sweep"
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

# ── Argument parsing ──────────────────────────────────────────────────────────
usage() {
    cat << 'HELP'
Usage: sweep_global_calibration.sh [options]

Options:
  --build-dir DIR        Directory containing the phu_calibrate binary
                         (default: build/linux-release/tools)
  --output-dir DIR       Root folder for all sweep output
                         (default: tmp/calibration_sweep)
  --quick                Use a smaller parameter grid
  --parallelism N        Max concurrent calibration processes (default: 10)
  --mono-slack DB        Monotonicity slack in dB (default: 0.10)
  --tail-min-slope DB    Minimum allowed tail slope in dB (default: -0.25)
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
        --help|-h)        usage ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

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
CURVE_THRESHOLDS=(10.0 3.5 2.8 2.0 0.0)
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
echo ""

# ── Build flat task list (parallel arrays, no word-splitting issues) ──────────
# Each "task" is a set of corresponding entries in these arrays.
TASK_GAINS=()
TASK_KNEES=()
TASK_CVMAXES=()
TASK_CURVE_NAMES=()
TASK_THRESHOLDS=()
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
                TASK_THRESHOLDS+=("${CURVE_THRESHOLDS[$i]}")
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
    threshold="${TASK_THRESHOLDS[$idx]}"
    csv_out="${TASK_CSV_OUTS[$idx]}"

    pct=$(( 100 * (idx + 1) / TOTAL ))
    echo "[$pct%] Queue ($((idx+1))/$TOTAL): $curve_name gain=$gain knee=$knee cvMax=$cvMax"

    (
        "$EXE" --measure-transfer --position 1 \
            --threshold  "$threshold" \
            --sidechain-gain "$gain" \
            --cv-soft-knee   "$knee" \
            --cv-max         "$cvMax" \
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

# ── Analysis (Python) ─────────────────────────────────────────────────────────
# Export protocol thresholds so the Python heredoc can read them via os.environ.
export PHU_MONO_SLACK_DB="$MONO_SLACK_DB"
export PHU_TAIL_MIN_SLOPE_DB="$TAIL_MIN_SLOPE_DB"
export PHU_CHECKPOINT1_DB="$CHECKPOINT1_DB"
export PHU_CHECKPOINT2_DB="$CHECKPOINT2_DB"
export PHU_BASELINE_REL_MIN_CP1_DB="$BASELINE_REL_MIN_CP1_DB"
export PHU_BASELINE_REL_MIN_CP2_DB="$BASELINE_REL_MIN_CP2_DB"
export PHU_MIN_SEP_35_TO_20_CP1_DB="$MIN_SEP_35_TO_20_CP1_DB"
export PHU_MIN_SEP_35_TO_20_CP2_DB="$MIN_SEP_35_TO_20_CP2_DB"
export PHU_MIN_SEP_CP1_DB="$MIN_SEP_CP1_DB"
export PHU_MIN_SEP_CP2_DB="$MIN_SEP_CP2_DB"
export PHU_REGULARIZATION_WEIGHT="$REGULARIZATION_WEIGHT"
export PHU_OUTPUT_ROOT="$OUTPUT_ROOT"

python3 << 'PYEOF'
import csv
import json
import math
import os
import sys
from datetime import datetime, timezone

# ── Read protocol parameters from environment ──────────────────────────────────
def ef(name):  # read float env var
    return float(os.environ[name])

MONO_SLACK_DB            = ef("PHU_MONO_SLACK_DB")
TAIL_MIN_SLOPE_DB        = ef("PHU_TAIL_MIN_SLOPE_DB")
CHECKPOINT1_DB           = ef("PHU_CHECKPOINT1_DB")
CHECKPOINT2_DB           = ef("PHU_CHECKPOINT2_DB")
BASELINE_REL_MIN_CP1_DB  = ef("PHU_BASELINE_REL_MIN_CP1_DB")
BASELINE_REL_MIN_CP2_DB  = ef("PHU_BASELINE_REL_MIN_CP2_DB")
MIN_SEP_35_TO_20_CP1_DB  = ef("PHU_MIN_SEP_35_TO_20_CP1_DB")
MIN_SEP_35_TO_20_CP2_DB  = ef("PHU_MIN_SEP_35_TO_20_CP2_DB")
MIN_SEP_CP1_DB           = ef("PHU_MIN_SEP_CP1_DB")
MIN_SEP_CP2_DB           = ef("PHU_MIN_SEP_CP2_DB")
REGULARIZATION_WEIGHT    = ef("PHU_REGULARIZATION_WEIGHT")
OUTPUT_ROOT              = os.environ["PHU_OUTPUT_ROOT"]

BASELINE_GAIN  = 0.7
BASELINE_KNEE  = 0.75
BASELINE_CVMAX = 9.0

CURVES = [
    {"name": "thresh10v0", "weight": 1.0},
    {"name": "thresh3v5",  "weight": 1.0},
    {"name": "thresh2v8",  "weight": 1.0},
    {"name": "thresh2v0",  "weight": 1.5},
    {"name": "thresh0v0",  "weight": 2.0},
]

# ── Helpers ────────────────────────────────────────────────────────────────────

def load_csv(path):
    try:
        with open(path, newline="") as f:
            lines = [l for l in f if not l.startswith("#") and l.strip()]
        return list(csv.DictReader(lines))
    except OSError as exc:
        print(f"  WARNING: cannot read {path}: {exc}", file=sys.stderr)
        return []


def nearest_row(rows, target_db):
    return min(rows, key=lambda r: abs(float(r["input_dbfs"]) - target_db))


# ── Parse result directories ───────────────────────────────────────────────────

results = []
param_set_scores = {}

for entry in sorted(os.scandir(OUTPUT_ROOT), key=lambda e: e.name):
    if not entry.is_dir():
        continue
    parts = entry.name.split("_")
    # Expected format: gain_X_knee_Y_cvMax_Z
    if len(parts) != 6 or parts[0] != "gain" or parts[2] != "knee" or parts[4] != "cvMax":
        continue
    try:
        gain  = float(parts[1])
        knee  = float(parts[3])
        cvmax = float(parts[5])
    except ValueError:
        continue

    for curve in CURVES:
        csv_path = os.path.join(entry.path, curve["name"] + ".csv")
        if not os.path.exists(csv_path):
            print(f"  WARNING: missing {csv_path}", file=sys.stderr)
            continue

        rows = load_csv(csv_path)
        if len(rows) < 5:
            print(f"  WARNING: insufficient rows in {csv_path}", file=sys.stderr)
            continue

        all_outputs = [float(r["output_dbfs"]) for r in rows]
        all_cv      = [float(r["cv_volts"])     for r in rows]

        # Find the first point where CV > 0 (knee onset).
        knee_index = next((k for k, cv in enumerate(all_cv) if cv > 0.0), 0)

        raw_deltas = [
            all_outputs[i] - all_outputs[i - 1]
            for i in range(max(1, knee_index), len(all_outputs))
        ]
        mono_violations = sum(1 for d in raw_deltas if d < -MONO_SLACK_DB)

        last_outputs      = [float(r["output_dbfs"]) for r in rows[-4:]]
        tail_deltas       = [last_outputs[i] - last_outputs[i-1] for i in range(1, len(last_outputs))]
        abs_deltas        = [abs(d) for d in tail_deltas]
        tail_min_delta    = min(tail_deltas) if tail_deltas else 0.0
        score             = sum(abs_deltas)

        row_cp1 = nearest_row(rows, CHECKPOINT1_DB)
        row_cp2 = nearest_row(rows, CHECKPOINT2_DB)
        gr_cp1  = float(row_cp1["gain_reduction_db"])
        gr_cp2  = float(row_cp2["gain_reduction_db"])

        results.append({
            "gain": gain, "knee": knee, "cvMax": cvmax,
            "curveName": curve["name"], "curveWeight": curve["weight"],
            "score": round(score, 4),
            "monoViolations": mono_violations,
            "tailMinDelta": round(tail_min_delta, 4),
            "tailDownturn": tail_min_delta < TAIL_MIN_SLOPE_DB,
            "grAtCheckpoint1": round(gr_cp1, 4),
            "grAtCheckpoint2": round(gr_cp2, 4),
            "csvPath": csv_path,
        })

        key = f"{gain}_{knee}_{cvmax}"
        if key not in param_set_scores:
            param_set_scores[key] = {
                "gain": gain, "knee": knee, "cvMax": cvmax,
                "totalScore": 0.0, "weightedScore": 0.0,
                "perCurveScores": {}, "perCurveTailMin": {},
                "perCurveMonoViolations": {}, "perCurveGrAtCheckpoint1": {},
                "perCurveGrAtCheckpoint2": {},
                "hardFailures": [], "hardPass": True, "protocolScore": 0.0,
            }
        ps = param_set_scores[key]
        ps["perCurveScores"][curve["name"]]           = round(score, 4)
        ps["perCurveTailMin"][curve["name"]]          = round(tail_min_delta, 4)
        ps["perCurveMonoViolations"][curve["name"]]   = mono_violations
        ps["perCurveGrAtCheckpoint1"][curve["name"]]  = round(gr_cp1, 4)
        ps["perCurveGrAtCheckpoint2"][curve["name"]]  = round(gr_cp2, 4)
        ps["totalScore"]    += score
        ps["weightedScore"] += score * curve["weight"]

# ── Apply hard protocol constraints and compute protocol score ─────────────────

for ps in param_set_scores.values():
    failures = []
    for curve in CURVES:
        n = curve["name"]
        if n not in ps["perCurveMonoViolations"]:
            failures.append(f"missing_curve_{n}")
            continue
        if ps["perCurveMonoViolations"][n] > 0:
            failures.append(f"mono_{n}")
        if ps["perCurveTailMin"][n] < TAIL_MIN_SLOPE_DB:
            failures.append(f"tail_{n}")

    base_cp1    = ps["perCurveGrAtCheckpoint1"].get("thresh10v0", 0.0)
    base_cp2    = ps["perCurveGrAtCheckpoint2"].get("thresh10v0", 0.0)
    rel3v5_cp1  = ps["perCurveGrAtCheckpoint1"].get("thresh3v5",  0.0) - base_cp1
    rel2v8_cp1  = ps["perCurveGrAtCheckpoint1"].get("thresh2v8",  0.0) - base_cp1
    rel2v0_cp1  = ps["perCurveGrAtCheckpoint1"].get("thresh2v0",  0.0) - base_cp1
    rel0v0_cp1  = ps["perCurveGrAtCheckpoint1"].get("thresh0v0",  0.0) - base_cp1
    rel3v5_cp2  = ps["perCurveGrAtCheckpoint2"].get("thresh3v5",  0.0) - base_cp2
    rel2v8_cp2  = ps["perCurveGrAtCheckpoint2"].get("thresh2v8",  0.0) - base_cp2
    rel2v0_cp2  = ps["perCurveGrAtCheckpoint2"].get("thresh2v0",  0.0) - base_cp2
    rel0v0_cp2  = ps["perCurveGrAtCheckpoint2"].get("thresh0v0",  0.0) - base_cp2

    if rel3v5_cp1 < BASELINE_REL_MIN_CP1_DB:                   failures.append("baseline_rel_cp1")
    if rel3v5_cp2 < BASELINE_REL_MIN_CP2_DB:                   failures.append("baseline_rel_cp2")
    if (rel2v0_cp1 - rel3v5_cp1) < MIN_SEP_35_TO_20_CP1_DB:   failures.append("sep_cp1_thresh3v5_thresh2v0")
    if (rel0v0_cp1 - rel2v0_cp1) < MIN_SEP_CP1_DB:            failures.append("sep_cp1_thresh2v0_thresh0v0")
    if (rel2v0_cp2 - rel3v5_cp2) < MIN_SEP_35_TO_20_CP2_DB:   failures.append("sep_cp2_thresh3v5_thresh2v0")
    if (rel0v0_cp2 - rel2v0_cp2) < MIN_SEP_CP2_DB:            failures.append("sep_cp2_thresh2v0_thresh0v0")

    ps["hardFailures"] = list(set(failures))
    ps["hardPass"]     = len(ps["hardFailures"]) == 0

    reg = (abs(ps["gain"]  - BASELINE_GAIN) +
           abs(ps["knee"]  - BASELINE_KNEE) +
           abs(ps["cvMax"] - BASELINE_CVMAX) / 2.0)
    mid_cp1     = (rel3v5_cp1 + rel2v0_cp1) / 2.0
    mid_cp2     = (rel3v5_cp2 + rel2v0_cp2) / 2.0
    mid_penalty = abs(rel2v8_cp1 - mid_cp1) + abs(rel2v8_cp2 - mid_cp2)
    ps["protocolScore"] = round(
        ps["weightedScore"] + REGULARIZATION_WEIGHT * reg + 0.25 * mid_penalty, 6)

# ── Rank and report ────────────────────────────────────────────────────────────

ranked  = sorted(param_set_scores.values(),
                 key=lambda x: (0 if x["hardPass"] else 1, x["protocolScore"]))
passing = [r for r in ranked if r["hardPass"]]

print("Top 10 Parameter Sets (protocol-ranked)")
print()
print(f"{'Rank':>4} | {'Gain':>5} | {'Knee':>5} | {'CvMax':>5} | "
      f"{'Pass':>4} | {'ProtoScore':>10} | {'Weighted':>8} | {'Total':>6} | FailCount")
print("-" * 100)
for i, r in enumerate(ranked[:10]):
    pass_text = "yes" if r["hardPass"] else "no"
    print(f"{i+1:>4} | {r['gain']:>5.1f} | {r['knee']:>5.2f} | {r['cvMax']:>5.1f} | "
          f"{pass_text:>4} | {r['protocolScore']:>10.4f} | "
          f"{r['weightedScore']:>8.4f} | {r['totalScore']:>6.3f} | "
          f"{len(r['hardFailures']):>9}")

print()
best = passing[0] if passing else None
if best:
    print("RECOMMENDATION")
    print("-" * 80)
    print(f"Best passing configuration (protocol score = {best['protocolScore']:.4f}):")
    print()
    print(f"  sidechainAmplifierGain = {best['gain']}")
    print(f"  sidechainCvSoftKneeV   = {best['knee']}")
    print(f"  cvMaxV                 = {best['cvMax']}")
    print()
    print("Per-curve dip scores:")
    for curve in CURVES:
        score = best["perCurveScores"].get(curve["name"], "N/A")
        print(f"  {curve['name']}: {score} (weight={curve['weight']})")
else:
    print("No candidate passed hard protocol constraints.", file=sys.stderr)
print()

# ── Save JSON outputs ──────────────────────────────────────────────────────────

summary_path = os.path.join(OUTPUT_ROOT, "sweep_results.json")
with open(summary_path, "w") as f:
    json.dump(ranked, f, indent=2)
print(f"Full results saved to: {summary_path}")

protocol_summary = {
    "generatedAt": datetime.now(timezone.utc).isoformat(),
    "thresholds": {
        "checkpoint1Db":             CHECKPOINT1_DB,
        "checkpoint2Db":             CHECKPOINT2_DB,
        "monoSlackDb":               MONO_SLACK_DB,
        "tailMinSlopeDb":            TAIL_MIN_SLOPE_DB,
        "baselineRelMinCp1Db":       BASELINE_REL_MIN_CP1_DB,
        "baselineRelMinCp2Db":       BASELINE_REL_MIN_CP2_DB,
        "minSep35To20AtCheckpoint1Db": MIN_SEP_35_TO_20_CP1_DB,
        "minSep35To20AtCheckpoint2Db": MIN_SEP_35_TO_20_CP2_DB,
        "minSepAtCheckpoint1Db":     MIN_SEP_CP1_DB,
        "minSepAtCheckpoint2Db":     MIN_SEP_CP2_DB,
    },
    "passingCount": len(passing),
    "bestPassing": (
        {"gain": best["gain"], "knee": best["knee"],
         "cvMax": best["cvMax"], "protocolScore": best["protocolScore"]}
        if best else None
    ),
    "passingCandidates": [
        {"gain": r["gain"], "knee": r["knee"], "cvMax": r["cvMax"],
         "protocolScore": r["protocolScore"],
         "weightedScore": r["weightedScore"],
         "totalScore":    r["totalScore"]}
        for r in passing
    ],
}
protocol_path = os.path.join(OUTPUT_ROOT, "protocol_summary.json")
with open(protocol_path, "w") as f:
    json.dump(protocol_summary, f, indent=2)
print(f"Protocol summary saved to: {protocol_path}")

sys.exit(0 if passing else 2)
PYEOF
