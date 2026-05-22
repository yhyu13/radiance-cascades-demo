# MBRC v2.0-pre HDR-EXR honest-metric sweep (doc/7/hdr_exr_metric_impl.md).
#
# Re-runs a SUBSET of the (delta) probe-density sweep with --screenshot-exr=1
# in render-mode=17, dumping cascade_gi / pt_full / pt_direct EXR sidecars.
# Goal: falsify "(delta) DELTA_REJECT verdict is consistent with LDR-PNG floor
# at ~20% by construction (colormap divisor=0.2 saturates HDR > 0.2)".
#
# N values: {16, 32, 64} (skip 48; 3 datapoints sufficient to discriminate).
# Cameras: {0, 2}. Output: 6 captures x 4 files = 24 files (~10 MB total).
# Hybrid OFF, MB OFF (clean baseline). All merge toggles ON (default).
# noise-seed-offset=0. frames=512 = matches v2.0-pre baseline.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$outDir    = "tools/v20_pre_measurement/captures_hdr_exr"
$frames    = 512
$cameras   = @(0, 2)
$nValues   = @(16, 32, 64)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$n, [int]$cam)
    $tag  = "cam${cam}_N$('{0:D2}' -f $n)_m17"
    $base = "${tag}.png"
    Write-Host "[capture] $tag (N=$n cam=$cam)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--cascade-c0-res=$n",
        "--use-multi-bounce=0",
        "--use-hybrid=0",
        "--use-directional-merge=1",
        "--use-dir-bilinear=1",
        "--use-spatial-trilinear=1",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|Phase7|PT accumulators" |
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

$total = $nValues.Count * $cameras.Count
$i = 0
$tStart = Get-Date
foreach ($n in $nValues) {
    foreach ($cam in $cameras) {
        $i++
        Write-Host "----- [$i/$total] -----"
        Capture -n $n -cam $cam
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] hdr-exr sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
