# MBRC v2.0 P2 — D-sweep: capture mode 22 at D=8 and D=16 on cornell-orig-alcove.
#
# Context: D=4 (engine default) measurement gave histogram overlap=0.6617
# (P2_OVERLAP_MEDIUM) and per-row JS=0.1310 (mass-weighted across rows). The
# load-bearing question: does the dx-collapse on cam2 (54.9% top-1 share vs
# cam0's 31.4% at the same row) PERSIST, SHARPEN, or DISSOLVE as D grows?
#
#   - DISSOLVE  -> the asymmetry is a discretization artifact of D=4 binning
#                  (cam2's surface samples hit a narrow azimuthal range that
#                  D=4 quantizes into a single bin); the architectural fix
#                  is "raise D" (one engine default change). Pre-committed
#                  band: per-row weighted JS < 0.05 at D=16 (~2.6x reduction).
#
#   - PERSIST   -> the asymmetry is genuine atlas content (cam2's probes
#                  have a narrow set of bright bins regardless of D); fix
#                  work is on the bake-gather chain. Per-row JS stays in
#                  [0.05, 0.15] at D=16 (within 60% of D=4 value).
#
#   - SHARPEN   -> increasing D exposes a narrower asymmetry hidden by D=4
#                  bin coarseness; bake-side fix work is targeted to a
#                  specific direction sub-band. Per-row JS > 0.15 at D=16.
#
# CLI: engine flag --cascade-dir-res=N already supports N in [2, 32] even
# (src/main3d.cpp:660). No engine work needed for D-sweep.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 256
$outDir = "tools/v20_arch_diagnostic/captures_p2_dombin_dsweep"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam, [int]$D)
    $tag  = "alcove_cam${cam}_M0_b2_mboff_dombin_m22_D${D}"
    $base = "${tag}.png"
    Write-Host "[p2-dsweep] $tag (mode 22, M0, MB OFF, b=2, D=$D, ST=0 default)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--cascade-dir-res=$D",
        "--use-hybrid=0",
        "--use-multi-bounce=0",
        "--blend-mode=0",
        "--noise-seed-offset=0",
        "--render-mode=22",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|render mode|cascade-dir-res|use-multi-bounce" |
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
Write-Host "===== v2.0 P2 D-sweep capture (D=8, D=16, cam0+cam2) ====="
foreach ($D in @(8, 16)) {
    foreach ($cam in @(0, 2)) {
        Capture -cam $cam -D $D
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"

Write-Host ""
Write-Host "===== Analysis (per D) ====="
foreach ($D in @(8, 16)) {
    $cam0 = "$outDir/alcove_cam0_M0_b2_mboff_dombin_m22_D${D}_dombin.exr"
    $cam2 = "$outDir/alcove_cam2_M0_b2_mboff_dombin_m22_D${D}_dombin.exr"
    $json = "$outDir/p2_dombin_D${D}_results.json"
    Write-Host ""
    Write-Host "--- D=$D ---"
    python tools/v20_arch_diagnostic/analyze_p2_dombin.py $cam0 $cam2 $json
}
