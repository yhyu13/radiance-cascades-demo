# MBRC v2.0 (h.3) MB-ON x {M0,M2,M4} factorial fill-in at b=2
#
# Per doc/7/v20_h2_merge_asymmetry_impl.md sec 6: (h.2) measured the
# MB-OFF leg of the M0/M2/M4 sweep. To close the data gap, also need
# MB-ON M0 and MB-ON M2 (MB-ON M4 already in captures_pt_bounce_ladder/).
#
# This sweep adds 4 new cells (M0 + M2, MB-ON, each on cam0+cam2),
# total ~1 min. The full 2x3x2 factorial after this:
#
#   MB-OFF x M0/M2 x cam0/cam2  -> captures_h2_merge/
#   MB-OFF x M4    x cam0/cam2  -> captures_h_disambig/
#   MB-ON  x M0/M2 x cam0/cam2  -> THIS SWEEP (captures_h3_mb_factorial/)
#   MB-ON  x M4    x cam0/cam2  -> captures_pt_bounce_ladder/ (b=2 cells)
#
# Pre-committed verdict (analyzer computes per-cam MB multiplier =
# MB_ON_ratio / MB_OFF_ratio for each merge variant; key question: is
# the multiplier uniform across variants?):
#
#   max(MB_mult) / min(MB_mult) per cam:
#     <= 1.10 -> MB_MULTIPLIER_INVARIANT (MB amp is merge-independent;
#                M4+MB super-additivity in alpha_m4_deepdive was b=8
#                artifact, not b=2 reproducible)
#     1.10..1.50 -> MB_MULTIPLIER_MILDLY_MERGE_DEPENDENT
#     > 1.50 -> MB_MULTIPLIER_STRONGLY_MERGE_DEPENDENT (M4+MB
#               super-additivity is real and merge-formula-specific;
#               supports the alpha_m4_deepdive +16.8%/+39.4% finding)
#
#   ALSO: spread comparison MB-OFF vs MB-ON per variant:
#     MB-ON spread >= MB-OFF spread per variant -> MB amplifies asymmetry
#     MB-ON spread < MB-OFF spread per variant -> MB partially symmetrizes

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h3_mb_factorial"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# (cellName, dm, db, st)
$cells = @(
    @("M0_baseline",   1, 1, 1),
    @("M2_iso_merge",  0, 1, 1)
    # M4_iso_nearest MB-ON already in captures_pt_bounce_ladder/alcove_cam{0,2}_b2_m17_*
)

function Capture {
    param([string]$cellName, [int]$dm, [int]$db, [int]$st, [int]$cam)
    $tag  = "alcove_cam${cam}_${cellName}_b2_mbon_m17"
    $base = "${tag}.png"
    Write-Host "[h3] $tag (dm=$dm db=$db st=$st, MB ON g=1.0, b=2)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--pt-max-bounces=2",
        "--use-multi-bounce=1",
        "--mb-gain=1.0",
        "--use-directional-merge=$dm",
        "--use-dir-bilinear=$db",
        "--use-spatial-trilinear=$st",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|use-directional|use-dir-bilinear|use-spatial|multi-bounce|pt-max-bounces|mb-gain|PT accumulators" |
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
Write-Host "===== v2.0 (h.3) MB-ON x merge factorial fill-in at b=2 ====="
foreach ($cell in $cells) {
    foreach ($cam in @(0, 2)) {
        Capture -cellName $cell[0] -dm $cell[1] -db $cell[2] -st $cell[3] -cam $cam
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
