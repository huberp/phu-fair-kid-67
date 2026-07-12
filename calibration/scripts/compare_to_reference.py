"""compare_to_reference.py

Compare a phu_calibrate transfer-curve CSV (dBFS) against a Fairchild manual
reference curve CSV (dBm) by applying the K offset and interpolating.

Usage:
    python compare_to_reference.py \\
        --plugin-csv   calibration/outputs/curve_family/curve3.csv \\
        --reference-csv calibration/reference/Transfere-Curve-3.csv \\
        --k-offset 19.2 \\
        --output-png   calibration/outputs/curve_family/curve3_compare.png \\
        --output-stats calibration/outputs/curve_family/curve3_stats.json

Output:
    PNG  — overlay plot of plugin vs reference in dBm
    JSON — {"rms_error_db": ..., "max_error_db": ..., "mean_error_db": ...}
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def _load_plugin_csv(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Load a phu_calibrate transfer CSV.

    Expected columns (comma-separated, first row = header):
        sweep_direction, input_dbfs, output_dbfs, gain_reduction_db, cv_volts, ...

    Returns (input_dbfs, output_dbfs) as 1-D float arrays, ascending by input.
    """
    rows = []
    with open(path, encoding="utf-8") as fh:
        header = None
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if header is None:
                header = [h.lower() for h in parts]
                continue
            if len(parts) < len(header):
                continue
            row = dict(zip(header, parts))
            try:
                rows.append((float(row["input_dbfs"]), float(row["output_dbfs"])))
            except (KeyError, ValueError):
                continue

    if not rows:
        raise ValueError(f"No valid rows found in plugin CSV: {path}")

    arr = np.array(sorted(rows, key=lambda r: r[0]))
    return arr[:, 0], arr[:, 1]


