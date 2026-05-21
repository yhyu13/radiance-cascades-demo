# MBRC v2.0-pre — Full measurement sweep (chunks 1+2 of report §6).
# 3 cameras × 3 seeds × 2 hybrid states (off + on) × 4 render modes = 72 captures.
# At 512-frame PT warmup, expect ~25-40 min total.
#
# Render mode subset (vs the 14-capture scouting sweep):
#   mode 00 = composite                       — sanity / regression baseline
#   mode 16 = PT reference                    — visual ground truth at this seed
#   mode 18 = combined Δ (cascade − PT)       — total error signed heatmap
#   mode 19 = GI-only Δ                       — isolates the GI component
# (modes 1, 20×4, 21, LOO c0..c3 dropped — already adequately exercised in scouting.)
#
# bug-211 workaround: raylib TakeScreenshot strips path. Capture basename, Move-Item to outDir.
# bug-227 (fixed): mode 20 PT dispatch — not used here anyway, but the gate is correct now.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$outDir    = "tools/v20_pre_measurement/captures_full"
$frames    = 512        # PT spp budget per capture (≈1 ray/pixel/frame in headless)
$cameras   = @(0, 1, 2)
$seeds     = @(0, 1, 2)
$hybrids   = @(0, 1)
$modes     = @(0, 16, 18, 19)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param(
        [int]$cam, [int]$seed, [int]$hybrid, [int]$mode
    )
    $tag  = "cam${cam}_s${seed}_h${hybrid}_m$('{0:D2}' -f $mode)"
    $base = "${tag}.png"
    Write-Host "[capture] $tag"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--noise-seed-offset=$seed",
        "--use-hybrid=$hybrid",
        "--render-mode=$mode",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList 2>&1 |
        Select-String -Pattern "render-mode|measurement-camera|noise-seed-offset|use-hybrid|screenshot saved" |
        ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
        Write-Host "  saved: $outDir/$base"
    } else {
        Write-Host "  WARN: no png produced for $tag"
    }
}

$total = $cameras.Count * $seeds.Count * $hybrids.Count * $modes.Count
$i = 0
$tStart = Get-Date
foreach ($cam in $cameras) {
    foreach ($seed in $seeds) {
        foreach ($hybrid in $hybrids) {
            foreach ($mode in $modes) {
                $i++
                Write-Host "----- [$i/$total] -----"
                Capture -cam $cam -seed $seed -hybrid $hybrid -mode $mode
            }
        }
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] full sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
