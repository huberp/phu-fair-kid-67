"""scan_sidechain_gain.py  — Quick gain scan for pre-detection calibration.

Sweeps --sidechain-gain over a range and reports combined RMS error across
all compression curves (2-5) with the hardware AC/DC settings.

Usage:
    python calibration/scripts/scan_sidechain_gain.py \
        --calibrate-bin build/vs2026-x64/tools/Release/phu_calibrate.exe
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
# Helpers (duplicated from compare_to_reference.py for self-containment)
# ---------------------------------------------------------------------------

def _load_plugin_csv(path: Path):
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


def _load_reference_csv(path: Path):
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
# Main
# ---------------------------------------------------------------------------

CURVES = [
    dict(idx=2, ac=8.5, dc=4.5, max_dbfs=9,
         ref="calibration/reference/Transfere-Curve-2.csv"),
    dict(idx=3, ac=5.0, dc=1.5, max_dbfs=9,
         ref="calibration/reference/Transfere-Curve-3.csv"),
    dict(idx=4, ac=0.5, dc=4.0, max_dbfs=9,
         ref="calibration/reference/Transfere-Curve-4.csv"),
    dict(idx=5, ac=0.5, dc=0.5, max_dbfs=9,
         ref="calibration/reference/Transfere-Curve-5.csv"),
]


def run_single(calibrate_bin: Path, gain: float, curve: dict,
               work_dir: Path, k_offset: float) -> tuple[float, int]:
    """Return (sse, n_points) for one gain/curve combination."""
    csv_out = work_dir / f"scan_c{curve['idx']}_g{gain:.3f}.csv"
    cmd = [
        str(calibrate_bin),
        "--measure-transfer",
        "--threshold-ac", str(curve["ac"]),
        "--threshold-dc", str(curve["dc"]),
        "--sidechain-gain", str(gain),
        "--transfer-min-dbfs", "-26",
        "--transfer-max-dbfs", str(curve["max_dbfs"]),
        "--transfer-step-db", "0.5",
        "--output", str(csv_out),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"  [WARN] phu_calibrate failed for gain={gain}, curve={curve['idx']}: {r.stderr[:120]}", file=sys.stderr)
        return 0.0, 0

    ref_path = Path(curve["ref"])
    pin, pout = _load_plugin_csv(csv_out)
    rin, rout = _load_reference_csv(ref_path)

    pin_dbm = pin + k_offset
    pout_dbm = pout + k_offset
    ok = (pin_dbm >= rin[0]) & (pin_dbm <= rin[-1])
    n = ok.sum()
    if n == 0:
        return 0.0, 0

    rout_interp = np.interp(pin_dbm[ok], rin, rout)
    err = pout_dbm[ok] - rout_interp
    return float(np.sum(err ** 2)), int(n)


def main():
    parser = argparse.ArgumentParser(description="Scan sidechain-gain values.")
    parser.add_argument("--calibrate-bin", required=True)
    parser.add_argument("--gain-min", type=float, default=0.2)
    parser.add_argument("--gain-max", type=float, default=2.0)
    parser.add_argument("--gain-steps", type=int, default=19)
    parser.add_argument("--k-offset", type=float, default=19.2)
    args = parser.parse_args()

    calibrate_bin = Path(args.calibrate_bin)
    gains = np.linspace(args.gain_min, args.gain_max, args.gain_steps)

    print(f"{'gain':>8}  {'rms_dB':>8}  {'per-curve RMS (C2 C3 C4 C5)'}")
    print("-" * 65)

    with tempfile.TemporaryDirectory() as tmpdir:
        work_dir = Path(tmpdir)
        for gain in gains:
            total_sse, total_n = 0.0, 0
            curve_rms = []
            for c in CURVES:
                sse, n = run_single(calibrate_bin, gain, c, work_dir, args.k_offset)
                total_sse += sse
                total_n += n
                curve_rms.append(f"{np.sqrt(sse / n):.2f}" if n > 0 else " ??? ")
            combined = np.sqrt(total_sse / total_n) if total_n > 0 else float("nan")
            per = "  ".join(curve_rms)
            print(f"{gain:8.3f}  {combined:8.3f}  {per}")


if __name__ == "__main__":
    main()
