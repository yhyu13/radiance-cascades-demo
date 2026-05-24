# MBRC v2.0 (h.c)' spatial-trilinear A/B at cam0 + cam2
#
# Hypothesis (c)': cam2's fract-corner-bias (h.c finding: 47.7% of cam2
# surface pixels piled into fract-distance bin [0.40,0.45) vs cam0's 19.6%;
# cam2 mean_fract 3.9x farther from cell center than cam0, biased to a
# specific corner) causes consistent 8-neighbor trilinear weight selection
# at every cam2 pixel -> if those weighted-heavily probes have dim direction
# bins, cam2 sees a dim cascade.
#
# Test: capture cam0 + cam2 at --use-spatial-trilinear=0 (nearest-parent
# spatial lookup). Compare cascade/PT ratio + cam2/cam0 spread to the
# (h.b) smoothstep baseline (ST=1, same M0 / MB-OFF / b=2 config).
#
# Pre-committed verdict bands (analyzer reads 4 cells: 2 reused from h4 at
# ST=1, 2 new at ST=0):
#
#   delta = spread(ST=0) - spread(ST=1)   # spread = cam2_ratio / cam0_ratio
#   (positive delta means ST=0 SHRINKS the cam0/cam2 gap, i.e. fract chain
#    was contributing to the under-supply)
#
#     >= +0.10  -> SPATIAL_TRILINEAR_PRIMARY_CONTRIBUTOR
#                 (disabling spatial trilinear materially closes the gap;
#                  fract-bias -> trilinear-weight chain is the live driver)
#     +0.03..+0.10 -> SPATIAL_TRILINEAR_PARTIAL_CONTRIBUTOR
#                 (small symmetrization; chain contributes but other
#                  layers also matter)
#     -0.03..+0.03 -> SPATIAL_TRILINEAR_NOT_THE_DRIVER
#                 (fract distribution is a red herring; asymmetry is
#                  intrinsic to per-direction-bin atlas content)
#     <= -0.03 -> SPATIAL_TRILINEAR_WIDENS_SPREAD
#                 (nearest-parent makes cam2 WORSE; either ST=1 is
#                  partially compensating an upstream issue, or the
#                  nearest-parent probe selected at cam2 is unusually dim)
#
# Plus shape-asymmetry sub-check (per cerebrum DNR 2026-05-24): report
# per-cam ratio deltas independently — if cam0 also changes significantly,
# the "fract drives the asymmetry" framing is weak (ST=0 shouldn't move
# cam0 much because cam0's fract is near-uniform across cells).

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h6_spatial_trilinear"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam, [int]$st)
    $stTag = if ($st -eq 1) { "st1" } else { "st0" }
    $tag   = "alcove_cam${cam}_${stTag}_M0_b2_mboff_m17"
    $base  = "${tag}.png"
    Write-Host "[h6] $tag (use-spatial-trilinear=$st, M0 default, MB OFF, b=2)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
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
        Select-String -Pattern "hdr-exr|screenshot saved|blend-mode|use-directional|use-dir-bilinear|use-spatial|multi-bounce|pt-max-bounces|PT accumulators" |
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
Write-Host "===== v2.0 (h.c)' spatial-trilinear A/B at MB-OFF b=2 ====="
# Only capture ST=0 cells; ST=1 cells reused from (h.b) smoothstep baseline.
foreach ($cam in @(0, 2)) {
    Capture -cam $cam -st 0
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
