# v2.5 Axis A — per-cascade contribution isolation sweep
#
# Captures cascade-only-{C0, C0..C1, C0..C2, C0..C3} at cornell/cam0/N=2048/mode-17.
# All other parameters mirror cv1_capture.ps1 + v24b so the analyzer can diff.

$exe     = "./build/RadianceCascades3D.exe"
$outDir  = "tools/v25_axisA/captures"
$cam     = 0
$camFile = "tools/v20_pre_measurement/cameras.json"
$N       = 2048
$levels  = @(0, 1, 2, 3)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

foreach ($L in $levels) {
    $tag  = "v25A_cornell_cam${cam}_mbon_g100_hyb0_N$('{0:D4}' -f $N)_m17_maxL$L"
    $base = "${tag}.png"

    Write-Host "===== v2.5/A per-level capture (cornell, cam0, MB-ON, hybrid OFF, maxLevel=$L) ====="
    Write-Host "[v25A] $tag (N=$N frames)"

    $argList = @(
        "--load-obj=cornell",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=1.0",
        "--use-hybrid=0",
        "--max-cascade-level=$L",
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
        Select-String -Pattern "hdr-exr|screenshot saved|render mode|load-obj|multi-bounce|use-hybrid|v2.5/A|maxCascadeLevel" |
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
    Write-Host "[done L=$L] in $($elapsed.TotalSeconds.ToString('F1')) s"
    Write-Host ""
}

Write-Host "Next: python tools/v25_axisA/analyze_per_level.py"
