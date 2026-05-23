# MBRC v2.0 — cam0/cam2 asymmetry diagnostic sweep
#
# Per [doc/7/mbrc_v20_pre_measurement_report.md §16.5] Priority 1: localize
# where the cam2 residual lives under the new defaults (M4_iso_nearest +
# MB g=1.0, committed in d64ea17).
#
# Triple-stack ceiling left cam0=0.681 / cam2=0.392 ratio to PT — cam0 gap
# 0.32, cam2 gap 0.61. The asymmetry persists at every stack level and is
# not a tunable axis. This sweep asks: does cam2's mode-19 GI delta
# (cascade_GI − PT_GI) spatially co-localize with cam2's mode-14 leak-
# suspect heatmap (atlas radiance in α=0 bins)? If yes, the residual lives
# at the bake-side leak boundary and a bake-time clamp is the architectural
# move. If no, the residual is elsewhere (smoothstep blend zone, probe-
# grid view angle, merge fetch geometry) and the next diagnostic is
# different.
#
# 8 captures, ~2 min:
#   cam ∈ {0, 2} × mode ∈ {0 composite, 14 leak, 18 total delta, 19 GI delta}
#
# All under new engine defaults (no config CLI flags). Hybrid OFF.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_asymmetry"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function MoveScreenshot {
    param([string]$tag)
    $src = "${tag}.png"
    if (Test-Path $src) { Move-Item -Force $src "$outDir/$src" }
    else { Write-Host "  WARN: missing $src" }
}

$cams  = @(0, 2)
$modes = @(0, 14, 18, 19)

function CaptureCell {
    param([int]$cam, [int]$mode)
    $tag  = "alcove_cam${cam}_m${mode}_newdefault"
    $base = "${tag}.png"
    Write-Host "[diag] $tag"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--noise-seed-offset=0",
        "--render-mode=$mode",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "screenshot saved|useDirectional|useDirBilinear|useSpatial|multi-bounce|MB.*cascade-bake" |
        ForEach-Object { Write-Host "  $_" }
    MoveScreenshot -tag $tag
}

$tStart = Get-Date
Write-Host "===== v2.0 cam2-asymmetry diagnostic sweep ====="
$i = 0
$total = $cams.Count * $modes.Count
foreach ($cam in $cams) {
    foreach ($mode in $modes) {
        $i++
        Write-Host "----- diag [$i/$total] -----"
        CaptureCell -cam $cam -mode $mode
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] asymmetry diagnostic sweep in $($elapsed.TotalMinutes.ToString('F1')) min"
