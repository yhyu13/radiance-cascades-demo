# MBRC v2.0 (h.b) smoothstep blend-zone toggle A/B at MB-OFF b=2
#
# Hypothesis (b): The smoothstep S-curve in the cascade->upper blend zone
# (radiance_3d.comp:771 area) compresses cascade contribution near tMax,
# under-supplying side-illuminated regions (cam2 alcove).
#
# Test: A/B the new uBlendMode uniform (0=smoothstep [default],
# 1=linear ramp, 2=step at midpoint) at the most-feature-rich merge
# (M0: dm=1 db=1 st=1) on cam0 + cam2, MB-OFF, b=2.
#
# Pre-committed verdict bands (analyzer computes per-cam cascade/PT ratio
# delta vs blend_mode=0 baseline):
#
#   On cam2, |ratio(mode=1) - ratio(mode=0)| and |ratio(mode=2) - ratio(mode=0)|:
#     <= 0.02 absolute  -> BLEND_ZONE_NOT_THE_BUG
#                          (smoothstep math is innocent; pivot to (c) atlas
#                          content / probe-cell oversampling investigation)
#     0.02..0.10 abs    -> BLEND_ZONE_PARTIAL_CONTRIBUTOR
#                          (small leak attributable to S-curve; useful but
#                          not sufficient to close cam0/cam2 spread)
#     > 0.10 abs        -> BLEND_ZONE_PRIMARY_SUSPECT
#                          (S-curve is materially under-supplying cam2;
#                          investigate replacing default with linear or
#                          adaptive blend)
#
#   ALSO: spread analysis per blend mode
#     spread(mode) = |cam0_ratio - cam2_ratio|
#     If spread(linear) < spread(smoothstep) by >= 0.05 -> linear symmetrizes
#     If spread(step)   < spread(smoothstep) by >= 0.05 -> hard step symmetrizes
#     If neither -> blend curve shape doesn't move the spread needle

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h4_smoothstep"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# (modeName, blendMode)
$modes = @(
    @("smoothstep", 0),   # baseline (default)
    @("linear",     1),
    @("step",       2)
)

function Capture {
    param([string]$modeName, [int]$blendMode, [int]$cam)
    $tag  = "alcove_cam${cam}_blend_${modeName}_M0_b2_mboff_m17"
    $base = "${tag}.png"
    Write-Host "[h4] $tag (blendMode=$blendMode, M0 full features, MB OFF, b=2)"
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
        "--blend-mode=$blendMode",
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
Write-Host "===== v2.0 (h.b) smoothstep blend-zone A/B at MB-OFF b=2 ====="
foreach ($mode in $modes) {
    foreach ($cam in @(0, 2)) {
        Capture -modeName $mode[0] -blendMode $mode[1] -cam $cam
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