def _load_reference_csv(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Load a Fairchild manual reference CSV.

    Format: European CSV — semicolon separator, decimal comma.
    Two columns: input_dBm ; output_dBm
    Optionally has a one-line text header (e.g. "Curve 3") which is skipped.

    Returns (input_dBm, output_dBm) as 1-D float arrays, ascending by input.
    """
    rows = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            parts = [p.strip().replace(",", ".") for p in line.split(";")]
            if len(parts) < 2:
                continue
            try:
                rows.append((float(parts[0]), float(parts[1])))
            except ValueError:
                continue  # skip header / comment lines

    if not rows:
        raise ValueError(f"No valid rows found in reference CSV: {path}")

    arr = np.array(sorted(rows, key=lambda r: r[0]))
    return arr[:, 0], arr[:, 1]


# ---------------------------------------------------------------------------
# Core comparison
# ---------------------------------------------------------------------------

def compare(
    plugin_input_dbfs: np.ndarray,
    plugin_output_dbfs: np.ndarray,
    ref_input_dbm: np.ndarray,
    ref_output_dbm: np.ndarray,
    k_offset: float,
) -> dict:
    """Interpolate reference at plugin input levels and compute error stats.

    Returns a dict with keys:
        plugin_input_dbm, plugin_output_dbm,
        ref_output_interp_dbm, error_db,
        rms_error_db, max_error_db, mean_error_db
    """
    plugin_input_dbm  = plugin_input_dbfs  + k_offset
    plugin_output_dbm = plugin_output_dbfs + k_offset

    # Clamp interpolation to the reference range.
    mask = (plugin_input_dbm >= ref_input_dbm[0]) & (plugin_input_dbm <= ref_input_dbm[-1])
    if mask.sum() == 0:
        raise ValueError(
            f"Plugin sweep range [{plugin_input_dbm[0]:.1f}, {plugin_input_dbm[-1]:.1f}] dBm "
            f"does not overlap reference range [{ref_input_dbm[0]:.1f}, {ref_input_dbm[-1]:.1f}] dBm."
        )

    pi  = plugin_input_dbm[mask]
    po  = plugin_output_dbm[mask]
    ref = np.interp(pi, ref_input_dbm, ref_output_dbm)
    err = po - ref

    return {
        "plugin_input_dbm":       pi,
        "plugin_output_dbm":      po,
        "ref_input_dbm":          ref_input_dbm,
        "ref_output_dbm":         ref_output_dbm,
        "ref_output_interp_dbm":  ref,
        "error_db":               err,
        "rms_error_db":           float(np.sqrt(np.mean(err**2))),
        "max_error_db":           float(np.max(np.abs(err))),
        "mean_error_db":          float(np.mean(err)),
    }


# ---------------------------------------------------------------------------
# Plot
# ---------------------------------------------------------------------------

def plot_comparison(result: dict, plugin_csv: Path, ref_csv: Path, output_png: Path) -> None:
    fig, axes = plt.subplots(2, 1, figsize=(8, 8), constrained_layout=True)

    # --- Transfer curve panel ---
    ax = axes[0]
    ax.plot(result["ref_input_dbm"], result["ref_output_dbm"],
            color="tab:blue", linewidth=1.5, label="Reference")
    ax.plot(result["plugin_input_dbm"], result["plugin_output_dbm"],
            color="tab:orange", linewidth=1.5, linestyle="--", label="Plugin")
    ax.set_xlabel("Input (dBm)")
    ax.set_ylabel("Output (dBm)")
    ax.set_title(f"Transfer Curve — {plugin_csv.stem} vs {ref_csv.stem}")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.35)

    # --- Error panel ---
    ax = axes[1]
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.fill_between(result["plugin_input_dbm"], result["error_db"],
                    alpha=0.3, color="tab:red")
    ax.plot(result["plugin_input_dbm"], result["error_db"],
            color="tab:red", linewidth=1.2,
            label=f"Error  rms={result['rms_error_db']:.3f} dB  "
                  f"max={result['max_error_db']:.3f} dB")
    ax.set_xlabel("Input (dBm)")
    ax.set_ylabel("Error (dB)")
    ax.set_title("Plugin − Reference Error")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.35)

    output_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_png, dpi=150)
    plt.close(fig)
    print(f"Plot saved: {output_png}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plugin-csv",    required=True, type=Path,
                    help="phu_calibrate transfer CSV (dBFS)")
    ap.add_argument("--reference-csv", required=True, type=Path,
                    help="Fairchild manual reference CSV (dBm, semicolon-separated)")
    ap.add_argument("--k-offset",      type=float, default=19.2,
                    help="dBm = dBFS + K  (default 19.2)")
    ap.add_argument("--output-png",    type=Path, default=None,
                    help="Output overlay PNG path")
    ap.add_argument("--output-stats",  type=Path, default=None,
                    help="Output JSON stats path")
    args = ap.parse_args()

    try:
        plug_in, plug_out   = _load_plugin_csv(args.plugin_csv)
        ref_in,  ref_out    = _load_reference_csv(args.reference_csv)
    except (FileNotFoundError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    result = compare(plug_in, plug_out, ref_in, ref_out, args.k_offset)

    stats = {
        "plugin_csv":    str(args.plugin_csv),
        "reference_csv": str(args.reference_csv),
        "k_offset_db":   args.k_offset,
        "n_points":      int(len(result["plugin_input_dbm"])),
        "rms_error_db":  result["rms_error_db"],
        "max_error_db":  result["max_error_db"],
        "mean_error_db": result["mean_error_db"],
    }

    print(f"RMS error : {stats['rms_error_db']:.4f} dB")
    print(f"Max error : {stats['max_error_db']:.4f} dB")
    print(f"Mean error: {stats['mean_error_db']:.4f} dB")

    if args.output_stats:
        args.output_stats.parent.mkdir(parents=True, exist_ok=True)
        args.output_stats.write_text(json.dumps(stats, indent=2))
        print(f"Stats saved: {args.output_stats}")

    if args.output_png:
        plot_comparison(result, args.plugin_csv, args.reference_csv, args.output_png)

    return 0


if __name__ == "__main__":
    sys.exit(main())
