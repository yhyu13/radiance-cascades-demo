# v2.4.b — Per-pixel indirect symptom clamp A/B capture
#
# Baseline: existing tools/v20_convergence/captures_cv1_postfix/N2048 (K=0 default)
# Variant:  this script captures K=2 (--indirect-clamp-k=2) at cornell/cam0/N=2048
#
# Other parameters mirror cv1_capture.ps1 so the analyzer can directly diff.

$exe     = "./build/RadianceCascades3D.exe"
$outDir  = "tools/v24b_clamp/captures"
$cam     = 0
$camFile = "tools/v20_pre_measurement/cameras.json"
$N       = 2048
$K       = 2

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

$tag  = "v24b_cornell_cam${cam}_mbon_g100_hyb0_N$('{0:D4}' -f $N)_m17_clampK$K"
$base = "${tag}.png"

Write-Host "===== v2.4.b indirect clamp capture (cornell, cam0, MB-ON g=1.0, hybrid OFF, mode 17, K=$K) ====="
Write-Host "[v24b] $tag (N=$N frames, --indirect-clamp-k=$K)"

$argList = @(
    "--load-obj=cornell",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=1.0",
    "--use-hybrid=0",
    "--indirect-clamp-k=$K",
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
    Select-String -Pattern "hdr-exr|screenshot saved|render mode|load-obj|multi-bounce|use-hybrid|v2.4.b|IndirectClamp" |
    ForEach-Object { Write-Host "  $_" }
$elapsed = (Get-Date) - $tStart

foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
    $src = "${tag}${suffix}"
    if (Test-Path $src) {
        Move-Item -Force $src "$outDir/$src"
        Write-Host "  moved $src -> $outDir/"
    } else {
        Write-Host "  WARN: missing $src"
    }
}

Write-Host "[done] capture in $($elapsed.TotalSeconds.ToString('F1')) s"
Write-Host ""
Write-Host "Next: python tools/v24b_clamp/analyze_v24b.py"
