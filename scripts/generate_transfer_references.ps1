#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Generate all threshold-variant transfer-curve reference CSVs and PNG plots.

.DESCRIPTION
    Runs phu_calibrate for the 5 threshold settings that correspond to the five
    "Input vs. Output Curves" shown in the Fairchild 670 manual (pages 8/11):

      Curve 1 — straight amplifier, threshold fully CCW  (threshold = 10.0 V, no compression)
      Curve 2 — AC/DC threshold slightly CW              (threshold =  3.5 V, light compression)
      Curve 3 — factory-adjusted condition               (threshold =  2.0 V, nominal)
      Curve 4 — AC/DC threshold fully CW                 (threshold =  0.0 V, maximum compression)
      Curve 5 — AC threshold slightly CW variant         (threshold =  2.8 V)

    All measurements use timing position 1 (shortest settle time).
    Reference CSVs are written to tests/.
    PNG plots are written to tmp/ (created if absent).

.PARAMETER BuildDir
    Path to the CMake build output directory containing phu_calibrate.exe.
    Defaults to build\vs2026-x64\tools\Release.

.EXAMPLE
    .\scripts\generate_transfer_references.ps1
    .\scripts\generate_transfer_references.ps1 -BuildDir build\vs2026-x64\tools\RelWithDebInfo
#>

param(
    [string]$BuildDir = "build\vs2026-x64\tools\Release\"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Resolve paths ─────────────────────────────────────────────────────────────
$root    = Split-Path $PSScriptRoot -Parent
$exe     = Join-Path (Join-Path $root $BuildDir) "phu_calibrate.exe"
$testDir = Join-Path $root "tests"
$tmpDir  = Join-Path $root "tmp"
$plotPy  = Join-Path (Join-Path $root "scripts") "plot_transfer.py"

if (-not (Test-Path $exe)) {
    Write-Error "phu_calibrate.exe not found at: $exe`nBuild the project first or pass -BuildDir."
}

if (-not (Test-Path $tmpDir)) {
    New-Item -ItemType Directory -Path $tmpDir | Out-Null
}

# ── Curve definitions ─────────────────────────────────────────────────────────
# Each entry: Label, threshold voltage, description for progress output
$curves = @(
    [pscustomobject]@{ Label="thresh10v0"; ThreshV="10.0"; Desc="curve 1 - straight amplifier (no compression)" },
    [pscustomobject]@{ Label="thresh3v5";  ThreshV="3.5";  Desc="curve 2 - light compression" },
    [pscustomobject]@{ Label="thresh2v8";  ThreshV="2.8";  Desc="curve 5 - AC-threshold variant" },
    [pscustomobject]@{ Label="thresh2v0";  ThreshV="2.0";  Desc="curve 3 - factory-adjusted (nominal)" },
    [pscustomobject]@{ Label="thresh0v0";  ThreshV="0.0";  Desc="curve 4 - maximum compression" }
)

# ── Run measurements and plots ────────────────────────────────────────────────
foreach ($c in $curves) {
    $csvOut = Join-Path $testDir "transfer_curve_ref_$($c.Label).csv"
    $pngOut = Join-Path $tmpDir  "transfer_curve_ref_$($c.Label).png"

    Write-Host "`n[$($c.Label)] $($c.Desc)" -ForegroundColor Cyan
    Write-Host "  Measuring  -> $csvOut"

    & $exe --measure-transfer --position 1 --threshold $c.ThreshV --output $csvOut
    if ($LASTEXITCODE -ne 0) {
        Write-Error "phu_calibrate failed for threshold=$($c.ThreshV)"
    }

    Write-Host "  Plotting   -> $pngOut"
    python $plotPy $csvOut --output $pngOut
    if ($LASTEXITCODE -ne 0) {
        Write-Error "plot_transfer.py failed for $csvOut"
    }
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host "`nDone." -ForegroundColor Green
Write-Host "Reference CSVs written to: $testDir"
Write-Host "PNG plots written to:       $tmpDir"
