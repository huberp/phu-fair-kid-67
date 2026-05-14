#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Generate all AC/DC threshold transfer-curve reference CSVs and PNG plots.

.DESCRIPTION
    Runs phu_calibrate for the 5 AC/DC settings that correspond to the five
    "Input vs. Output Curves" shown in the Fairchild 670 manual (pages 8/11).

    Note: these AC/DC tuple values are initial software calibration targets and
    may be refined by sweep_global_calibration.

    All measurements use timing position 1 (shortest settle time).
    Reference CSVs are written to calibration/reference/.
    PNG plots are written to calibration/plots/ (created if absent).

.PARAMETER BuildDir
    Path to the CMake build output directory containing phu_calibrate.exe.
    Defaults to build\vs2026-x64\tools\Release.

.EXAMPLE
    .\calibration\scripts\generate_transfer_references.ps1
    .\calibration\scripts\generate_transfer_references.ps1 -BuildDir build\vs2026-x64\tools\RelWithDebInfo
#>

param(
    [string]$BuildDir = "build\vs2026-x64\tools\Release\",
    [string]$SidechainGain = "0.7",
    [string]$CvSoftKnee = "0.75",
    [string]$CvMax = "9.0"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Resolve paths ─────────────────────────────────────────────────────────────
# Script lives under calibration/scripts after refactor.
$root         = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe          = Join-Path (Join-Path $root $BuildDir) "phu_calibrate.exe"
$referenceDir = Join-Path $root "calibration\reference"
$plotDir      = Join-Path $root "calibration\plots"
$plotPy       = Join-Path (Join-Path $root "scripts") "plot_transfer.py"

if (-not (Test-Path $exe)) {
    Write-Error "phu_calibrate.exe not found at: $exe`nBuild the project first or pass -BuildDir."
}

if (-not (Test-Path $referenceDir)) {
    New-Item -ItemType Directory -Path $referenceDir -Force | Out-Null
}
if (-not (Test-Path $plotDir)) {
    New-Item -ItemType Directory -Path $plotDir -Force | Out-Null
}

# ── Curve definitions ─────────────────────────────────────────────────────────
# Each entry: Label, AC threshold (V), DC bias (V), description for progress output.
$curves = @(
    [pscustomobject]@{ Label="thresh10v0"; AcThreshV="10.0"; DcBiasV="0.0"; Desc="curve 1 - straight amplifier (no compression)" },
    [pscustomobject]@{ Label="thresh3v5";  AcThreshV="8.0";  DcBiasV="1.5"; Desc="curve 2 - light compression (AC slight CW, DC max)" },
    [pscustomobject]@{ Label="thresh2v8";  AcThreshV="3.0";  DcBiasV="0.2"; Desc="curve 5 - heavy AC, minimal DC" },
    [pscustomobject]@{ Label="thresh2v0";  AcThreshV="5.0";  DcBiasV="1.0"; Desc="curve 3 - factory-adjusted (nominal)" },
    [pscustomobject]@{ Label="thresh0v0";  AcThreshV="0.5";  DcBiasV="1.8"; Desc="curve 4 - maximum compression" }
)

# ── Run measurements and plots ────────────────────────────────────────────────
foreach ($c in $curves) {
    $csvOut = Join-Path $referenceDir "transfer_curve_ref_$($c.Label).csv"
    $pngOut = Join-Path $plotDir      "transfer_curve_ref_$($c.Label).png"

    Write-Host "`n[$($c.Label)] $($c.Desc)" -ForegroundColor Cyan
    Write-Host "  Measuring  -> $csvOut"

    & $exe --measure-transfer --position 1 --threshold-ac $c.AcThreshV --threshold-dc $c.DcBiasV --output $csvOut `
        --sidechain-gain $SidechainGain --cv-soft-knee $CvSoftKnee --cv-max $CvMax
    if ($LASTEXITCODE -ne 0) {
        Write-Error "phu_calibrate failed for ac=$($c.AcThreshV) dc=$($c.DcBiasV)"
    }

    Write-Host "  Plotting   -> $pngOut"
    python $plotPy $csvOut --output $pngOut
    if ($LASTEXITCODE -ne 0) {
        Write-Error "plot_transfer.py failed for $csvOut"
    }
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host "`nDone." -ForegroundColor Green
Write-Host "Reference CSVs written to: $referenceDir"
Write-Host "PNG plots written to:       $plotDir"
Write-Host "Calibration knobs: sidechain_gain=$SidechainGain cv_soft_knee=$CvSoftKnee cv_max=$CvMax"
