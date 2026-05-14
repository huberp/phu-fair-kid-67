#!/usr/bin/env python3
import argparse
import csv
import json
import math
import os
import statistics
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Analyze calibration sweep CSV outputs.")
    p.add_argument("--output-root", required=True)
    p.add_argument("--checkpoint1-db", type=float, default=-9.0)
    p.add_argument("--checkpoint2-db", type=float, default=-6.0)
    p.add_argument("--mono-slack-db", type=float, default=0.10)
    p.add_argument("--tail-min-slope-db", type=float, default=-0.25)
    p.add_argument("--baseline-rel-min-cp1-db", type=float, default=-0.10)
    p.add_argument("--baseline-rel-min-cp2-db", type=float, default=-0.45)
    p.add_argument("--min-sep-35-to-20-cp1-db", type=float, default=-0.35)
    p.add_argument("--min-sep-35-to-20-cp2-db", type=float, default=0.20)
    p.add_argument("--min-sep-cp1-db", type=float, default=0.50)
    p.add_argument("--min-sep-cp2-db", type=float, default=0.50)
    p.add_argument("--regularization-weight", type=float, default=0.2)
    p.add_argument("--baseline-gain", type=float, default=0.7)
    p.add_argument("--baseline-knee", type=float, default=0.75)
    p.add_argument("--baseline-cvmax", type=float, default=9.0)
    p.add_argument("--min-parameter-influence", type=float, default=0.05)
    p.add_argument("--min-cv-clamp-utilization", type=float, default=0.01)
    return p.parse_args()


CURVES = [
    {"name": "thresh10v0", "weight": 1.0},
    {"name": "thresh3v5", "weight": 1.0},
    {"name": "thresh2v8", "weight": 1.0},
    {"name": "thresh2v0", "weight": 1.5},
    {"name": "thresh0v0", "weight": 2.0},
]


def load_csv(path: Path):
    rows = []
    with path.open(newline="", encoding="utf-8") as f:
        data = [line for line in f if line.strip() and not line.startswith("#")]
    reader = csv.DictReader(data)
    for row in reader:
        if "input_dbfs" not in row or "output_dbfs" not in row:
            continue
        if row.get("sweep_direction", "") not in ("", "up"):
            continue
        try:
            rows.append(
                {
                    "input_dbfs": float(row["input_dbfs"]),
                    "output_dbfs": float(row["output_dbfs"]),
                    "gr_db": float(row["gain_reduction_db"]),
                    "cv_volts": float(row["cv_volts"]),
                    "raw_cv_volts": float(row.get("raw_cv_volts", 0.0) or 0.0),
                    "effective_cv_volts": float(row.get("effective_cv_volts", 0.0) or 0.0),
                    "applied_cv_volts": float(row.get("applied_cv_volts", 0.0) or 0.0),
                    "stage_cv_volts": float(row.get("stage_cv_volts", 0.0) or 0.0),
                    "cv_clamp_ratio": float(row.get("cv_clamp_ratio", 0.0) or 0.0),
                }
            )
        except ValueError:
            pass
    return rows


def nearest_row(rows, target_db):
    return min(rows, key=lambda r: abs(r["input_dbfs"] - target_db))


