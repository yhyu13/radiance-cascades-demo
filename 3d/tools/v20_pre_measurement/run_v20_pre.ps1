# v2.0-pre measurement harness — engine-side capture stub.
#
# Scope (this script): drive the engine to produce per-(camera, cascade-exclude,
# noise-seed) screenshots + cascade-config.json files into tools/v20_pre_measurement/captures/.
# This script does NOT compute RMSE — that ships in a follow-up Python tool
# (analyze.py) once the PT-cache EXR dumper lands. See impl doc §6 "Deferred".
#
# Per the plan (doc/7/mbrc_v20_pre_measurement_plan.md):
#   - 3 cameras (cam0..cam2 from cameras.json)
#   - cascade-exclude sweep: {-1, 0, 1, 2, 3} (baseline + leave-one-out per cascade)
#   - 4 noise-seed offsets per (camera, exclude) for the noise-floor estimate
#   - For each capture: --error-decomp-mode={0,1,2,3} via separate runs gives the
#     4 artifacts the spatial-vs-angular decomposition needs.
#
# Total captures: 3 cams * 5 excludes * 4 seeds * 4 decomp-modes = 240 screenshots.
# Each run is ~12s headless (load + warmup + capture); ~50 min wall-clock total.
#
# Usage:
#   pwsh tools/v20_pre_measurement/run_v20_pre.ps1 [-WarmupFrames 32] [-DryRun]
#
# Output:
#   tools/v20_pre_measurement/captures/cam{N}_excl{X}_seed{S}_mode{M}_frame{F}.png
#   tools/v20_pre_measurement/captures/cascade_config_*.json (one per capture)

param(
    [int]    $WarmupFrames   = 32,
    [string] $Scene          = "cornell-orig-alcove",
    [string] $Exe            = "build/Release/RadianceCascades3D.exe",
    [string] $CamerasJson    = "tools/v20_pre_measurement/cameras.json",
    [string] $OutDir         = "tools/v20_pre_measurement/captures",
    [int[]]  $Cameras        = @(0,1,2),
    [int[]]  $Excludes       = @(-1,0,1,2,3),
    [int[]]  $Seeds          = @(0,1,2,3),
    [int[]]  $DecompModes    = @(0,1,2,3),
    [switch] $DryRun
)

if (-not (Test-Path $Exe)) {
    Write-Error "Engine binary not found: $Exe. Run build.ps1 first."
    exit 1
}
if (-not (Test-Path $CamerasJson)) {
    Write-Error "cameras.json not found: $CamerasJson"
    exit 1
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

$total = $Cameras.Count * $Excludes.Count * $Seeds.Count * $DecompModes.Count
$i = 0
Write-Host "[v20-pre] $total captures planned ($($Cameras.Count) cams x $($Excludes.Count) excludes x $($Seeds.Count) seeds x $($DecompModes.Count) decomp-modes)"

foreach ($cam in $Cameras) {
foreach ($excl in $Excludes) {
foreach ($seed in $Seeds) {
foreach ($mode in $DecompModes) {
    $i++
    $tag = "cam${cam}_excl${excl}_seed${seed}_mode${mode}"
    $args = @(
        "--load-obj=$Scene",
        "--measurement-cameras-file=$CamerasJson",
        "--measurement-camera=$cam",
        "--cascade-exclude=$excl",
        "--noise-seed-offset=$seed",
        "--render-mode=20",
        "--error-decomp-mode=$mode",
        "--exit-frames=$WarmupFrames",
        "--screenshot=$tag",
        "--cascade-config-dump"
    )
    Write-Host "[$i/$total] $tag"
    if ($DryRun) {
        Write-Host "  (dry-run) $Exe $args"
    } else {
        & $Exe $args 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "[$i/$total] $tag exit=$LASTEXITCODE"
        }
    }
}}}}

Write-Host "[v20-pre] done. Captures in $OutDir"
Write-Host "[v20-pre] Next: PT-cache EXR dump + python analyze.py (deferred, see impl doc §6)."
