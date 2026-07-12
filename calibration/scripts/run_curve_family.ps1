<#
.SYNOPSIS
    Run phu_calibrate for all 5 Fairchild reference curves and compare each to
    its hand-digitized ground-truth reference.

.DESCRIPTION
    For each curve:
      1. Invoke phu_calibrate with the curve's AC and DC threshold estimates.
      2. Call compare_to_reference.py to overlay against the reference CSV.
      3. Write the sweep CSV, comparison PNG, and stats JSON under
         calibration/outputs/curve_family/.

.PARAMETER CalibrateBin
    Path to the phu_calibrate executable.
    Defaults to: build\vs2026-x64\tools\Release\phu_calibrate.exe

.PARAMETER OutputDir
    Directory to write all outputs.
    Defaults to: calibration/outputs/curve_family

.PARAMETER KOffset
    dBm = dBFS + K.  Default: 19.2

.EXAMPLE
    # From the repo root:
    pwsh -File calibration/scripts/run_curve_family.ps1

.EXAMPLE
    # Override the calibrate binary (e.g. Linux):
    pwsh -File calibration/scripts/run_curve_family.ps1 `
        -CalibrateBin build/linux/tools/phu_calibrate
#>

[CmdletBinding()]
param(
    [string] $CalibrateBin = "build\vs2026-x64\tools\Release\phu_calibrate.exe",
    [string] $OutputDir    = "calibration\outputs\curve_family",
    [double] $KOffset      = 19.2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Resolve repo root (one level above this script's directory) ──────────────
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
Push-Location $repoRoot

# ── Validate calibrate binary ─────────────────────────────────────────────────
if (-not (Test-Path $CalibrateBin)) {
    Write-Error "phu_calibrate not found at: $CalibrateBin`nBuild the project first."
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# ── Curve definitions ─────────────────────────────────────────────────────────
# Each entry: (curve index, AC threshold V, DC bias V, description, MaxDbfs override)
# MaxDbfs: per-curve peak ceiling for --transfer-max-dbfs (null => use $sweepMaxDbfs).
#
# Curve 1 override (MaxDbfs=0):
#   Curve 1 is the linear reference (no compression, AC=10V).  With K=19.2 dBm,
#   0 dBFS peak = +19.2 dBm, which is the plugin's designed maximum operating level.
#   Above 0 dBFS the plugin's variable-mu stage may saturate, producing large
#   divergence from the straight-line reference.  Cap the Curve-1 sweep at 0 dBFS
#   so the comparison stays in the valid linear range (-9.5 … +19.2 dBm).

# AC/DC values for C1–C3, C5 are scaled by sidechainAmplifierGain (0.7) relative to the
# original reference measurements (Phase 1 architecture fix: pre-detection gain move).
# C4 was re-fitted (Phase 2+3) because the 3-stage 12AX7×2→12BH7 sidechain tube chain
# saturates the AC path at high drive levels, requiring a different DC bias to match the
# "Max compression, DC near-max" reference curve.  AC=0.010 V / DC=4.519 V is the Nelder-Mead
# optimum for the tube-chain model (rms=2.963 dB vs 5.412 dB with the Phase 1 values).
# If sidechainAmplifierGain or the tube chain parameters change, re-run:
#   .venv\Scripts\python.exe calibration\scripts\fit_ac_dc.py --curves 4 --sweep-min -26 --sweep-max 9 --sweep-step 2.0 ...
$curves = @(
    [PSCustomObject]@{ Index=1; AC= 7.0;   DC=0.0;   Desc="Linear (no GR)";              MaxDbfs=  0 }
    [PSCustomObject]@{ Index=2; AC= 5.95;  DC=3.15;  Desc="Light compression, DC CW";    MaxDbfs=$null }
    [PSCustomObject]@{ Index=3; AC= 3.5;   DC=1.05;  Desc="Factory condition";            MaxDbfs=$null }
    [PSCustomObject]@{ Index=4; AC= 0.010; DC=4.519; Desc="Max compression, DC near-max"; MaxDbfs=$null }
    [PSCustomObject]@{ Index=5; AC= 0.35;  DC=0.35;  Desc="Heavy AC, minimal DC";         MaxDbfs=$null }
)

