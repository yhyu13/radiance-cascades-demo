# MBRC v2.0 (h.c)''' spatial-trilinear=0 mitigation validation across scenes
#
# (h.c)' on cornell-orig-alcove found that --use-spatial-trilinear=0
# IMPROVES integration quality on BOTH cams (cam0 +21%, cam2 +9% in
# cascade/PT ratio) but WIDENS the cam0/cam2 spread (0.65 -> 0.58).
# (h.c)'' confirmed ST=1 is one of several downstream symmetrizers.
#
# Mitigation question: should we ship --use-spatial-trilinear=0 as the
# default? The cam0/cam2 spread widening is a regression on alcove view
# multi-camera comparison, but the absolute quality gain is real and
# should reproduce on simpler scenes that don't have alcove geometry.
#
# Test: capture default Cornell (cornell) and cornell-orig (alcove without
# the alcove cutout) at ST=0 vs ST=1 with default auto-fit camera.
# 4 cells total. Compare cascade/PT ratio per (scene, ST).
#
# Pre-committed verdict bands (analyzer reports per-scene delta-of-ratios):
#
#   ratio_st0 - ratio_st1:
#     >= +0.05 -> ST0_IMPROVES_QUALITY (mitigation flag candidate)
#     -0.05..+0.05 -> ST_NEUTRAL (no quality difference)
#     <= -0.05 -> ST1_BETTER (alcove finding doesn't generalize)
#
# If BOTH scenes land ST0_IMPROVES_QUALITY, recommend flipping default.
# If mixed or NEUTRAL, hold default at ST=1.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h8_st0_mitigation"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# (sceneName, sceneTag)
$scenes = @(
    @("cornell",      "cornell_default"),
    @("cornell-orig", "cornell_orig")
)

function Capture {
    param([string]$sceneArg, [string]$sceneTag, [int]$st)
    $stTag = if ($st -eq 1) { "st1" } else { "st0" }
    $tag   = "${sceneTag}_${stTag}_M0_b2_mboff_m17"
    $base  = "${tag}.png"
    Write-Host "[h8] $tag (scene=$sceneArg ST=$st M0 MB-OFF b=2)"
    $argList = @(
        "--load-obj=$sceneArg",
        "--use-hybrid=0",
        "--pt-max-bounces=2",
        "--use-multi-bounce=0",
        "--use-directional-merge=1",
        "--use-dir-bilinear=1",
        "--use-spatial-trilinear=$st",
        "--blend-mode=0",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|blend-mode|use-directional|use-dir-bilinear|use-spatial|multi-bounce|pt-max-bounces|PT accumulators|load-obj" |
        ForEach-Object { Write-Host "  $_" }
    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

$tStart = Get-Date
Write-Host "===== v2.0 (h.c)''' ST=0 mitigation validation across 2 scenes ====="
foreach ($scene in $scenes) {
    foreach ($st in @(1, 0)) {
        Capture -sceneArg $scene[0] -sceneTag $scene[1] -st $st
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