def analyze_curve(rows, args):
    all_outputs = [r["output_dbfs"] for r in rows]
    all_cv = [r["cv_volts"] for r in rows]

    knee_index = next((k for k, cv in enumerate(all_cv) if cv > 0.0), 0)
    raw_deltas = [all_outputs[i] - all_outputs[i - 1] for i in range(max(1, knee_index), len(all_outputs))]
    mono_violations = sum(1 for d in raw_deltas if d < -args.mono_slack_db)

    tail_outputs = [r["output_dbfs"] for r in rows[-4:]]
    tail_deltas = [tail_outputs[i] - tail_outputs[i - 1] for i in range(1, len(tail_outputs))] if len(tail_outputs) >= 2 else [0.0]
    tail_min_delta = min(tail_deltas)
    score = sum(abs(d) for d in tail_deltas)

    cp1 = nearest_row(rows, args.checkpoint1_db)
    cp2 = nearest_row(rows, args.checkpoint2_db)

    clamp_mean = statistics.fmean(r["cv_clamp_ratio"] for r in rows) if rows else 0.0
    cv_applied_mean = statistics.fmean(r["applied_cv_volts"] for r in rows) if rows else 0.0
    cv_stage_mean = statistics.fmean(r["stage_cv_volts"] for r in rows) if rows else 0.0

    return {
        "score": round(score, 4),
        "monoViolations": mono_violations,
        "tailMinDelta": round(tail_min_delta, 4),
        "tailDownturn": tail_min_delta < args.tail_min_slope_db,
        "grAtCheckpoint1": round(cp1["gr_db"], 4),
        "grAtCheckpoint2": round(cp2["gr_db"], 4),
        "clampMean": round(clamp_mean, 6),
        "cvAppliedMean": round(cv_applied_mean, 6),
        "cvStageMean": round(cv_stage_mean, 6),
        "rows": rows,
    }


def parse_param_set(dirname: str):
    parts = dirname.split("_")
    if len(parts) != 6 or parts[0] != "gain" or parts[2] != "knee" or parts[4] != "cvMax":
        return None
    try:
        return float(parts[1]), float(parts[3]), float(parts[5])
    except ValueError:
        return None