# ── Sweep parameters ──────────────────────────────────────────────────────────
# NOTE: --transfer-min/max-dbfs are PEAK levels; phu_calibrate stores RMS in
# the CSV (sine peak-to-RMS = 3.01 dB).  With K = 19.2 dBm:
#   measured input_dbm  = input_dbfs_rms + 19.2
#   input_dbfs_rms      = peak_dbfs - 3.01
# Reference curves span roughly -9.5 dBm to +25 dBm, which maps to:
#   peak_min = (-9.5  - 19.2 + 3.01) = -25.7 dBFS  -> use -26
#   peak_max = (+25.0 - 19.2 + 3.01) = +8.81 dBFS  -> use +9
$sweepMinDbfs = -26
$sweepMaxDbfs =   9
$sweepStepDb  =   0.5

# ── Run each curve ────────────────────────────────────────────────────────────
foreach ($c in $curves) {
    $tag      = "curve$($c.Index)"
    $csvOut   = Join-Path $OutputDir "$tag.csv"
    $pngOut   = Join-Path $OutputDir "${tag}_compare.png"
    $statsOut = Join-Path $OutputDir "${tag}_stats.json"
    $refCsv   = "calibration\reference\Transfere-Curve-$($c.Index).csv"

    Write-Host ""
    Write-Host "── Curve $($c.Index): $($c.Desc) ──────────────────────────" -ForegroundColor Cyan
    Write-Host "   AC = $($c.AC) V   DC = $($c.DC) V"

    # Run phu_calibrate
    # Resolve the effective sweep ceiling: use the per-curve override when set,
    # otherwise fall back to the global $sweepMaxDbfs.
    $effectiveMax = if ($null -ne $c.MaxDbfs) { $c.MaxDbfs } else { $sweepMaxDbfs }
    $calibArgs = @(
        "--measure-transfer"
        "--threshold-ac",   $c.AC
        "--threshold-dc",   $c.DC
        "--transfer-min-dbfs", $sweepMinDbfs
        "--transfer-max-dbfs", $effectiveMax
        "--transfer-step-db",  $sweepStepDb
        "--output", $csvOut
    )

    Write-Host "   Running: $CalibrateBin $calibArgs"
    & $CalibrateBin @calibArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "phu_calibrate exited with code $LASTEXITCODE for curve $($c.Index). Skipping compare."
        continue
    }
    Write-Host "   Sweep CSV: $csvOut" -ForegroundColor Green

    # Compare against reference
    $compareArgs = @(
        "calibration\scripts\compare_to_reference.py"
        "--plugin-csv",    $csvOut
        "--reference-csv", $refCsv
        "--k-offset",      $KOffset
        "--output-png",    $pngOut
        "--output-stats",  $statsOut
    )

    Write-Host "   Comparing against: $refCsv"
    python @compareArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "compare_to_reference.py failed for curve $($c.Index)."
    }
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "═══ Summary ════════════════════════════════════════════════" -ForegroundColor Yellow
foreach ($c in $curves) {
    $statsFile = Join-Path $OutputDir "curve$($c.Index)_stats.json"
    if (Test-Path $statsFile) {
        $stats = Get-Content $statsFile | ConvertFrom-Json
        $line = "  Curve {0}  rms={1:F3} dB  max={2:F3} dB  ({3})" -f $c.Index, $stats.rms_error_db, $stats.max_error_db, $c.Desc
        Write-Host $line
    } else {
        Write-Host "  Curve $($c.Index)  -- no stats (run failed)" -ForegroundColor Red
    }
}

Pop-Location
Write-Host ""
Write-Host "Outputs written to: $OutputDir" -ForegroundColor Green
