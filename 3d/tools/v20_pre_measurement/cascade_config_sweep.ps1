# MBRC v2.0-pre — Cascade-config sweep (discriminator for hypothesis gamma).
#
# Tests whether increasing cascade angular bin count (dirRes, D, D^2 rays/probe)
# shrinks the asymmetric cascade-vs-PT delta regions found in the full sweep
# (doc/7/mbrc_v20_pre_measurement_report.md §3.5 / §8 / §11).
#
# Hypothesis (gamma) predicts: doubling D significantly reduces blue+red region
# area on mode 19 (GI-only delta) at BOTH cam0 AND cam2. If invariant under D,
# (gamma) is rejected and the leading hypothesis flips to (alpha) merge-weighting
# or (beta) MB-gain.
#
# Matrix: 3 D values x 2 cameras x 2 modes = 12 captures, uniform-D (scaled=0).
#   D in {4, 8, 16}
#   cam in {0 (front-on), 2 (front-left elevated, inverts screen-space)}
#   mode in {18 (combined delta), 19 (GI-only delta)}
#   seed=0 (single; bug-230 deferred)
#   hybrid=off (clean baseline cascade)
#   frames=512 (matches v2.0-pre full sweep for direct comparison)
#
# bug-211 workaround: raylib TakeScreenshot strips path. Capture to basename,
# Move-Item to outDir.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$outDir    = "tools/v20_pre_measurement/captures_cfg"
$frames    = 512
$dirReses  = @(4, 8, 16)
$cameras   = @(0, 2)
$modes     = @(18, 19)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam, [int]$D, [int]$mode)
    $tag  = "cam${cam}_d$('{0:D2}' -f $D)_m$('{0:D2}' -f $mode)"
    $base = "${tag}.png"
    Write-Host "[capture] $tag"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--cascade-dir-res=$D",
        "--cascade-scaled-dir-res=0",
        "--noise-seed-offset=0",
        "--render-mode=$mode",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList 2>&1 |
        Select-String -Pattern "cascade-dir-res|cascade-scaled-dir-res|Phase 8|render-mode|measurement-camera|screenshot saved" |
        ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
        Write-Host "  saved: $outDir/$base"
    } else {
        Write-Host "  WARN: no png produced for $tag"
    }
}

$total = $dirReses.Count * $cameras.Count * $modes.Count
$i = 0
$tStart = Get-Date
foreach ($D in $dirReses) {
    foreach ($cam in $cameras) {
        foreach ($mode in $modes) {
            $i++
            Write-Host "----- [$i/$total] -----"
            Capture -cam $cam -D $D -mode $mode
        }
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] cascade-config sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
