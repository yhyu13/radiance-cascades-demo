# MBRC v2.0 (h.c)'' downstream-knobs final rule-out at cam0+cam2
#
# Context: (h.b) ruled out blend zone, (h.c) ruled out probe-cell boundary
# aliasing at the mean, (h.c)' ruled out spatial trilinear (revealed it as
# a symmetrizer). The remaining downstream toggles BEFORE bake-side are:
#   - useDirectionalMerge (DM): 0 = isotropic fallback (bypass per-direction
#     sampling entirely, use texture(uRadiance) avg), 1 = per-direction-bin
#     atlas lookup (default)
#   - useDirBilinear (DB): 0 = nearest-bin texelFetch, 1 = 4-bin bilinear
#     inside per-direction lookup (default)
#
# Hypothesis: if BOTH DM=0 AND DM=1+DB=0 fail to close the cam0/cam2 spread,
# the entire downstream consumption path is locked-in innocent and the
# asymmetry source MUST be bake-side per-direction-bin atlas content.
#
# Cells: 4 new (DM=0 cam{0,2} ST=1 + DM=1 DB=0 cam{0,2} ST=1), reuse 2 from
# h4_smoothstep for DM=1 DB=1 ST=1 baseline. Total 6 configs.
#
# Pre-committed verdict bands (analyzer computes spread cam2/cam0 per config):
#
#   delta_spread(config) = spread(config) - spread(DM1_DB1)  # vs baseline
#
#     |delta| >= 0.10 with sign POSITIVE -> DOWNSTREAM_KNOB_PRIMARY_DRIVER
#                                          (this knob closes the gap; bake-side
#                                          framing rejected)
#     |delta| in [0.03, 0.10) POSITIVE   -> DOWNSTREAM_KNOB_PARTIAL_CONTRIBUTOR
#     |delta| < 0.03 OR NEGATIVE         -> DOWNSTREAM_KNOB_INNOCENT
#                                          (this knob doesn't close the gap;
#                                          bake-side framing strengthened)
#
# Per-cam sub-check (cerebrum DNR 2026-05-24): report per-cam absolute deltas
# alongside spread delta so symmetrizer-vs-contributor pattern is visible.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h7_downstream"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# Configs: (tag, useDirectionalMerge, useDirBilinear)
# DM=1 DB=1 is the default baseline (reuse h.b smoothstep cells).
$configs = @(
    @("dm0",    0, 1),  # isotropic fallback
    @("dm1db0", 1, 0)   # nearest-bin
)

function Capture {
    param([string]$cfgTag, [int]$dm, [int]$db, [int]$cam)
    $tag  = "alcove_cam${cam}_${cfgTag}_M0_b2_mboff_m17"
    $base = "${tag}.png"
    Write-Host "[h7] $tag (DM=$dm DB=$db ST=1, M0, MB OFF, b=2)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--pt-max-bounces=2",
        "--use-multi-bounce=0",
        "--use-directional-merge=$dm",
        "--use-dir-bilinear=$db",
        "--use-spatial-trilinear=1",
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
Write-Host "===== v2.0 (h.c)'' downstream knobs final rule-out ====="
foreach ($cfg in $configs) {
    foreach ($cam in @(0, 2)) {
        Capture -cfgTag $cfg[0] -dm $cfg[1] -db $cfg[2] -cam $cam
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
