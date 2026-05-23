#!/usr/bin/env python3
"""
plot_transfer.py — Plot one or more transfer-curve CSV files produced by phu_calibrate.

Default behavior is now calibration-dashboard oriented:
- Multiple curves on transfer and GR panels
- Family-delta panel when >=2 curves are provided
- CV pipeline panel (raw/effective/applied/stage)
- Automatic checkpoint callouts for separation diagnostics
"""

import argparse
import os
import sys
import re
from pathlib import Path
from typing import Dict, List, Tuple


DEFAULT_LABEL_ORDER = [
    "thresh10v0",
    "thresh3v5",
    "thresh2v8",
    "thresh2v0",
    "thresh0v0",
]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Plot Fairchild 670 transfer-curve CSV(s).")
    p.add_argument("csv_files", nargs="+", help="One or more CSV files from phu_calibrate --measure-transfer")
    p.add_argument("--reference", "-r", default=None, help="Optional reference CSV for single-curve overlay")
    p.add_argument("--output", "-o", default=None, help="Output image path (PNG). If omitted, displays interactively.")
    p.add_argument("--checkpoint", action="append", type=float, default=None,
                   help="Checkpoint dBFS for separation callouts (repeatable)")
    p.add_argument("--direction", choices=["up", "down", "all"], default="up",
                   help="Select which sweep direction rows to use from transfer CSVs")
    p.add_argument("--title", default="Fairchild 670 Transfer Calibration Dashboard")
    return p.parse_args()


def _detect_encoding(path: str) -> str:
    with open(path, "rb") as f:
        bom = f.read(2)
    return "utf-16" if bom in (b"\xff\xfe", b"\xfe\xff") else "utf-8"


def infer_label(path: str) -> str:
    stem = Path(path).stem
    m = re.match(r"(?i)curve[-_ ]?(\d+)$", stem)
    if m:
        return f"Curve-{m.group(1)}"
    for k in DEFAULT_LABEL_ORDER:
        if k in stem:
            return k
    return stem


def load_transfer_csv(path: str, direction: str = "up") -> Tuple[List[dict], Dict[str, str]]:
    meta: Dict[str, str] = {}
    rows: List[dict] = []
    encoding = _detect_encoding(path)
    with open(path, newline="", encoding=encoding) as f:
        header_seen = False
        for line in f:
            line = line.rstrip()
            if line.startswith("#"):
                for token in line.lstrip("# ").split():
                    if "=" in token:
                        k, v = token.split("=", 1)
                        meta[k] = v
                continue
            if not header_seen and "input_dbfs" in line:
                header_seen = True
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 4:
                continue
            try:
                has_direction = parts[0] in ("up", "down")
                offset = 1 if has_direction else 0
                row_direction = parts[0] if has_direction else "up"
                row = {
                    "sweep_direction": row_direction,
                    "input_dbfs": float(parts[offset + 0]),
                    "output_dbfs": float(parts[offset + 1]),
                    "gr_db": float(parts[offset + 2]),
                    "cv_volts": float(parts[offset + 3]),
                    "raw_cv_volts": float(parts[offset + 4]) if len(parts) > offset + 4 and parts[offset + 4] else 0.0,
                    "effective_cv_volts": float(parts[offset + 5]) if len(parts) > offset + 5 and parts[offset + 5] else 0.0,
                    "applied_cv_volts": float(parts[offset + 6]) if len(parts) > offset + 6 and parts[offset + 6] else 0.0,
                    "stage_cv_volts": float(parts[offset + 7]) if len(parts) > offset + 7 and parts[offset + 7] else 0.0,
                    "cv_clamp_ratio": float(parts[offset + 8]) if len(parts) > offset + 8 and parts[offset + 8] else 0.0,
                }
                rows.append(row)
            except ValueError:
                pass
    if direction in ("up", "down"):
        rows = [r for r in rows if r["sweep_direction"] == direction]

    if rows:
        return rows, meta

    # Fallback for hand-digitized Fairchild manual references:
    # lines like "-9,294...; -9,946..." (semicolon-separated, decimal commas).
    with open(path, newline="", encoding=encoding) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if ";" not in line:
                continue

            parts = [p.strip() for p in line.split(";")]
            if len(parts) < 2:
                continue

            try:
                input_dbfs = float(parts[0].replace(",", "."))
                output_dbfs = float(parts[1].replace(",", "."))
            except ValueError:
                continue

            rows.append({
                "sweep_direction": "up",
                "input_dbfs": input_dbfs,
                "output_dbfs": output_dbfs,
                "gr_db": input_dbfs - output_dbfs,
                "cv_volts": 0.0,
                "raw_cv_volts": 0.0,
                "effective_cv_volts": 0.0,
                "applied_cv_volts": 0.0,
                "stage_cv_volts": 0.0,
                "cv_clamp_ratio": 0.0,
            })

    if rows:
        meta["format"] = "xy_reference"

    if direction in ("up", "down"):
        rows = [r for r in rows if r["sweep_direction"] == direction]

    return rows, meta


