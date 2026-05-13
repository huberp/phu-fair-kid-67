#Requires -Version 5.0
<#
.SYNOPSIS
Automated global sweep across all 5 threshold curves to find optimal calibration parameters.

.DESCRIPTION
Tests combinations of sidechain_gain, cv_soft_knee, and cv_max across all 5 reference curves.
Calculates right-edge dip metric (sum of absolute deltas in final 4 output rows).
Ranks results and identifies global-optimal configuration.

.EXAMPLE
.\sweep_global_calibration.ps1
#>

param(
    [string]$BuildDir = "build\vs2026-x64\tools\Release\",
    [string]$OutputDir = "tmp\calibration_sweep",
    [switch]$Quick = $false  # Quick mode: fewer parameter combinations
)

$ErrorActionPreference = "Stop"

# Define test matrix
if ($Quick) {
    $gainValues = @(0.5, 0.6, 0.7)
    $kneeValues = @(0.5, 0.75, 1.0)
    $cvMaxValues = @(7.0, 8.0, 9.0)
} else {
    $gainValues = @(0.4, 0.5, 0.6, 0.7, 0.8)
    $kneeValues = @(0.5, 0.75, 1.0, 1.5)
    $cvMaxValues = @(6.0, 7.0, 8.0, 9.0)
}

# Threshold curves: (name, threshold_v)
$curves = @(
    @{ name = "thresh10v0"; threshold = 10.0; weight = 1.0 }
    @{ name = "thresh3v5";  threshold = 3.5;  weight = 1.0 }
    @{ name = "thresh2v8";  threshold = 2.8;  weight = 1.0 }
    @{ name = "thresh2v0";  threshold = 2.0;  weight = 1.5 }  # Primary target
    @{ name = "thresh0v0";  threshold = 0.0;  weight = 2.0 }  # Most critical
)

# Verify calibration tool exists
$exe = Resolve-Path $BuildDir | Join-Path -ChildPath "phu_calibrate.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Calibration tool not found: $exe"
    exit 1
}

# Create output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Storage for results
$results = @()
$runCount = 0
$totalRuns = $gainValues.Count * $kneeValues.Count * $cvMaxValues.Count * $curves.Count

Write-Host "Starting global calibration sweep..."
Write-Host "Configuration:" -ForegroundColor Cyan
Write-Host "  Gain values: [$($gainValues -join ', ')]"
Write-Host "  Knee values: [$($kneeValues -join ', ')]"
Write-Host "  CV Max values: [$($cvMaxValues -join ', ')]"
Write-Host "  Curves: $(($curves | ForEach-Object { $_.name }) -join ', ')"
Write-Host "  Total runs: $totalRuns (approx $(($totalRuns * 5) / 60) minutes)"
Write-Host ""

# Main sweep loop
foreach ($gain in $gainValues) {
    foreach ($knee in $kneeValues) {
        foreach ($cvMax in $cvMaxValues) {
            
            $paramSetName = "gain_$([math]::Round($gain, 2))_knee_$([math]::Round($knee, 2))_cvMax_$([math]::Round($cvMax, 1))"
            $paramSetDir = Join-Path $OutputDir $paramSetName
            if (-not (Test-Path $paramSetDir)) {
                New-Item -ItemType Directory -Path $paramSetDir -Force | Out-Null
            }
            
            foreach ($curve in $curves) {
                $runCount++
                $pct = [math]::Round(100 * $runCount / $totalRuns, 1)
                Write-Host "[$pct%] Testing: $($curve.name) with gain=$gain knee=$knee cvMax=$cvMax" -ForegroundColor DarkGray
                
                $csvOut = Join-Path $paramSetDir "$($curve.name).csv"
                
                # Run calibration
                & $exe --measure-transfer --position 1 --threshold $curve.threshold `
                    --sidechain-gain $gain `
                    --cv-soft-knee $knee `
                    --cv-max $cvMax `
                    --output $csvOut 2>&1 | Out-Null
                
                if (-not (Test-Path $csvOut)) {
                    Write-Warning "Failed to generate: $csvOut"
                    continue
                }
                
                # Parse CSV and calculate right-edge dip metric
                $rows = @(Get-Content $csvOut | Where-Object { $_ -and -not $_.StartsWith('#') }) | ConvertFrom-Csv
                
                if ($rows.Count -lt 5) {
                    Write-Warning "CSV has insufficient rows: $($rows.Count)"
                    continue
                }
                
                # Extract last 4 output values
                $lastRows = $rows | Select-Object -Last 4
                $outputs = @($lastRows | ForEach-Object { [double]$_.output_dbfs })
                
                # Calculate deltas (slope in final region)
                $deltas = @()
                for ($i = 1; $i -lt $outputs.Count; $i++) {
                    $deltas += [math]::Abs($outputs[$i] - $outputs[$i-1])
                }
                
                # Score: sum of absolute deltas (lower = flatter)
                $score = ($deltas | Measure-Object -Sum).Sum
                
                # Store result
                $results += @{
                    gain         = $gain
                    knee         = $knee
                    cvMax        = $cvMax
                    curveName    = $curve.name
                    curveWeight  = $curve.weight
                    score        = [math]::Round($score, 4)
                    lastOutputs  = $outputs
                    deltas       = $deltas
                    csvPath      = $csvOut
                }
            }
        }
    }
}

