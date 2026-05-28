# v2.4 — C0 directional-resolution bump A/B capture
#
# Baseline: existing tools/v20_convergence/captures_cv1_postfix/N2048 (D=8 default)
# Variant:  this script captures D=16 at C0 (--cascade-dir-res=16)
#
# Other parameters mirror cv1_capture.ps1 (cornell, cam0, MB-ON g=1.0, hybrid OFF, N=2048)
# so the analyzer can directly diff the two captures.

$exe     = "./build/RadianceCascades3D.exe"
$outDir  = "tools/v24_c0_hd/captures"
$cam     = 0
$camFile = "tools/v20_pre_measurement/cameras.json"
$N       = 2048

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

$tag  = "v24_cornell_cam${cam}_mbon_g100_hyb0_N$('{0:D4}' -f $N)_m17_c0d16"
$base = "${tag}.png"

Write-Host "===== v2.4 C0 dirRes bump capture (cornell, cam0, MB-ON g=1.0, hybrid OFF, mode 17, C0 D=16) ====="
Write-Host "[v24] $tag (N=$N frames, --cascade-dir-res=16)"

$argList = @(
    "--load-obj=cornell",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=1.0",
    "--use-hybrid=0",
    "--cascade-dir-res=16",
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
    Select-String -Pattern "hdr-exr|screenshot saved|render mode|load-obj|multi-bounce|use-hybrid|Cascade 0|Cascade 1|Cascade 2|Cascade 3|dirRes|atlas" |
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
Write-Host "Next: python tools/v24_c0_hd/analyze_v24.py"
