# MBRC v2.0 P2-E — P2 framework validation on symmetric control scene
#
# Context: B+D steps both confirmed cam0/cam2 per-bin atlas asymmetry on
# cornell-orig-alcove is structural (not D-induced, not MB-curable). Before
# committing fix-time engineering for Option A (bake-side bin-coverage /
# firefly-clamp / direction-aware probes), validate the measurement method
# itself on a flat-symmetric scene.
#
# If cornell-orig (symmetric variant, no alcove) shows similar cam0/cam2 dx
# collapse magnitude, then the P2 measurement framework has built-in
# cam-dependent bias and all Option A fix candidates are premature
# (we'd be fixing a measurement artifact, not a scene property).
#
# If cornell-orig shows DRAMATICALLY reduced cam2 collapse, the P2 framework
# is unbiased; the alcove geometry is the genuine source of cam2 asymmetry;
# Option A is warranted.
#
# Config: same as P2 baseline (MB-OFF, D=8 default, mode 22, render-mode 22,
# 256 frames). Only --load-obj differs (cornell-orig vs cornell-orig-alcove).
# Same cam0+cam2 from cameras.json. NOTE: cameras.json was authored for
# cornell-orig-alcove geometry; cam2 (-1.6, 1.5, 2.6) viewing (0.5, 0.7, 0)
# still views the cornell-orig symmetric room from the same world-space
# pose, so this is a clean cam-as-control comparison.
#
# Pre-committed verdict bands (per-row weighted JS, cornell-orig cam0 vs cam2):
#
#   P2_FRAMEWORK_VALIDATED  -> per-row JS <= 0.05 (cam2 collapses to symmetric
#                              distribution on flat-symmetric scene; P2
#                              framework is unbiased; alcove genuinely
#                              causes the cornell-orig-alcove asymmetry;
#                              Option A warranted).
#   P2_FRAMEWORK_PARTIAL    -> per-row JS in (0.05, 0.10] (framework mostly
#                              unbiased; small residual cam-dependent bias
#                              accounts for ~30% of the alcove asymmetry).
#   P2_FRAMEWORK_BORDERLINE -> per-row JS in (0.10, 0.13] (framework
#                              substantially biased; Option A fix would
#                              partially target a measurement artifact).
#   P2_FRAMEWORK_BIASED     -> per-row JS > 0.13 (framework biased to a
#                              degree that matches alcove case 0.153;
#                              Option A premature — investigate measurement
#                              bias first).
#
# Capture cost: ~0.5 min (2 cells x ~15s).

$exe    = "./build/RadianceCascades3D.exe"
$frames = 256
$outDir = "tools/v20_arch_diagnostic/captures_p2_dombin_control"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam)
    $tag  = "ctrl_cornell-orig_cam${cam}_M0_b1_mboff_dombin_m22"
    $base = "${tag}.png"
    Write-Host "[p2-ctrl] $tag (mode 22, M0, MB OFF, b=1, D=8 default, cornell-orig SYMMETRIC)"
    $argList = @(
        "--load-obj=cornell-orig",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--use-multi-bounce=0",
        "--blend-mode=0",
        "--noise-seed-offset=0",
        "--render-mode=22",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|render mode|load-obj|use-multi-bounce" |
        ForEach-Object { Write-Host "  $_" }
    foreach ($suffix in @(".png", "_dombin.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

$tStart = Get-Date
Write-Host "===== v2.0 P2-E framework validation (cornell-orig symmetric, cam0+cam2) ====="
foreach ($cam in @(0, 2)) {
    Capture -cam $cam
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"

Write-Host ""
Write-Host "===== Analysis (cornell-orig cam0 vs cam2) ====="
$cam0 = "$outDir/ctrl_cornell-orig_cam0_M0_b1_mboff_dombin_m22_dombin.exr"
$cam2 = "$outDir/ctrl_cornell-orig_cam2_M0_b1_mboff_dombin_m22_dombin.exr"
$json = "$outDir/p2_dombin_control_results.json"
python tools/v20_arch_diagnostic/analyze_p2_dombin.py $cam0 $cam2 $json
