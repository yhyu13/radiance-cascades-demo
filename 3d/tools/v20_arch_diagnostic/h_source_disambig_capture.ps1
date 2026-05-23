# MBRC v2.0 (h) source disambiguation -- HDR EXR capture step
#
# Per doc/7/v20_pt_bounce_ladder_impl.md section 6: the bounce-ladder
# falsified (f) energy-loss and revealed (h) single-bounce asymmetry --
# cam0 cascade is +39% OVER-bright vs PT_b=2. Two candidate sources:
#
#   (h.1) MB feedback at g=1.0 super-additivity (alpha_m4_deepdive_impl.md
#         section 4.2 measured M4 x MB +16.8% super-additive on cam0).
#   (h.2) First-bounce 3-way merge formula at radiance_3d.comp:656-682 is
#         over-integrating atlas radiance regardless of MB.
#
# This script captures cascade-WITHOUT-MB-feedback at b=2 on cam0+cam2
# via --use-multi-bounce=0 (other engine defaults unchanged: M4 merge,
# trilinear off, dir-bilinear off). MB-ON captures at b=2 already exist
# under captures_pt_bounce_ladder/. Analyzer compares the two:
#
#   - If MB-OFF cam0 cascade/PT ratio at b=2 drops to ~1.0 (symmetric),
#     MB feedback IS the +39% over-source -> target is the (beta) g=2.0
#     runaway numerical-instability root-cause (h.1 confirmed).
#   - If MB-OFF cam0 cascade/PT ratio is already ~1.4 (still +39% over),
#     the first-bounce merge formula itself is over-integrating
#     (h.2 confirmed) -> target is radiance_3d.comp:656-682.
#
# 2 new captures (~30s); reuses 2 captures from bounce-ladder. Total 4.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h_disambig"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam)
    $tag  = "alcove_cam${cam}_b2_mboff_m17"
    $base = "${tag}.png"
    Write-Host "[capture] $tag (cam=$cam, pt-max-bounces=2, use-multi-bounce=0)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--pt-max-bounces=2",
        "--use-multi-bounce=0",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|multi-bounce|pt-max-bounces|PT accumulators" |
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
Write-Host "===== v2.0 (h) source disambig (MB-OFF at b=2) capture ====="
Capture -cam 0
Capture -cam 2
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
