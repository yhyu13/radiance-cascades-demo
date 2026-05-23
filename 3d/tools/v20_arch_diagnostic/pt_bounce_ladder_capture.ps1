# MBRC v2.0 self-critique: PT-bounce-ladder capture
#
# Per doc/7/v20_absolute_residual_impl.md section 5 critique: the
# "NET_UNDER_BRIGHT" verdict assumed apples-to-apples cascade-vs-PT
# comparison, but cascade integrates ~single-bounce + MB temporal feedback
# while PT default integrates max_bounces=8. The dimness may be structural
# (single-bounce-vs-multi-bounce gap), not an energy-loss bug.
#
# This sweep captures pt_full at max_bounces = {2, 4, 8} alongside the
# always-direct-only pt_direct (hard-coded max_bounces=1). The analyzer
# then computes |Sum+/Sum-| at each rung. If b=2 closes the gap toward
# [0.7, 1.3], "(f) bake-time energy loss" hypothesis is FALSIFIED -- the
# gap is bounce-count mismatch. If b=2 still reads NET_UNDER_BRIGHT, the
# single-bounce integration itself is leaky and (f) survives.
#
# 6 captures (3 bounces x 2 cams), ~3 min.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_pt_bounce_ladder"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam, [int]$bounces)
    $tag  = "alcove_cam${cam}_b${bounces}_m17"
    $base = "${tag}.png"
    Write-Host "[capture] $tag (cam=$cam, pt-max-bounces=$bounces)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--pt-max-bounces=$bounces",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|pt-max-bounces|PT accumulators" |
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
Write-Host "===== v2.0 PT bounce-ladder capture ====="
foreach ($cam in @(0, 2)) {
    foreach ($b in @(2, 4, 8)) {
        Capture -cam $cam -bounces $b
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
