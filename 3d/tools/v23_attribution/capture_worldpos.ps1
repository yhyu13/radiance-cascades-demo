# v2.3 Step 0 — First-hit world-position capture for leak-source probe attribution
#
# Captures a single mode-23 EXR at cornell/cam0 N=2048 (matches the v22
# bright-mask basis). The EXR stores per-pixel first-hit world position;
# precondition.py bins those positions by C0 probe-cell index and builds a
# Lorenz curve of bright-pixel mass per cell.
#
# Pre-committed gate (see doc/7/v23_leak_attribution_impl.md Step 0):
#   STRONG    top-5% probes cover ≥ 40% of bright pixels  -> proceed to Step 1
#   MARGINAL  top-5% in [20%, 40%)                         -> diagnostic-only
#   DEAD      top-5% < 20%                                 -> skip to v2.4

$exe     = "./build/RadianceCascades3D.exe"
$outDir  = "tools/v23_attribution/captures"
$cam     = 0
$camFile = "tools/v20_pre_measurement/cameras.json"
$N       = 2048

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

$tag  = "v23_cornell_cam${cam}_mbon_g100_hyb0_N$('{0:D4}' -f $N)_m23"
$base = "${tag}.png"

Write-Host "===== v2.3 Step 0 worldpos capture (cornell, cam0, MB-ON g=1.0, hybrid OFF, mode 23) ====="
Write-Host "[v23] $tag (N=$N frames)"

$argList = @(
    "--load-obj=cornell",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=1.0",
    "--use-hybrid=0",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=1",
    "--render-mode=23",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--screenshot=$base"
)

$tStart = Get-Date
& $exe @argList |
    Select-String -Pattern "hdr-exr|screenshot saved|render mode|load-obj|multi-bounce|use-hybrid|atlas|grid|probe" |
    ForEach-Object { Write-Host "  $_" }
$elapsed = (Get-Date) - $tStart

foreach ($suffix in @(".png", "_worldpos.exr")) {
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
Write-Host "Next: python tools/v23_attribution/precondition.py"
