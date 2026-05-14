#Requires -Version 5.0
<#
.SYNOPSIS
Lock in optimal calibration parameters and regenerate official reference curves.

.DESCRIPTION
Updates source code defaults with optimal parameters, rebuilds, and regenerates
all 5 reference CSV/PNG files to be committed to the repository.

.EXAMPLE
.\lock_and_regenerate.ps1 -SidechainGain 0.6 -CvSoftKnee 0.75 -CvMax 8.0
#>

param(
    [double]$SidechainGain = 0.7,
    [double]$CvSoftKnee = 0.75,
    [double]$CvMax = 9.0,
    [string]$BuildDir = "build\vs2026-x64\tools\Release\",
    [string]$ProtocolSummaryPath = "calibration\outputs\calibration_sweep\protocol_summary.json",
    [switch]$SkipProtocolGate = $false
)

$ErrorActionPreference = "Stop"

# Script lives under calibration/scripts after refactor.
$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $projectRoot $BuildDir
}
if (-not [System.IO.Path]::IsPathRooted($ProtocolSummaryPath)) {
    $ProtocolSummaryPath = Join-Path $projectRoot $ProtocolSummaryPath
}

Write-Host "Locking calibration parameters into codebase..." -ForegroundColor Cyan
Write-Host "  sidechainAmplifierGain = $SidechainGain"
Write-Host "  sidechainCvSoftKneeV   = $CvSoftKnee"
Write-Host "  cvMaxV                 = $CvMax"
Write-Host ""

if (-not $SkipProtocolGate) {
    if (-not (Test-Path $ProtocolSummaryPath)) {
        Write-Error "Protocol summary not found: $ProtocolSummaryPath. Run calibration\scripts\sweep_global_calibration.ps1 first or pass -SkipProtocolGate."
        exit 1
    }

    Write-Host "Validating protocol gate from $ProtocolSummaryPath..." -ForegroundColor DarkGray
    $protocol = Get-Content $ProtocolSummaryPath -Raw | ConvertFrom-Json
    if ($null -eq $protocol.passingCandidates -or $protocol.passingCandidates.Count -eq 0) {
        Write-Error "Protocol gate failed: no passing candidates available in summary."
        exit 1
    }

    $match = $protocol.passingCandidates | Where-Object {
        ([math]::Abs([double]$_.gain - $SidechainGain) -lt 0.0001) -and
        ([math]::Abs([double]$_.knee - $CvSoftKnee) -lt 0.0001) -and
        ([math]::Abs([double]$_.cvMax - $CvMax) -lt 0.0001)
    } | Select-Object -First 1

    if ($null -eq $match) {
        Write-Error "Protocol gate failed: requested parameters are not in passingCandidates."
        Write-Host "Tip: pick a passing tuple from $ProtocolSummaryPath or pass -SkipProtocolGate for manual override." -ForegroundColor Yellow
        exit 1
    }

    Write-Host "  [OK] Protocol gate passed" -ForegroundColor Green
}

# Find the core header file
$coreHeaderPath = Join-Path $projectRoot "src\DSP\Models\Fairchild\Fairchild670Core.h"
if (-not (Test-Path $coreHeaderPath)) {
    Write-Error "Core header not found: $coreHeaderPath"
    exit 1
}

# Update sidechainAmplifierGain in processor header
Write-Host "Updating $coreHeaderPath..." -ForegroundColor DarkGray

$content = Get-Content $coreHeaderPath -Raw

# Update sidechainAmplifierGain default
$pattern = 'float sidechainAmplifierGain = [\d\.]+f;'
$replacement = "float sidechainAmplifierGain = ${SidechainGain}f;"
$content = [regex]::Replace($content, $pattern, $replacement)

# Update sidechainCvSoftKneeV default
$pattern = 'float sidechainCvSoftKneeV = [\d\.]+f;'
$replacement = "float sidechainCvSoftKneeV = ${CvSoftKnee}f;"
$content = [regex]::Replace($content, $pattern, $replacement)

Set-Content -Path $coreHeaderPath -Value $content -NoNewline
    Write-Host "  [OK] Updated sidechainAmplifierGain and sidechainCvSoftKneeV" -ForegroundColor Green

# Check if cvMaxV needs updating (usually in VariableMuPushPullStage or stage config)
$stageHeaderPath = Join-Path $projectRoot "src\DSP\Models\Fairchild\VariableMuPushPullStage.h"
if (Test-Path $stageHeaderPath) {
    Write-Host "Checking $stageHeaderPath..." -ForegroundColor DarkGray
    $stageContent = Get-Content $stageHeaderPath -Raw
    
    # Only update if pattern found
    if ($stageContent -match 'float cvMaxV = [\d\.]+f;') {
        $stageContent = [regex]::Replace($stageContent, 'float cvMaxV = [\d\.]+f;', "float cvMaxV = ${CvMax}f;")
        Set-Content -Path $stageHeaderPath -Value $stageContent -NoNewline
        Write-Host "  [OK] Updated cvMaxV" -ForegroundColor Green
    } else {
        Write-Host "  - cvMaxV not found in header (may be in stageCfg)" -ForegroundColor DarkGray
    }
}

# Rebuild project
Write-Host ""
Write-Host "Rebuilding project..." -ForegroundColor Cyan

# Find cmake executable using the helper script
$findCmakePath = Join-Path $projectRoot "scripts\find-cmake.ps1"
if (Test-Path $findCmakePath) {
    $output = & $findCmakePath
    # Last line should be the cmake path
    if ($output -is [array]) {
        $cmakePath = $output[-1]
    } else {
        $cmakePath = $output
    }
    if (-not $cmakePath) {
        Write-Error "CMake path lookup failed"
        exit 1
    }
} else {
    Write-Error "find-cmake.ps1 not found"
    exit 1
}

$configPreset = "vs2026-x64-release"
$buildPreset = "release"

Write-Host "  CMake: $cmakePath" -ForegroundColor DarkGray
Write-Host "  Configuring..." -ForegroundColor DarkGray
& "$cmakePath" --preset $configPreset 2>&1 | Select-Object -Last 5

Write-Host "  Building..." -ForegroundColor DarkGray
& "$cmakePath" --build --preset $buildPreset 2>&1 | Select-Object -Last 10

# Verify build succeeded
$exePath = Resolve-Path $BuildDir | Join-Path -ChildPath "phu_calibrate.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "Build failed: phu_calibrate.exe not found at $exePath"
    exit 1
}

    Write-Host "  [OK] Build successful" -ForegroundColor Green

# Regenerate reference curves
Write-Host ""
Write-Host "Regenerating official reference curves..." -ForegroundColor Cyan
& (Join-Path $projectRoot "calibration\scripts\generate_transfer_references.ps1") -SidechainGain $SidechainGain -CvSoftKnee $CvSoftKnee -CvMax $CvMax

Write-Host ""
Write-Host "SUCCESS!" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Review generated CSV files in calibration/reference/"
Write-Host "  2. Verify PNG plots in calibration/plots/"
Write-Host "  3. Run unit tests: .\build\vs2026-x64\Release\phu_transfer_curve_tests.exe"
Write-Host "  4. Commit changes:"
Write-Host "     git add src/DSP/Models/Fairchild/Fairchild670Core.h"
Write-Host "     git add calibration/reference/transfer_curve_ref_*.csv"
    Write-Host "     git commit -m \"Lock calibration: gain=$SidechainGain, knee=$CvSoftKnee, cvMax=$CvMax\""
Write-Host ""
