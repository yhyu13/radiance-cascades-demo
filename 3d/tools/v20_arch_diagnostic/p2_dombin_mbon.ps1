# MBRC v2.0 P2-D — Per-Pixel Dominant Direction Bin Viz at MB-ON
#
# Context: P2 (MB-OFF baseline) verdict P2_OVERLAP_MEDIUM with cam2's dx
# collapse at (0,1) (54.9% top-1 share) confirmed bake-side per-bin atlas
# asymmetry by direct measurement. P2-B D-sweep verdict P2_DSWEEP_SHARPEN
# killed the "raise D" fix candidate (D=16 INTENSIFIES the asymmetry; per-row
# JS 0.131 -> 0.176). The D step asks: does multi-bounce temporal feedback
# REDISTRIBUTE per-bin atlas content enough to close the gap, OR is the
# asymmetry preserved under MB?
#
# If MB closes the gap materially:
#   - MB-ON is the cheapest cure (no architectural work needed beyond keeping
#     MB-ON default, which is already the post-(alpha)-flip state).
#   - Option A (bake-side fix prototype) becomes lower priority.
#
# If MB preserves the asymmetry:
#   - Bake-side single-bounce per-bin content is the bottleneck regardless
#     of temporal feedback. Option A proceeds under MB-ON.
#   - This is the more likely outcome given the (h.3) MB factorial finding
#     (MB delivers ~2-2.4x brightness multiplier across merge modes but does
#     NOT close cam0/cam2 spread — cam2 stays around 0.33 multiplier-relative
#     while cam0 climbs).
#
# Pre-committed verdict bands (per-row weighted JS at D=4 vs MB-OFF baseline
# 0.1310):
#
#   MB_CURES   -> per-row JS <= 0.05 (MB redistributes per-bin atlas content
#                 enough to drop the per-row asymmetry below the original
#                 SHARPEN/DISSOLVE threshold). MB-ON is the architectural fix.
#   MB_REDUCES -> per-row JS in (0.05, 0.10] (MB closes >25% of the
#                 asymmetry; bake-side fix gets less leverage but still helps).
#   MB_PRESERVES -> per-row JS in (0.10, 0.16] (MB has little effect on the
#                 per-bin distribution; cam2 still collapses).
#   MB_AMPLIFIES -> per-row JS > 0.16 (MB increases the asymmetry; temporal
#                 feedback is consolidating cam2's narrow bins by re-injecting
#                 their content).
#
# Capture cost: ~0.5 min (2 cells x ~15s including 256 frames for cascade
# settlement + MB equilibrium).

$exe    = "./build/RadianceCascades3D.exe"
$frames = 256
$outDir = "tools/v20_arch_diagnostic/captures_p2_dombin_mbon"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam)
    $tag  = "alcove_cam${cam}_M0_b2_mbon_dombin_m22"
    $base = "${tag}.png"
    Write-Host "[p2-mbon] $tag (mode 22, M0, MB ON gain=1.0, b=2, D=4 default)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=1.0",
        # bug-234 mitigation: measurement-camera mode pins jitter -> no rebake
        # trigger -> MB feedback never fires. Force jitter ON so the cascade
        # rebakes per frame and MB temporal feedback accumulates into the atlas.
        "--use-probe-jitter=1",
        "--blend-mode=0",
        "--noise-seed-offset=0",
        "--render-mode=22",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|render mode|use-multi-bounce|multi-bounce-gain|use-spatial" |
        ForEach-Object { Write-Host "  $_" }
    foreach ($suffix in @(".png", "_dombin.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

$tStart = Get-Date
Write-Host "===== v2.0 P2-D MB-ON capture (cam0, cam2) ====="
foreach ($cam in @(0, 2)) {
    Capture -cam $cam
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"

Write-Host ""
Write-Host "===== Analysis (MB-ON vs MB-OFF baseline) ====="
$cam0 = "$outDir/alcove_cam0_M0_b2_mbon_dombin_m22_dombin.exr"
$cam2 = "$outDir/alcove_cam2_M0_b2_mbon_dombin_m22_dombin.exr"
$json = "$outDir/p2_dombin_mbon_results.json"
python tools/v20_arch_diagnostic/analyze_p2_dombin.py $cam0 $cam2 $json
