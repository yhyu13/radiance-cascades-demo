# v3 M0 Stage 1 — Cornell hybrid-ON baseline capture
#
# Single capture (N=2048) at cornell/cam0/MB-ON g=1.0/HYBRID-ON/mode-17.
# Mirrors the existing cornell cascade-OFF baseline at
# tools/v20_convergence/captures_cv1_postfix/cv1_cornell_cam0_mbon_g100_hyb0_N2048_m17_postfix*
# with --use-hybrid=1 flipped on. All other flags match (Default Path A semantics —
# leak-suppression flags DM/ST/WS NOT forced on; engine defaults govern).

$exe    = "./build/RadianceCascades3D.exe"
$outDir = "tools/v3_baseline/captures_cornell_hybon"
$N      = 2048
$cam    = 0
$camFile = "tools/v20_pre_measurement/cameras.json"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

$tag  = "v3base_cornell_cam${cam}_mbon_g100_hyb1_N$('{0:D4}' -f $N)_m17"
$base = "${tag}.png"

Write-Host "===== Cornell hybrid-ON baseline (N=$N, --use-hybrid=1) ====="
Write-Host "[capture] $tag"

$argList = @(
    "--load-obj=cornell",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=1.0",
    "--use-hybrid=1",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=1",
    "--render-mode=17",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--screenshot=$base"
)

$tStart = Get-Date
& $exe @argList |
    Select-String -Pattern "use-hybrid|multi-bounce|screenshot saved|hdr-exr|render-mode" |
    ForEach-Object { Write-Host "  $_" }
$elapsed = (Get-Date) - $tStart

foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
    $src = "${tag}${suffix}"
    if (Test-Path $src) {
        Move-Item -Force $src "$outDir/$src"
        Write-Host "  moved: $src"
    } else {
        Write-Host "  WARN: missing $src"
    }
}

Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
