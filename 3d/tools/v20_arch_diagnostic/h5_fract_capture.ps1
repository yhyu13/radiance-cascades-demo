# MBRC v2.0 (h.c) Probe-cell fract(pg) viz on cam0 vs cam2
#
# Hypothesis (c): cam2 systematically samples more probe-cell boundaries
# than cam0, causing trilinear weight aliasing -> the under-supply we see in
# cam2's cascade/PT ratio.
#
# Test: capture render mode 8 (per-pixel fract(probeGridCoord) RGB) on
# cam0 and cam2 at the same engine config used in (h.b). No PT, no EXR
# needed - mode 8 is a pure visualization.
#
# Pre-committed verdict bands (analyzer reads PNG, computes per-pixel
# distance-from-cell-center metric d = max(|f.x-0.5|, |f.y-0.5|, |f.z-0.5|),
# averaged over surface-hit pixels - sky pixels exclude via alpha=1 sentinel):
#
#   mean_d(cam2) - mean_d(cam0):
#     >= +0.05  -> CAM2_OVERSAMPLES_BOUNDARIES (real effect, follow up)
#     -0.05..+0.05 -> CAM2_PROBE_COVERAGE_NEUTRAL (cell-position
#                     distribution is symmetric -> spread not driven by
#                     spatial probe-cell aliasing; pivot to per-direction-bin
#                     sampling hypothesis)
#     <= -0.05 -> CAM2_OVERSAMPLES_CELL_CENTERS (opposite of predicted;
#                 cam0 is boundary-heavier - novel finding)
#
# Also: histogram comparison via JSON for visual inspection in doc.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 64
$outDir = "tools/v20_arch_diagnostic/captures_h5_fract"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam)
    $tag  = "alcove_cam${cam}_M0_b2_mboff_m8"
    $base = "${tag}.png"
    Write-Host "[h5] $tag (mode 8, M0 default, MB OFF, b=2)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--pt-max-bounces=2",
        "--use-multi-bounce=0",
        "--use-directional-merge=1",
        "--use-dir-bilinear=1",
        "--use-spatial-trilinear=1",
        "--blend-mode=0",
        "--noise-seed-offset=0",
        "--render-mode=8",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "screenshot saved|render-mode|use-directional|use-dir-bilinear|use-spatial" |
        ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
    } else {
        Write-Host "  WARN: missing $base"
    }
}

$tStart = Get-Date
Write-Host "===== v2.0 (h.c) probe-cell fract(pg) cam A/B ====="
foreach ($cam in @(0, 2)) {
    Capture -cam $cam
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
