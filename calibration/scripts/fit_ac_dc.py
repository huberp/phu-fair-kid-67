"""fit_ac_dc.py  — Phase 5 Calibration Parameter Optimizer

For each Fairchild reference curve, finds the AC threshold and DC bias values
that minimise the RMS error between the plugin's transfer curve and the
hand-digitized ground-truth curve.

Uses scipy.optimize.minimize (Nelder-Mead), invoking phu_calibrate as a
subprocess for each candidate (AC, DC) pair.

Usage:
    python calibration/scripts/fit_ac_dc.py \\
        --calibrate-bin  build/vs2026-x64/tools/Release/phu_calibrate.exe \\
        --output-json    calibration/outputs/fitted_parameters.json

Options:
    --curves        Comma-separated list of curve indices to fit (default: 1,2,3,4,5)
    --k-offset      dBm = dBFS + K  (default 19.2)
    --sweep-min     Transfer sweep min dBFS (default -35)
    --sweep-max     Transfer sweep max dBFS (default -5)
    --sweep-step    Transfer sweep step dB  (default 0.5)
    --work-dir      Temp directory for intermediate CSVs (default: system temp)
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.optimize import minimize


# ---------------------------------------------------------------------------
# Shared parsing helpers (duplicated from compare_to_reference.py for
# self-containment; import if you refactor to a shared lib later)
# ---------------------------------------------------------------------------

def _load_plugin_csv(path: Path) -> tuple[np.ndarray, np.ndarray]:
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
        raise ValueError(f"No valid rows in plugin CSV: {path}")
    arr = np.array(sorted(rows, key=lambda r: r[0]))
    return arr[:, 0], arr[:, 1]


def _load_reference_csv(path: Path) -> tuple[np.ndarray, np.ndarray]:
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
                continue
    if not rows:
        raise ValueError(f"No valid rows in reference CSV: {path}")
    arr = np.array(sorted(rows, key=lambda r: r[0]))
    return arr[:, 0], arr[:, 1]


# ---------------------------------------------------------------------------
# Objective function
# ---------------------------------------------------------------------------

def _run_calibrate(
    calibrate_bin: Path,
    ac: float,
    dc: float,
    sweep_min: float,
    sweep_max: float,
    sweep_step: float,
    work_dir: Path,
    tag: str,
) -> Path:
    """Run phu_calibrate with the given AC/DC and return the output CSV path."""
    out_csv = work_dir / f"fit_{tag}.csv"
    cmd = [
        str(calibrate_bin),
        "--measure-transfer",
        "--threshold-ac",        str(ac),
        "--threshold-dc",        str(dc),
        "--transfer-min-dbfs",   str(sweep_min),
        "--transfer-max-dbfs",   str(sweep_max),
        "--transfer-step-db",    str(sweep_step),
        "--output",              str(out_csv),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"phu_calibrate failed (exit {result.returncode}):\n{result.stderr}"
        )
    return out_csv


def make_objective(
    calibrate_bin: Path,
    ref_in_dbm: np.ndarray,
    ref_out_dbm: np.ndarray,
    k_offset: float,
    sweep_min: float,
    sweep_max: float,
    sweep_step: float,
    work_dir: Path,
    curve_idx: int,
) -> callable:
    """Return an objective function f([ac, dc]) → rms_error_db."""
    call_count = [0]

    def objective(params: np.ndarray) -> float:
        ac = float(np.clip(params[0], 0.01, 15.0))
        dc = float(np.clip(params[1], 0.0,  10.0))
        call_count[0] += 1
        tag = f"c{curve_idx}_{call_count[0]:04d}"
        try:
            csv_path = _run_calibrate(
                calibrate_bin, ac, dc,
                sweep_min, sweep_max, sweep_step,
                work_dir, tag,
            )
            plug_in, plug_out = _load_plugin_csv(csv_path)
        except Exception as exc:
            print(f"  [objective] error: {exc}", file=sys.stderr)
            return 1e6

        plug_in_dbm  = plug_in  + k_offset
        plug_out_dbm = plug_out + k_offset

        mask = (plug_in_dbm >= ref_in_dbm[0]) & (plug_in_dbm <= ref_in_dbm[-1])
        if mask.sum() == 0:
            return 1e6

        ref_interp = np.interp(plug_in_dbm[mask], ref_in_dbm, ref_out_dbm)
        rms = float(np.sqrt(np.mean((plug_out_dbm[mask] - ref_interp) ** 2)))
        print(f"  [{call_count[0]:4d}] AC={ac:.3f}  DC={dc:.3f}  rms={rms:.4f} dB")
        return rms

    return objective


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

INITIAL_PARAMS = {
    1: (10.0, 0.0),
    2: (8.5,  4.5),
    3: (5.0,  1.5),
    4: (0.5,  4.0),
    5: (0.5,  0.5),
}

REF_DIR = Path("calibration/reference")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--calibrate-bin", required=True, type=Path,
                    help="Path to phu_calibrate executable")
    ap.add_argument("--output-json", required=True, type=Path,
                    help="Write fitted parameters as JSON")
    ap.add_argument("--curves",      default="1,2,3,4,5",
                    help="Comma-separated curve indices to fit (default: 1,2,3,4,5)")
    ap.add_argument("--k-offset",    type=float, default=19.2)
    ap.add_argument("--sweep-min",   type=float, default=-35.0)
    ap.add_argument("--sweep-max",   type=float, default=-5.0)
    ap.add_argument("--sweep-step",  type=float, default=0.5)
    ap.add_argument("--work-dir",    type=Path,  default=None,
                    help="Temp directory for intermediate CSVs")
    args = ap.parse_args()

    if not args.calibrate_bin.exists():
        print(f"ERROR: calibrate binary not found: {args.calibrate_bin}", file=sys.stderr)
        return 1

    curve_indices = [int(x.strip()) for x in args.curves.split(",")]
    fitted = {}

    with tempfile.TemporaryDirectory(dir=args.work_dir) as tmp:
        work_dir = Path(tmp)
        for idx in curve_indices:
            ref_csv = REF_DIR / f"Transfere-Curve-{idx}.csv"
            if not ref_csv.exists():
                print(f"WARNING: reference not found: {ref_csv} — skipping curve {idx}",
                      file=sys.stderr)
                continue

            print(f"\n{'='*60}")
            print(f"  Fitting Curve {idx}  (reference: {ref_csv.name})")
            print(f"{'='*60}")

            ref_in, ref_out = _load_reference_csv(ref_csv)
            x0 = np.array(INITIAL_PARAMS.get(idx, (5.0, 1.0)), dtype=float)

            obj = make_objective(
                args.calibrate_bin, ref_in, ref_out,
                args.k_offset,
                args.sweep_min, args.sweep_max, args.sweep_step,
                work_dir, idx,
            )

            res = minimize(obj, x0, method="Nelder-Mead",
                           options={"xatol": 0.01, "fatol": 0.005,
                                    "maxiter": 200, "disp": True})

            ac_fit = float(np.clip(res.x[0], 0.01, 15.0))
            dc_fit = float(np.clip(res.x[1], 0.0,  10.0))
            rms_fit = float(res.fun)

            print(f"\n  → Fitted: AC={ac_fit:.4f} V  DC={dc_fit:.4f} V  rms={rms_fit:.4f} dB")
            fitted[str(idx)] = {
                "ac_threshold_v": ac_fit,
                "dc_bias_v":      dc_fit,
                "rms_error_db":   rms_fit,
                "optimizer_success": bool(res.success),
                "optimizer_message": str(res.message),
            }

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(fitted, indent=2))
    print(f"\nFitted parameters saved: {args.output_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