def nearest_value(rows: List[dict], x: float, key: str) -> float:
    r = min(rows, key=lambda rr: abs(rr["input_dbfs"] - x))
    return r[key]


def order_key(label: str) -> int:
    try:
        return DEFAULT_LABEL_ORDER.index(label)
    except ValueError:
        return 999


def main() -> None:
    args = parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is required: pip install matplotlib", file=sys.stderr)
        sys.exit(1)

    series = []
    for path in args.csv_files:
        rows, meta = load_transfer_csv(path, args.direction)
        if not rows:
            print(f"No rows found in {path}", file=sys.stderr)
            continue
        series.append({"path": path, "rows": rows, "meta": meta, "label": infer_label(path)})

    if not series:
        print("No usable CSV data loaded.", file=sys.stderr)
        sys.exit(1)

    series.sort(key=lambda s: order_key(s["label"]))

    fig, axes = plt.subplots(2, 2, figsize=(14, 9))
    ax_transfer = axes[0][0]
    ax_gr = axes[0][1]
    ax_delta = axes[1][0]
    ax_cv = axes[1][1]

    # Panel 1: Transfer curves
    all_vals = []
    for s in series:
        in_vals = [r["input_dbfs"] for r in s["rows"]]
        out_vals = [r["output_dbfs"] for r in s["rows"]]
        all_vals += in_vals + out_vals
        ax_transfer.plot(in_vals, out_vals, marker="o", linewidth=1.3, markersize=3, label=s["label"])

    all_xy_reference = all(s["meta"].get("format") == "xy_reference" for s in series)
    if all_xy_reference:
        min_v, max_v = -10.0, 25.0
    else:
        min_v = min(all_vals) - 2
        max_v = max(all_vals) + 2
    ax_transfer.plot([min_v, max_v], [min_v, max_v], "--", color="gray", linewidth=0.9, label="Unity gain")
    ax_transfer.set_xlim(min_v, max_v)
    ax_transfer.set_ylim(min_v, max_v)
    ax_transfer.set_title("Input vs Output (all curves)")
    ax_transfer.set_xlabel("Input (dBFS)")
    ax_transfer.set_ylabel("Output (dBFS)")
    ax_transfer.grid(True, alpha=0.3)
    ax_transfer.legend(fontsize=8)

    # Panel 2: GR curves
    for s in series:
        in_vals = [r["input_dbfs"] for r in s["rows"]]
        gr_vals = [r["gr_db"] for r in s["rows"]]
        ax_gr.plot(in_vals, gr_vals, marker="o", linewidth=1.3, markersize=3, label=s["label"])
    ax_gr.axhline(0, color="gray", linestyle="--", linewidth=0.8)
    ax_gr.set_title("Gain Reduction (all curves)")
    ax_gr.set_xlabel("Input (dBFS)")
    ax_gr.set_ylabel("GR (dB)")
    ax_gr.grid(True, alpha=0.3)

    # Panel 3: Delta diagnostics
    name_map = {s["label"]: s for s in series}
    baseline = name_map.get("thresh10v0", series[0])
    base_rows = baseline["rows"]
    x_vals = [r["input_dbfs"] for r in base_rows]

    def delta_to_base(curve_label: str):
        if curve_label not in name_map:
            return None
        rows = name_map[curve_label]["rows"]
        ys = [nearest_value(rows, x, "gr_db") - nearest_value(base_rows, x, "gr_db") for x in x_vals]
        return ys

    for lbl in ("thresh3v5", "thresh2v8", "thresh2v0", "thresh0v0"):
        ys = delta_to_base(lbl)
        if ys is not None:
            ax_delta.plot(x_vals, ys, linewidth=1.4, label=f"{lbl} - thresh10v0")

    if "thresh3v5" in name_map and "thresh2v0" in name_map:
        y = [nearest_value(name_map["thresh2v0"]["rows"], x, "gr_db") - nearest_value(name_map["thresh3v5"]["rows"], x, "gr_db") for x in x_vals]
        ax_delta.plot(x_vals, y, "--", linewidth=1.5, label="(2.0 - 3.5) separation")
    if "thresh2v0" in name_map and "thresh0v0" in name_map:
        y = [nearest_value(name_map["thresh0v0"]["rows"], x, "gr_db") - nearest_value(name_map["thresh2v0"]["rows"], x, "gr_db") for x in x_vals]
        ax_delta.plot(x_vals, y, "--", linewidth=1.5, label="(0.0 - 2.0) separation")

    checkpoints = args.checkpoint if args.checkpoint is not None else [-9.0, -6.0]
    for cp in sorted(set(checkpoints)):
        ax_delta.axvline(cp, color="gray", linestyle=":", linewidth=0.8)
    ax_delta.set_title("Family deltas / separation diagnostics")
    ax_delta.set_xlabel("Input (dBFS)")
    ax_delta.set_ylabel("ΔGR (dB)")
    ax_delta.grid(True, alpha=0.3)
    delta_handles, delta_labels = ax_delta.get_legend_handles_labels()
    if delta_handles:
        ax_delta.legend(fontsize=8)

    # Panel 4: CV pipeline for strongest curve if present, else first
    cv_focus = name_map.get("thresh0v0", series[0])
    cv_rows = cv_focus["rows"]
    cv_x = [r["input_dbfs"] for r in cv_rows]
    ax_cv.plot(cv_x, [r["raw_cv_volts"] for r in cv_rows], label="raw CV")
    ax_cv.plot(cv_x, [r["effective_cv_volts"] for r in cv_rows], label="effective CV")
    ax_cv.plot(cv_x, [r["applied_cv_volts"] for r in cv_rows], label="applied CV")
    ax_cv.plot(cv_x, [r["stage_cv_volts"] for r in cv_rows], label="stage CV")
    clamp = [r["cv_clamp_ratio"] for r in cv_rows]
    ax_cv2 = ax_cv.twinx()
    ax_cv2.plot(cv_x, clamp, color="black", linestyle=":", label="clamp ratio")
    ax_cv2.set_ylabel("Clamp ratio")
    ax_cv2.set_ylim(0.0, 1.05)

    ax_cv.set_title(f"CV pipeline ({cv_focus['label']})")
    ax_cv.set_xlabel("Input (dBFS)")
    ax_cv.set_ylabel("Volts")
    ax_cv.grid(True, alpha=0.3)

    # Callouts
    callout_lines = []
    for cp in sorted(set(checkpoints)):
        c35 = name_map.get("thresh3v5")
        c20 = name_map.get("thresh2v0")
        c00 = name_map.get("thresh0v0")
        if c35 and c20:
            sep_35_20 = nearest_value(c20["rows"], cp, "gr_db") - nearest_value(c35["rows"], cp, "gr_db")
            callout_lines.append(f"cp {cp:+.1f} dBFS: 2.0-3.5 = {sep_35_20:+.3f} dB")
        if c20 and c00:
            sep_20_00 = nearest_value(c00["rows"], cp, "gr_db") - nearest_value(c20["rows"], cp, "gr_db")
            callout_lines.append(f"cp {cp:+.1f} dBFS: 0.0-2.0 = {sep_20_00:+.3f} dB")

    clamp_max = max(clamp) if clamp else 0.0
    callout_lines.append(f"{cv_focus['label']} max clamp ratio = {clamp_max:.3f}")
    if clamp_max < 0.01:
        callout_lines.append("WARNING: cvMax likely inactive in measured range")

    fig.text(0.02, 0.01, "\n".join(callout_lines), fontsize=9)
    fig.suptitle(args.title, fontsize=14)
    fig.tight_layout(rect=[0, 0.03, 1, 0.96])

    if args.output:
        plt.savefig(args.output, dpi=150)
        print(f"Saved: {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
