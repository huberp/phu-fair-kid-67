#Requires -Version 5.0
<#
.SYNOPSIS
Automated global sweep across all 5 threshold curves to find optimal calibration parameters.

.DESCRIPTION
Tests combinations of sidechain_gain, cv_soft_knee, and cv_max across all 5 reference curves.
Applies strict protocol checks (monotonicity, tail downturn, family ordering,
family separation) and ranks only passing candidates.

.EXAMPLE
.\sweep_global_calibration.ps1
#>

param(
    [string]$BuildDir = "build\vs2026-x64\tools\Release\",
    [string]$OutputDir = "tmp\calibration_sweep",
    [switch]$Quick = $false,  # Quick mode: fewer parameter combinations
    [double]$MonoSlackDb = 0.10,
    [double]$TailMinSlopeDb = -0.25,
    [double]$Checkpoint1Db = -9.0,
    [double]$Checkpoint2Db = -6.0,
    [double]$MinSepAtCheckpoint1Db = 0.50,
    [double]$MinSepAtCheckpoint2Db = 0.50,
    [double]$RegularizationWeight = 0.2,
    [int]$CurveParallelism = 1
)

$ErrorActionPreference = "Stop"

# Protocol thresholds (second-draft defaults)
$monoSlackDb = $MonoSlackDb
$tailMinSlopeDb = $TailMinSlopeDb
$minSepAtCheckpoint1Db = $MinSepAtCheckpoint1Db
$minSepAtCheckpoint2Db = $MinSepAtCheckpoint2Db
$regularizationWeight = $RegularizationWeight

$baselineGain = 0.7
$baselineKnee = 0.75
$baselineCvMax = 9.0

function Get-NearestRow {
    param(
        [Parameter(Mandatory = $true)]
        [array]$Rows,
        [Parameter(Mandatory = $true)]
        [double]$TargetInputDb
    )

    $best = $null
    $bestDist = [double]::PositiveInfinity
    foreach ($row in $Rows) {
        $input = [double]$row.input_dbfs
        $dist = [math]::Abs($input - $TargetInputDb)
        if ($dist -lt $bestDist) {
            $best = $row
            $bestDist = $dist
        }
    }
    return $best
}

