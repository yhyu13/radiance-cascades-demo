# MBRC v2.0-pre - (beta) MB-gain discriminator sweep.
#
# Tests whether raising multiBounceGain shrinks the asymmetric cascade-vs-PT
# delta found in the full sweep (doc/7/mbrc_v20_pre_measurement_report.md
# section 3.5) and the cascade-config sweep (section 12).
#
# Hypothesis (beta) predicts: gain=2.0 reduces mode-19 Delta-band area by
# >=30% on BOTH cam0 AND cam2 (rule B1, pre-committed in cascade_config_sweep_impl.md
# section 8.1). If <=10%, (beta) is REJECTED.
#
# Matrix: 5 gains x 2 cameras x 2 modes = 20 captures.
#   multiBounceGain in {0.5, 1.0, 1.5, 2.0, 3.0}
#   cam in {0, 2}
#   mode in {18 (combined Delta), 19 (GI-only Delta)}
#   --use-multi-bounce=1  (REQUIRED -- MB feedback is OFF by default;
#                          gain slider is dead without it)
#   --use-hybrid=0        (clean baseline; hybrid + MB gain BOTH add energy,
#                          confounded otherwise; see hybrid section 3.5 finding)
#   --cascade-scaled-dir-res=1  (engine default; matches full sweep section 3.5
#                                 numbers; NOT uniform-D from cascade-config sweep)
#   seed=0 (single; bug-230 still open -- promoted to mandatory if B1 lands
#           in WEAK band per impl doc section 8.2)
#   frames=512 (matches v2.0-pre baseline)
#
# bug-211 workaround: raylib TakeScreenshot strips path. Capture to basename,
# Move-Item to outDir.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$outDir    = "tools/v20_pre_measurement/captures_mb"
$frames    = 512
$gains     = @(0.5, 1.0, 1.5, 2.0, 3.0)
$cameras   = @(0, 2)
$modes     = @(18, 19)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam, [double]$gain, [int]$mode)
    # Filename gain encoding: 0.5 -> "g050", 1.0 -> "g100", 2.0 -> "g200", 3.0 -> "g300".
    $gainTag = "g$('{0:D3}' -f [int]($gain * 100))"
    $tag  = "cam${cam}_${gainTag}_m$('{0:D2}' -f $mode)"
    $base = "${tag}.png"
    Write-Host "[capture] $tag (gain=$gain)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=$gain",
        "--use-hybrid=0",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--render-mode=$mode",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList 2>&1 |
        Select-String -Pattern "multi-bounce|MultiBounce|Phase MB|render-mode|measurement-camera|screenshot saved|use-hybrid" |
        ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
        Write-Host "  saved: $outDir/$base"
    } else {
        Write-Host "  WARN: no png produced for $tag"
    }
}

$total = $gains.Count * $cameras.Count * $modes.Count
$i = 0
$tStart = Get-Date
foreach ($gain in $gains) {
    foreach ($cam in $cameras) {
        foreach ($mode in $modes) {
            $i++
            Write-Host "----- [$i/$total] -----"
            Capture -cam $cam -gain $gain -mode $mode
        }
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] MB-gain sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