def main() -> int:
    args = parse_args()
    root = Path(args.output_root)
    if not root.exists():
        print(f"Error: output root not found: {root}", file=sys.stderr)
        return 1

    param_set_scores = {}
    results = []

    for entry in sorted(root.iterdir(), key=lambda p: p.name):
        if not entry.is_dir():
            continue
        parsed = parse_param_set(entry.name)
        if parsed is None:
            continue
        gain, knee, cvmax = parsed
        key = f"{gain}_{knee}_{cvmax}"

        if key not in param_set_scores:
            param_set_scores[key] = {
                "gain": gain,
                "knee": knee,
                "cvMax": cvmax,
                "totalScore": 0.0,
                "weightedScore": 0.0,
                "perCurveScores": {},
                "perCurveTailMin": {},
                "perCurveMonoViolations": {},
                "perCurveGrAtCheckpoint1": {},
                "perCurveGrAtCheckpoint2": {},
                "perCurveClampMean": {},
                "perCurveCvAppliedMean": {},
                "perCurveCvStageMean": {},
                "hardFailures": [],
                "hardPass": True,
                "protocolScore": 0.0,
            }

        for curve in CURVES:
            csv_path = entry / f"{curve['name']}.csv"
            if not csv_path.exists():
                continue
            rows = load_csv(csv_path)
            if len(rows) < 5:
                continue

            m = analyze_curve(rows, args)
            results.append(
                {
                    "gain": gain,
                    "knee": knee,
                    "cvMax": cvmax,
                    "curveName": curve["name"],
                    "curveWeight": curve["weight"],
                    "csvPath": str(csv_path),
                    **{k: m[k] for k in ["score", "monoViolations", "tailMinDelta", "tailDownturn", "grAtCheckpoint1", "grAtCheckpoint2", "clampMean", "cvAppliedMean", "cvStageMean"]},
                }
            )

            ps = param_set_scores[key]
            ps["perCurveScores"][curve["name"]] = m["score"]
            ps["perCurveTailMin"][curve["name"]] = m["tailMinDelta"]
            ps["perCurveMonoViolations"][curve["name"]] = m["monoViolations"]
            ps["perCurveGrAtCheckpoint1"][curve["name"]] = m["grAtCheckpoint1"]
            ps["perCurveGrAtCheckpoint2"][curve["name"]] = m["grAtCheckpoint2"]
            ps["perCurveClampMean"][curve["name"]] = m["clampMean"]
            ps["perCurveCvAppliedMean"][curve["name"]] = m["cvAppliedMean"]
            ps["perCurveCvStageMean"][curve["name"]] = m["cvStageMean"]
            ps["totalScore"] += m["score"]
            ps["weightedScore"] += m["score"] * curve["weight"]

    for ps in param_set_scores.values():
        failures = []
        for c in CURVES:
            n = c["name"]
            if n not in ps["perCurveMonoViolations"]:
                failures.append(f"missing_curve_{n}")
                continue
            if ps["perCurveMonoViolations"][n] > 0:
                failures.append(f"mono_{n}")
            if ps["perCurveTailMin"][n] < args.tail_min_slope_db:
                failures.append(f"tail_{n}")

        base_cp1 = ps["perCurveGrAtCheckpoint1"].get("thresh10v0", 0.0)
        base_cp2 = ps["perCurveGrAtCheckpoint2"].get("thresh10v0", 0.0)

        rel3v5_cp1 = ps["perCurveGrAtCheckpoint1"].get("thresh3v5", 0.0) - base_cp1
        rel2v8_cp1 = ps["perCurveGrAtCheckpoint1"].get("thresh2v8", 0.0) - base_cp1
        rel2v0_cp1 = ps["perCurveGrAtCheckpoint1"].get("thresh2v0", 0.0) - base_cp1
        rel0v0_cp1 = ps["perCurveGrAtCheckpoint1"].get("thresh0v0", 0.0) - base_cp1

        rel3v5_cp2 = ps["perCurveGrAtCheckpoint2"].get("thresh3v5", 0.0) - base_cp2
        rel2v8_cp2 = ps["perCurveGrAtCheckpoint2"].get("thresh2v8", 0.0) - base_cp2
        rel2v0_cp2 = ps["perCurveGrAtCheckpoint2"].get("thresh2v0", 0.0) - base_cp2
        rel0v0_cp2 = ps["perCurveGrAtCheckpoint2"].get("thresh0v0", 0.0) - base_cp2

        if rel3v5_cp1 < args.baseline_rel_min_cp1_db:
            failures.append("baseline_rel_cp1")
        if rel3v5_cp2 < args.baseline_rel_min_cp2_db:
            failures.append("baseline_rel_cp2")
        if (rel2v0_cp1 - rel3v5_cp1) < args.min_sep_35_to_20_cp1_db:
            failures.append("sep_cp1_thresh3v5_thresh2v0")
        if (rel0v0_cp1 - rel2v0_cp1) < args.min_sep_cp1_db:
            failures.append("sep_cp1_thresh2v0_thresh0v0")
        if (rel2v0_cp2 - rel3v5_cp2) < args.min_sep_35_to_20_cp2_db:
            failures.append("sep_cp2_thresh3v5_thresh2v0")
        if (rel0v0_cp2 - rel2v0_cp2) < args.min_sep_cp2_db:
            failures.append("sep_cp2_thresh2v0_thresh0v0")

        ps["hardFailures"] = sorted(set(failures))
        ps["hardPass"] = len(ps["hardFailures"]) == 0

        reg = abs(ps["gain"] - args.baseline_gain) + abs(ps["knee"] - args.baseline_knee) + abs(ps["cvMax"] - args.baseline_cvmax) / 2.0
        mid_cp1 = (rel3v5_cp1 + rel2v0_cp1) / 2.0
        mid_cp2 = (rel3v5_cp2 + rel2v0_cp2) / 2.0
        mid_penalty = abs(rel2v8_cp1 - mid_cp1) + abs(rel2v8_cp2 - mid_cp2)
        ps["protocolScore"] = round(ps["weightedScore"] + args.regularization_weight * reg + 0.25 * mid_penalty, 6)

        clamp_vals = list(ps["perCurveClampMean"].values())
        applied_vals = list(ps["perCurveCvAppliedMean"].values())
        stage_vals = list(ps["perCurveCvStageMean"].values())
        ps["cvUtilization"] = {
            "meanClampRatio": round(statistics.fmean(clamp_vals), 6) if clamp_vals else 0.0,
            "maxClampRatio": round(max(clamp_vals), 6) if clamp_vals else 0.0,
            "meanAppliedCv": round(statistics.fmean(applied_vals), 6) if applied_vals else 0.0,
            "meanStageCv": round(statistics.fmean(stage_vals), 6) if stage_vals else 0.0,
        }

        ps["familyDeltas"] = {
            "cp1": {
                "thresh3v5_minus_thresh10v0": round(rel3v5_cp1, 6),
                "thresh2v8_minus_thresh10v0": round(rel2v8_cp1, 6),
                "thresh2v0_minus_thresh10v0": round(rel2v0_cp1, 6),
                "thresh0v0_minus_thresh10v0": round(rel0v0_cp1, 6),
                "thresh2v0_minus_thresh3v5": round(rel2v0_cp1 - rel3v5_cp1, 6),
                "thresh0v0_minus_thresh2v0": round(rel0v0_cp1 - rel2v0_cp1, 6),
            },
            "cp2": {
                "thresh3v5_minus_thresh10v0": round(rel3v5_cp2, 6),
                "thresh2v8_minus_thresh10v0": round(rel2v8_cp2, 6),
                "thresh2v0_minus_thresh10v0": round(rel2v0_cp2, 6),
                "thresh0v0_minus_thresh10v0": round(rel0v0_cp2, 6),
                "thresh2v0_minus_thresh3v5": round(rel2v0_cp2 - rel3v5_cp2, 6),
                "thresh0v0_minus_thresh2v0": round(rel0v0_cp2 - rel2v0_cp2, 6),
            },
        }

    ranked = sorted(param_set_scores.values(), key=lambda x: (0 if x["hardPass"] else 1, x["protocolScore"]))
    passing = [r for r in ranked if r["hardPass"]]

    influences = {}
    for param in ("gain", "knee", "cvMax"):
        groups = defaultdict(list)
        for r in ranked:
            groups[r[param]].append(r["protocolScore"])
        means = {str(k): statistics.fmean(v) for k, v in groups.items() if v}
        influence = max(means.values()) - min(means.values()) if means else 0.0
        influences[param] = {
            "meanProtocolScoreByValue": means,
            "influence": round(influence, 6),
            "identifiable": influence >= args.min_parameter_influence,
        }

    best = passing[0] if passing else (ranked[0] if ranked else None)
    mean_clamp_all = statistics.fmean([r["cvUtilization"]["meanClampRatio"] for r in ranked]) if ranked else 0.0

    identifiability_gate = {
        "minParameterInfluence": args.min_parameter_influence,
        "minCvClampUtilization": args.min_cv_clamp_utilization,
        "parameterInfluence": influences,
        "meanClampRatioAcrossParamSets": round(mean_clamp_all, 6),
        "cvMaxSweepLikelyInactive": (influences["cvMax"]["influence"] < args.min_parameter_influence) and (mean_clamp_all < args.min_cv_clamp_utilization),
    }
    identifiability_gate["pass"] = all(v["identifiable"] for v in influences.values())

    sweep_results = root / "sweep_results.json"
    with sweep_results.open("w", encoding="utf-8") as f:
        json.dump(ranked, f, indent=2)

    protocol_summary = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "thresholds": {
            "checkpoint1Db": args.checkpoint1_db,
            "checkpoint2Db": args.checkpoint2_db,
            "monoSlackDb": args.mono_slack_db,
            "tailMinSlopeDb": args.tail_min_slope_db,
            "baselineRelMinCp1Db": args.baseline_rel_min_cp1_db,
            "baselineRelMinCp2Db": args.baseline_rel_min_cp2_db,
            "minSep35To20AtCheckpoint1Db": args.min_sep_35_to_20_cp1_db,
            "minSep35To20AtCheckpoint2Db": args.min_sep_35_to_20_cp2_db,
            "minSepAtCheckpoint1Db": args.min_sep_cp1_db,
            "minSepAtCheckpoint2Db": args.min_sep_cp2_db,
        },
        "passingCount": len(passing),
        "bestPassing": (
            {
                "gain": best["gain"],
                "knee": best["knee"],
                "cvMax": best["cvMax"],
                "protocolScore": best["protocolScore"],
                "cvUtilization": best["cvUtilization"],
                "familyDeltas": best["familyDeltas"],
            }
            if best
            else None
        ),
        "passingCandidates": [
            {
                "gain": r["gain"],
                "knee": r["knee"],
                "cvMax": r["cvMax"],
                "protocolScore": r["protocolScore"],
                "weightedScore": r["weightedScore"],
                "totalScore": r["totalScore"],
                "cvUtilization": r["cvUtilization"],
            }
            for r in passing
        ],
        "identifiabilityGate": identifiability_gate,
    }

    protocol_summary_path = root / "protocol_summary.json"
    with protocol_summary_path.open("w", encoding="utf-8") as f:
        json.dump(protocol_summary, f, indent=2)

    sensitivity_matrix = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "baseline": {
            "gain": args.baseline_gain,
            "knee": args.baseline_knee,
            "cvMax": args.baseline_cvmax,
        },
        "influence": influences,
    }
    sensitivity_path = root / "sensitivity_matrix.json"
    with sensitivity_path.open("w", encoding="utf-8") as f:
        json.dump(sensitivity_matrix, f, indent=2)

    protocol_report = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "summary": {
            "totalParamSets": len(ranked),
            "passingParamSets": len(passing),
            "identifiabilityPass": identifiability_gate["pass"],
            "cvMaxSweepLikelyInactive": identifiability_gate["cvMaxSweepLikelyInactive"],
        },
        "bestCandidate": best,
        "identifiability": identifiability_gate,
    }
    protocol_report_path = root / "protocol_report.json"
    with protocol_report_path.open("w", encoding="utf-8") as f:
        json.dump(protocol_report, f, indent=2)

    md_path = root / "protocol_report.md"
    with md_path.open("w", encoding="utf-8") as f:
        f.write("# Calibration Protocol Report\n\n")
        f.write(f"- Generated: {protocol_report['generatedAt']}\n")
        f.write(f"- Param sets: {len(ranked)}\n")
        f.write(f"- Passing sets: {len(passing)}\n")
        f.write(f"- Identifiability gate: {'PASS' if identifiability_gate['pass'] else 'FAIL'}\n")
        f.write(f"- cvMax likely inactive: {'YES' if identifiability_gate['cvMaxSweepLikelyInactive'] else 'NO'}\n\n")
        if best:
            f.write("## Best Candidate\n\n")
            f.write(f"- gain: {best['gain']}\n")
            f.write(f"- knee: {best['knee']}\n")
            f.write(f"- cvMax: {best['cvMax']}\n")
            f.write(f"- protocolScore: {best['protocolScore']}\n")
            f.write(f"- mean clamp ratio: {best['cvUtilization']['meanClampRatio']}\n")
            f.write("\n### Family deltas at checkpoints\n\n")
            for cp in ("cp1", "cp2"):
                f.write(f"- {cp}: {best['familyDeltas'][cp]}\n")
            f.write("\n")

        f.write("## Parameter Influence (Sensitivity)\n\n")
        for p, info in influences.items():
            f.write(f"- {p}: influence={info['influence']} identifiable={'yes' if info['identifiable'] else 'no'}\n")
        f.write("\n")

        f.write("## Required Artifacts\n\n")
        f.write("- protocol_summary.json\n")
        f.write("- protocol_report.json\n")
        f.write("- sensitivity_matrix.json\n")
        f.write("- family plots (multi-curve + deltas)\n")

    print(f"Full results saved to: {sweep_results}")
    print(f"Protocol summary saved to: {protocol_summary_path}")
    print(f"Sensitivity matrix saved to: {sensitivity_path}")
    print(f"Protocol report saved to: {protocol_report_path}")
    print(f"Markdown report saved to: {md_path}")

    if not identifiability_gate["pass"]:
        print("Identifiability gate failed: one or more parameters have negligible influence.", file=sys.stderr)
        return 3

    return 0 if passing else 2


if __name__ == "__main__":
    raise SystemExit(main())