Write-Host ""
Write-Host "Sweep complete. Analyzing results..." -ForegroundColor Cyan

# Calculate weighted scores per parameter set
$paramSetScores = @{}

foreach ($result in $results) {
    $key = "$($result.gain)_$($result.knee)_$($result.cvMax)"
    if (-not $paramSetScores.ContainsKey($key)) {
        $paramSetScores[$key] = @{
            gain            = $result.gain
            knee            = $result.knee
            cvMax           = $result.cvMax
            totalScore      = 0
            weightedScore   = 0
            perCurveScores  = @{}
        }
    }
    
    $paramSetScores[$key].perCurveScores[$result.curveName] = $result.score
    $paramSetScores[$key].totalScore += $result.score
    $paramSetScores[$key].weightedScore += $result.score * $result.curveWeight
}

# Sort by weighted score (lower is better)
$ranked = $paramSetScores.Values | Sort-Object -Property weightedScore

Write-Host ""
Write-Host "Top 10 Parameter Sets (by weighted score)" -ForegroundColor Green
Write-Host ""
Write-Host @"
Rank | Gain  | Knee  | CvMax | Weighted | Total  | thresh10v0 | thresh3v5 | thresh2v8 | thresh2v0 | thresh0v0
     |       |       |       | Score    | Score  | (w=1.0)    | (w=1.0)   | (w=1.0)   | (w=1.5)   | (w=2.0)
"@
Write-Host ('-' * 130)

for ($i = 0; $i -lt [math]::Min(10, $ranked.Count); $i++) {
    $r = $ranked[$i]
    $score0v0 = if ($r.perCurveScores.ContainsKey('thresh0v0')) { [math]::Round($r.perCurveScores['thresh0v0'], 3) } else { 'N/A' }
    $score2v0 = if ($r.perCurveScores.ContainsKey('thresh2v0')) { [math]::Round($r.perCurveScores['thresh2v0'], 3) } else { 'N/A' }
    $score2v8 = if ($r.perCurveScores.ContainsKey('thresh2v8')) { [math]::Round($r.perCurveScores['thresh2v8'], 3) } else { 'N/A' }
    $score3v5 = if ($r.perCurveScores.ContainsKey('thresh3v5')) { [math]::Round($r.perCurveScores['thresh3v5'], 3) } else { 'N/A' }
    $score10v0 = if ($r.perCurveScores.ContainsKey('thresh10v0')) { [math]::Round($r.perCurveScores['thresh10v0'], 3) } else { 'N/A' }
    
    Write-Host ("{0,4} | {1,5:F1} | {2,5:F2} | {3,5:F1} | {4,8:F4} | {5,6:F3} | {6,10} | {7,9} | {8,9} | {9,9} | {10,9}" -f `
        ($i+1), $r.gain, $r.knee, $r.cvMax, $r.weightedScore, $r.totalScore, `
        $score10v0, $score3v5, $score2v8, $score2v0, $score0v0)
}

Write-Host ""

# Recommend best configuration
$best = $ranked[0]
Write-Host "RECOMMENDATION" -ForegroundColor Yellow
Write-Host ("-" * 80)
Write-Host "Best global configuration (weighted score = $([math]::Round($best.weightedScore, 4))):"
Write-Host ""
Write-Host "  sidechainAmplifierGain = $($best.gain)"
Write-Host "  sidechainCvSoftKneeV   = $($best.knee)"
Write-Host "  cvMaxV                 = $($best.cvMax)"
Write-Host ""
Write-Host "Per-curve scores:"
foreach ($curve in $curves) {
    $score = if ($best.perCurveScores.ContainsKey($curve.name)) { [math]::Round($best.perCurveScores[$curve.name], 4) } else { 'N/A' }
    Write-Host "  $($curve.name): $score (weight=$($curve.weight))"
}
Write-Host ""

# Save results to JSON for further analysis
$summaryPath = Join-Path $OutputDir "sweep_results.json"
$ranked | ConvertTo-Json | Set-Content $summaryPath
Write-Host "Full results saved to: $summaryPath" -ForegroundColor DarkGray

# Ask user to confirm and lock in
Write-Host ""
Write-Host "Action Required:" -ForegroundColor Cyan
Write-Host "1. Review the top recommendations above"
Write-Host "2. If satisfied, run: .\scripts\lock_and_regenerate.ps1 -SidechainGain $($best.gain) -CvSoftKnee $($best.knee) -CvMax $($best.cvMax)"
Write-Host "   OR with custom values: .\scripts\lock_and_regenerate.ps1 -SidechainGain <val> -CvSoftKnee <val> -CvMax <val>"
Write-Host ""

exit 0