# Define test matrix
if ($Quick) {
    # Broaden quick exploration so we can still discover separated curve families.
    $gainValues = @(0.6, 0.8, 1.0, 1.2)
    $kneeValues = @(0.5, 0.75, 1.25)
    $cvMaxValues = @(8.0, 9.0, 10.0)
} else {
    $gainValues = @(0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2)
    $kneeValues = @(0.5, 0.75, 1.0, 1.25, 1.5)
    $cvMaxValues = @(8.0, 9.0, 10.0)
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
$exePath = [string]$exe

if ($CurveParallelism -lt 1) {
    Write-Error "CurveParallelism must be >= 1"
    exit 1
}

# Create output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}
$outputRoot = (Resolve-Path $OutputDir).Path

# Storage for results
$results = @()
$runCount = 0
$totalRuns = $gainValues.Count * $kneeValues.Count * $cvMaxValues.Count * $curves.Count

Write-Host "Starting global calibration sweep..."
Write-Host "Configuration:" -ForegroundColor Cyan
Write-Host "  Gain values: [$($gainValues -join ', ')]"
Write-Host "  Knee values: [$($kneeValues -join ', ')]"
Write-Host "  CV Max values: [$($cvMaxValues -join ', ')]"
Write-Host "  Checkpoints: [$Checkpoint1Db dBFS, $Checkpoint2Db dBFS]"
Write-Host "  Min separation: [$minSepAtCheckpoint1Db dB, $minSepAtCheckpoint2Db dB]"
Write-Host "  Monotonicity slack: $monoSlackDb dB"
Write-Host "  Tail minimum slope: $tailMinSlopeDb dB"
Write-Host "  Curve parallelism: $CurveParallelism"
Write-Host "  Curves: $(($curves | ForEach-Object { $_.name }) -join ', ')"
Write-Host "  Total runs: $totalRuns (approx $(($totalRuns * 5) / 60) minutes)"
Write-Host ""

# Main sweep loop
foreach ($gain in $gainValues) {
    foreach ($knee in $kneeValues) {
        foreach ($cvMax in $cvMaxValues) {
            
            $paramSetName = "gain_$([math]::Round($gain, 2))_knee_$([math]::Round($knee, 2))_cvMax_$([math]::Round($cvMax, 1))"
            $paramSetDir = Join-Path $outputRoot $paramSetName
            if (-not (Test-Path $paramSetDir)) {
                New-Item -ItemType Directory -Path $paramSetDir -Force | Out-Null
            }

            $curveRuns = @()
            if ($CurveParallelism -gt 1) {
                $jobs = @()
                foreach ($curve in $curves) {
                    $runCount++
                    $pct = [math]::Round(100 * $runCount / $totalRuns, 1)
                    Write-Host "[$pct%] Queue: $($curve.name) with gain=$gain knee=$knee cvMax=$cvMax" -ForegroundColor DarkGray

                    $csvOut = Join-Path $paramSetDir "$($curve.name).csv"
                    $jobs += Start-Job -ScriptBlock {
                        param($exePath, $threshold, $gain, $knee, $cvMax, $csvOut, $curveName, $curveWeight)

                        & $exePath --measure-transfer --position 1 --threshold $threshold `
                            --sidechain-gain $gain `
                            --cv-soft-knee $knee `
                            --cv-max $cvMax `
                            --output $csvOut *> $null

                        [pscustomobject]@{
                            curveName = $curveName
                            curveWeight = $curveWeight
                            csvPath = $csvOut
                            success = (Test-Path $csvOut)
                        }
                    } -ArgumentList $exePath, $curve.threshold, $gain, $knee, $cvMax, $csvOut, $curve.name, $curve.weight

                    while ($jobs.Count -ge $CurveParallelism) {
                        $done = Wait-Job -Job $jobs -Any
                        $curveRuns += Receive-Job -Job $done
                        Remove-Job -Job $done
                        $jobs = @($jobs | Where-Object { $_.Id -ne $done.Id })
                    }
                }

                if ($jobs.Count -gt 0) {
                    Wait-Job -Job $jobs | Out-Null
                    foreach ($job in $jobs) {
                        $curveRuns += Receive-Job -Job $job
                        Remove-Job -Job $job
                    }
                }
            } else {
                foreach ($curve in $curves) {
                    $runCount++
                    $pct = [math]::Round(100 * $runCount / $totalRuns, 1)
                    Write-Host "[$pct%] Testing: $($curve.name) with gain=$gain knee=$knee cvMax=$cvMax" -ForegroundColor DarkGray

                    $csvOut = Join-Path $paramSetDir "$($curve.name).csv"

                    & $exePath --measure-transfer --position 1 --threshold $curve.threshold `
                        --sidechain-gain $gain `
                        --cv-soft-knee $knee `
                        --cv-max $cvMax `
                        --output $csvOut 2>&1 | Out-Null

                    $curveRuns += [pscustomobject]@{
                        curveName = $curve.name
                        curveWeight = $curve.weight
                        csvPath = $csvOut
                        success = (Test-Path $csvOut)
                    }
                }
            }

            foreach ($curveRun in $curveRuns) {
                $csvOut = [string]$curveRun.csvPath
                if (-not $curveRun.success) {
                    Write-Warning "Failed to generate: $csvOut"
                    continue
                }
                
                # Parse CSV and calculate right-edge dip metric
                $rows = @(Get-Content $csvOut | Where-Object { $_ -and -not $_.StartsWith('#') }) | ConvertFrom-Csv
                
                if ($rows.Count -lt 5) {
                    Write-Warning "CSV has insufficient rows: $($rows.Count)"
                    continue
                }
                
                $allOutputs = @($rows | ForEach-Object { [double]$_.output_dbfs })
                $allInputs = @($rows | ForEach-Object { [double]$_.input_dbfs })
                $allGR = @($rows | ForEach-Object { [double]$_.gain_reduction_db })
                $allCv = @($rows | ForEach-Object { [double]$_.cv_volts })

                # Evaluate monotonicity only after knee onset (or entire curve when always-active)
                $kneeIndex = 0
                for ($k = 0; $k -lt $allCv.Count; $k++) {
                    if ($allCv[$k] -gt 0.0) {
                        $kneeIndex = $k
                        break
                    }
                }

                $rawDeltas = @()
                for ($i = [math]::Max(1, $kneeIndex); $i -lt $allOutputs.Count; $i++) {
                    $rawDeltas += ($allOutputs[$i] - $allOutputs[$i - 1])
                }

                $monoViolations = @($rawDeltas | Where-Object { $_ -lt (0.0 - $monoSlackDb) }).Count
                
                # Calculate deltas (slope in final region)
                $lastRows = $rows | Select-Object -Last 4
                $outputs = @($lastRows | ForEach-Object { [double]$_.output_dbfs })
                $deltas = @()
                $signedTailDeltas = @()
                for ($i = 1; $i -lt $outputs.Count; $i++) {
                    $deltas += [math]::Abs($outputs[$i] - $outputs[$i-1])
                    $signedTailDeltas += ($outputs[$i] - $outputs[$i-1])
                }
                $tailMinDelta = ($signedTailDeltas | Measure-Object -Minimum).Minimum
                $tailDownturn = $tailMinDelta -lt $tailMinSlopeDb
                
                # Score: sum of absolute deltas (lower = flatter)
                $score = ($deltas | Measure-Object -Sum).Sum

                # Checkpoint GR values for family ordering/separation checks.
                $rowCp1 = Get-NearestRow -Rows $rows -TargetInputDb $Checkpoint1Db
                $rowCp2 = Get-NearestRow -Rows $rows -TargetInputDb $Checkpoint2Db
                $grCp1 = [double]$rowCp1.gain_reduction_db
                $grCp2 = [double]$rowCp2.gain_reduction_db
                
                # Store result
                $results += @{
                    gain         = $gain
                    knee         = $knee
                    cvMax        = $cvMax
                    curveName    = [string]$curveRun.curveName
                    curveWeight  = [double]$curveRun.curveWeight
                    score        = [math]::Round($score, 4)
                    monoViolations = $monoViolations
                    tailMinDelta = [math]::Round($tailMinDelta, 4)
                    tailDownturn = $tailDownturn
                    grAtCheckpoint1 = [math]::Round($grCp1, 4)
                    grAtCheckpoint2 = [math]::Round($grCp2, 4)
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
            perCurveTailMin = @{}
            perCurveMonoViolations = @{}
            perCurveGrAtCheckpoint1 = @{}
            perCurveGrAtCheckpoint2 = @{}
            hardFailures    = @()
            hardPass        = $true
            protocolScore   = [double]0.0
        }
    }
    
    $paramSetScores[$key].perCurveScores[$result.curveName] = $result.score
    $paramSetScores[$key].perCurveTailMin[$result.curveName] = $result.tailMinDelta
    $paramSetScores[$key].perCurveMonoViolations[$result.curveName] = $result.monoViolations
    $paramSetScores[$key].perCurveGrAtCheckpoint1[$result.curveName] = $result.grAtCheckpoint1
    $paramSetScores[$key].perCurveGrAtCheckpoint2[$result.curveName] = $result.grAtCheckpoint2
    $paramSetScores[$key].totalScore += $result.score
    $paramSetScores[$key].weightedScore += $result.score * $result.curveWeight
}

# Apply hard constraints and protocol scoring.
foreach ($entry in $paramSetScores.Values) {
    foreach ($curve in $curves) {
        $name = $curve.name
        if (-not $entry.perCurveMonoViolations.ContainsKey($name)) {
            $entry.hardFailures += "missing_curve_$name"
            continue
        }

        if ([int]$entry.perCurveMonoViolations[$name] -gt 0) {
            $entry.hardFailures += "mono_$name"
        }

        if ([double]$entry.perCurveTailMin[$name] -lt $tailMinSlopeDb) {
            $entry.hardFailures += "tail_$name"
        }
    }

    $baseCp1 = [double]$entry.perCurveGrAtCheckpoint1['thresh10v0']
    $baseCp2 = [double]$entry.perCurveGrAtCheckpoint2['thresh10v0']

    $rel3v5Cp1 = [double]$entry.perCurveGrAtCheckpoint1['thresh3v5'] - $baseCp1
    $rel2v8Cp1 = [double]$entry.perCurveGrAtCheckpoint1['thresh2v8'] - $baseCp1
    $rel2v0Cp1 = [double]$entry.perCurveGrAtCheckpoint1['thresh2v0'] - $baseCp1
    $rel0v0Cp1 = [double]$entry.perCurveGrAtCheckpoint1['thresh0v0'] - $baseCp1

    $rel3v5Cp2 = [double]$entry.perCurveGrAtCheckpoint2['thresh3v5'] - $baseCp2
    $rel2v8Cp2 = [double]$entry.perCurveGrAtCheckpoint2['thresh2v8'] - $baseCp2
    $rel2v0Cp2 = [double]$entry.perCurveGrAtCheckpoint2['thresh2v0'] - $baseCp2
    $rel0v0Cp2 = [double]$entry.perCurveGrAtCheckpoint2['thresh0v0'] - $baseCp2

    # Hard anchor checks: enforce robust family ordering/separation for 3.5 -> 2.0 -> 0.0.
    if ($rel3v5Cp1 -lt -0.10) { $entry.hardFailures += "baseline_rel_cp1" }
    if ($rel3v5Cp2 -lt -0.10) { $entry.hardFailures += "baseline_rel_cp2" }

    if (($rel2v0Cp1 - $rel3v5Cp1) -lt $minSepAtCheckpoint1Db) { $entry.hardFailures += "sep_cp1_thresh3v5_thresh2v0" }
    if (($rel0v0Cp1 - $rel2v0Cp1) -lt $minSepAtCheckpoint1Db) { $entry.hardFailures += "sep_cp1_thresh2v0_thresh0v0" }
    if (($rel2v0Cp2 - $rel3v5Cp2) -lt $minSepAtCheckpoint2Db) { $entry.hardFailures += "sep_cp2_thresh3v5_thresh2v0" }
    if (($rel0v0Cp2 - $rel2v0Cp2) -lt $minSepAtCheckpoint2Db) { $entry.hardFailures += "sep_cp2_thresh2v0_thresh0v0" }

    $entry.hardFailures = @($entry.hardFailures | Select-Object -Unique)
    $entry.hardPass = ($entry.hardFailures.Count -eq 0)

    $reg = [math]::Abs([double]$entry.gain - $baselineGain) +
           [math]::Abs([double]$entry.knee - $baselineKnee) +
           ([math]::Abs([double]$entry.cvMax - $baselineCvMax) / 2.0)
    # Soft penalty for where thresh2v8 sits between thresh3v5 and thresh2v0.
    $midCp1 = ($rel3v5Cp1 + $rel2v0Cp1) / 2.0
    $midCp2 = ($rel3v5Cp2 + $rel2v0Cp2) / 2.0
    $midPenalty = [math]::Abs($rel2v8Cp1 - $midCp1) + [math]::Abs($rel2v8Cp2 - $midCp2)

    $entry.protocolScore = [math]::Round(([double]$entry.weightedScore + ($regularizationWeight * $reg) + (0.25 * $midPenalty)), 6)
}

# Sort by protocol score (lower is better) and prefer hard-pass candidates.
$ranked = $paramSetScores.Values | Sort-Object -Property @{Expression = { if ($_.hardPass) { 0 } else { 1 } }}, @{Expression = { $_.protocolScore }}
$passing = @($ranked | Where-Object { $_.hardPass })

Write-Host ""
Write-Host "Top 10 Parameter Sets (protocol-ranked)" -ForegroundColor Green
Write-Host ""
Write-Host @"
Rank | Gain  | Knee  | CvMax | Pass | ProtoScore | Weighted | Total  | FailCount
"@
Write-Host ('-' * 100)

for ($i = 0; $i -lt [math]::Min(10, $ranked.Count); $i++) {
    $r = $ranked[$i]
    $passText = if ($r.hardPass) { "yes" } else { "no" }
    
    Write-Host ("{0,4} | {1,5:F1} | {2,5:F2} | {3,5:F1} | {4,4} | {5,10:F4} | {6,8:F4} | {7,6:F3} | {8,9}" -f `
        ($i+1), $r.gain, $r.knee, $r.cvMax, $passText, $r.protocolScore, $r.weightedScore, $r.totalScore, $r.hardFailures.Count)
}

Write-Host ""

# Recommend best passing configuration
if ($passing.Count -gt 0) {
    $best = $passing[0]
    Write-Host "RECOMMENDATION" -ForegroundColor Yellow
    Write-Host ("-" * 80)
    Write-Host "Best passing configuration (protocol score = $([math]::Round($best.protocolScore, 4))):"
    Write-Host ""
    Write-Host "  sidechainAmplifierGain = $($best.gain)"
    Write-Host "  sidechainCvSoftKneeV   = $($best.knee)"
    Write-Host "  cvMaxV                 = $($best.cvMax)"
    Write-Host ""
    Write-Host "Per-curve dip scores:"
    foreach ($curve in $curves) {
        $score = if ($best.perCurveScores.ContainsKey($curve.name)) { [math]::Round($best.perCurveScores[$curve.name], 4) } else { 'N/A' }
        Write-Host "  $($curve.name): $score (weight=$($curve.weight))"
    }
} else {
    Write-Host "No candidate passed hard protocol constraints." -ForegroundColor Red
    $best = $null
}
Write-Host ""

# Save results to JSON for further analysis
$summaryPath = Join-Path $OutputDir "sweep_results.json"
$ranked | ConvertTo-Json | Set-Content $summaryPath
Write-Host "Full results saved to: $summaryPath" -ForegroundColor DarkGray

$protocolSummaryPath = Join-Path $OutputDir "protocol_summary.json"
$protocolSummary = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    thresholds = [ordered]@{
        checkpoint1Db = $Checkpoint1Db
        checkpoint2Db = $Checkpoint2Db
        monoSlackDb = $monoSlackDb
        tailMinSlopeDb = $tailMinSlopeDb
        minSepAtCheckpoint1Db = $minSepAtCheckpoint1Db
        minSepAtCheckpoint2Db = $minSepAtCheckpoint2Db
    }
    passingCount = $passing.Count
    bestPassing = if ($best -ne $null) { [ordered]@{ gain = $best.gain; knee = $best.knee; cvMax = $best.cvMax; protocolScore = $best.protocolScore } } else { $null }
    passingCandidates = @($passing | Select-Object gain, knee, cvMax, protocolScore, weightedScore, totalScore)
}
$protocolSummary | ConvertTo-Json -Depth 6 | Set-Content $protocolSummaryPath
Write-Host "Protocol summary saved to: $protocolSummaryPath" -ForegroundColor DarkGray

# Ask user to confirm and lock in
Write-Host ""
Write-Host "Action Required:" -ForegroundColor Cyan
Write-Host "1. Review protocol_summary.json and top protocol-ranked candidates"
if ($best -ne $null) {
    Write-Host "2. If satisfied, run: .\scripts\lock_and_regenerate.ps1 -SidechainGain $($best.gain) -CvSoftKnee $($best.knee) -CvMax $($best.cvMax)"
    Write-Host "   OR choose another PASSING candidate from protocol_summary.json"
} else {
    Write-Host "2. No lock step allowed: adjust model/sweep and rerun until at least one passing candidate exists"
}
Write-Host ""

if ($passing.Count -gt 0) {
    exit 0
}

exit 2
