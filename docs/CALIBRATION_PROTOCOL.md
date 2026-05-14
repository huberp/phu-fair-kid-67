# Fairchild 670 Calibration Protocol

This document defines the strict gating protocol for transfer-curve calibration so changes remain analog-faithful across the full five-curve family.

## Scope

Applies to all calibration work that changes transfer-curve outputs, including updates to:
- sidechain CV law parameters
- threshold mapping behavior
- stage CV ceiling and soft-knee behavior
- generated transfer reference CSV/PNG outputs

## Protocol Goal

Ensure all five threshold curves preserve expected family behavior while avoiding synthetic post-knee downturn artifacts.

## Curve Set

The protocol always evaluates these five generated curves:
- thresh10v0
- thresh3v5
- thresh2v8
- thresh2v0
- thresh0v0

## Hard Constraints (must pass)

1. Monotonic output progression post-knee
- For each curve, output_dbfs must not decrease by more than monotonicity slack between successive post-knee samples.

2. Right-edge tail behavior
- Tail-region slope must not go below the negative tail threshold.
- Any sustained right-edge downturn beyond tolerance fails the candidate.

3. Family ordering at high levels
- At each checkpoint, hard anchor ordering uses relative gain reduction versus thresh10v0:
  thresh3v5 <= thresh2v0 <= thresh0v0
- thresh2v8 placement is scored as a soft constraint (not hard fail) because the current
  single-threshold model cannot always place it exactly between thresh3v5 and thresh2v0.

4. Family separation at high levels
- Minimum adjacent spacing must be met at high-level checkpoints.

5. Stability sanity
- Candidate data must be finite and complete for all five curves.

## Scored Objectives (for passing candidates)

After hard constraints, candidates are ranked by protocol score:
- weighted dip score (legacy curve smoothness metric)
- regularization term (distance from baseline defaults)

The best passing candidate is the one with the lowest protocol score.

## Current Default Thresholds (second draft)

These are implemented in scripts/sweep_global_calibration.ps1:
- monotonicity slack: 0.10 dB
- minimum tail slope: -0.25 dB
- checkpoint 1: -9 dBFS, minimum adjacent separation: 0.50 dB
- checkpoint 2: -6 dBFS, minimum adjacent separation: 0.50 dB

These thresholds are command-line tunable in the sweep script.

## Required Workflow

1. Run constrained sweep
- scripts/sweep_global_calibration.ps1

Example (quick constrained exploration):
- scripts/sweep_global_calibration.ps1 -Quick

Example (custom thresholds):
- scripts/sweep_global_calibration.ps1 -Checkpoint1Db -9 -Checkpoint2Db -6 -MinSepAtCheckpoint1Db 0.6 -MinSepAtCheckpoint2Db 0.5

2. Review protocol artifacts
- tmp/calibration_sweep/sweep_results.json
- tmp/calibration_sweep/protocol_summary.json
- tmp/calibration_sweep/protocol_report.json
- tmp/calibration_sweep/sensitivity_matrix.json
- tmp/calibration_sweep/protocol_report.md

3. Lock only passing parameters
- scripts/lock_and_regenerate.ps1 -SidechainGain <g> -CvSoftKnee <k> -CvMax <m>
- The lock script validates selected parameters against passingCandidates.

4. Regenerate references and run tests
- scripts/generate_transfer_references.ps1
- transfer tests must pass before commit.

## Lock Gate Enforcement

By default, lock_and_regenerate.ps1 blocks locking unless:
- protocol_summary.json exists
- passingCandidates is non-empty
- chosen tuple (gain, knee, cvMax) is present in passingCandidates

Manual override is available with -SkipProtocolGate, but this should be used only for controlled experiments and never for reference-lock commits.

## CI / Review Policy

Any PR that updates transfer calibration or reference curves should include:
- protocol_summary.json with at least one passing candidate
- protocol_report.json (ordering/separation/monotonicity/tail/CV utilization summary)
- sensitivity_matrix.json proving parameter influence is non-negligible
- selected tuple and protocol score in PR description
- regenerated CSV references and transfer-test pass evidence
- multi-curve family plot and delta plot evidence (including checkpoint callouts)

## Notes

This is a second-draft protocol and may be tightened as manual-reference digitization and per-threshold tests become stricter.
