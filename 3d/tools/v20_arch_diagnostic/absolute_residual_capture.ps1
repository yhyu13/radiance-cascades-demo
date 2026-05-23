# MBRC v2.0 absolute-residual analyzer -- HDR EXR capture step
#
# Per doc/7/v20_cam2_asymmetry_diagnostic_impl.md section 5: capture mode-17
# HDR EXR triplets (cascade_gi / pt_full / pt_direct) at cam0 + cam2 under
# the NEW engine defaults (commit d64ea17 onward: M4_iso_nearest +
# MB g=1.0), so the analyzer differences against the current-shipping
# configuration rather than the v2.0-pre baseline captures in
# captures_hdr_exr/ (which used --use-directional-merge=1 --use-dir-bilinear=1
# --use-multi-bounce=0).
#
# Only 2 captures (~30s) -- NEW-default mode 17, cam0 + cam2, cascadeC0Res=32
# (engine default), hybrid OFF, seed 0, 512 frames.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_abs_residual"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam)
    $tag  = "alcove_cam${cam}_newdefault_m17"
    $base = "${tag}.png"
    Write-Host "[capture] $tag"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|useDirectional|useDirBilinear|multi-bounce|PT accumulators" |
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
Write-Host "===== v2.0 absolute-residual HDR EXR capture ====="
Capture -cam 0
Capture -cam 2
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
