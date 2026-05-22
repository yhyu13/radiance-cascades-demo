# MBRC v2.0-pre - (delta) spatial probe density discriminator sweep.
#
# Tests whether varying C0 probe-grid resolution shrinks the asymmetric
# cascade-vs-PT delta from report sec 12/13/14. (delta) is the last remaining
# named hypothesis after (gamma) rejected, (beta) demoted, (alpha) rejected.
#
# 4 N values x 2 cams x 2 modes = 16 captures.
# Default = N=32. Sweep N in {16, 32, 48, 64} (1 lower, baseline, 2 higher).
#
# Pre-committed verdict (B1 area discriminator, written BEFORE running):
#   STRONG_DELTA : any N reduces cam2 mode-19 Delta-area >=20% AND keeps cam0 within +/-10%
#   WEAK_DELTA   : any N reduces cam2 10-20% AND keeps cam0 within +/-10%; OR >=20% on one cam only
#   DELTA_REJECT : ALL N within +/-10% of N=32 on BOTH cams -> exit named-hypothesis tree
#   DELTA_LEVERAGE_WRONG_DIR : any N increases Delta-area >+10% on either cam (bidirectional)
#
# Hybrid OFF, MB OFF (clean baseline). All merge toggles ON (engine default,
# confirmed best in alpha sweep). --noise-seed-offset=0 (single seed; bug-230
# still open, gated on WEAK band). frames=512 = matches v2.0-pre baseline.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$outDir    = "tools/v20_pre_measurement/captures_delta"
$frames    = 512
$cameras   = @(0, 2)
$modes     = @(18, 19)
$nValues   = @(16, 32, 48, 64)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$n, [int]$cam, [int]$mode)
    $tag  = "cam${cam}_N$('{0:D2}' -f $n)_m$('{0:D2}' -f $mode)"
    $base = "${tag}.png"
    Write-Host "[capture] $tag (N=$n)"
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
        "--render-mode=$mode",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList 2>&1 |
        Select-String -Pattern "cascade-c0-res|render-mode|measurement-camera|screenshot saved|use-hybrid|use-multi-bounce" |
        ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
        Write-Host "  saved: $outDir/$base"
    } else {
        Write-Host "  WARN: no png produced for $tag"
    }
}

$total = $nValues.Count * $cameras.Count * $modes.Count
$i = 0
$tStart = Get-Date
foreach ($n in $nValues) {
    foreach ($cam in $cameras) {
        foreach ($mode in $modes) {
            $i++
            Write-Host "----- [$i/$total] -----"
            Capture -n $n -cam $cam -mode $mode
        }
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] delta probe-density sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
