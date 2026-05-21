# Diagnostic sweep for RC problem identification (cam0, ~128 warmup frames)
# Captures: composite, cascade-vis, GI-delta, combined-delta, error-decomp sub-modes 0-3,
#           leave-one-out modes 18 with cascade-exclude -1..3
# Workaround for bug-211: raylib strips path, capture basename, then Move-Item.

$exe = "./build/RadianceCascades3D.exe"
$camFile = "tools/v20_pre_measurement/cameras.json"
$outDir = "tools/v20_pre_measurement/captures"
$frames = 192   # extra warmup for PT cache + temporal accum
$cam = 0
$tag = "cam0"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([string]$label, [string]$extraArgs)
    $base = "${tag}_${label}.png"
    Write-Host "[capture] $label ($extraArgs)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    ) + ($extraArgs -split ' ' | Where-Object { $_ })
    & $exe @argList 2>&1 | Select-String -Pattern "render-mode|error-decomp|cascade-exclude|measurement-camera|screenshot saved" | ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
        Write-Host "  saved: $outDir/$base"
    } else {
        Write-Host "  WARN: no png produced"
    }
}

# A) Final composite + diagnostic modes (default cascade chain)
Capture "mode00_composite"    "--render-mode=0"
Capture "mode01_cascadevis"   "--render-mode=1"
Capture "mode18_combined"     "--render-mode=18"
Capture "mode19_gi_delta"     "--render-mode=19"
Capture "mode20_total"        "--render-mode=20 --error-decomp-mode=0"
Capture "mode20_direct"       "--render-mode=20 --error-decomp-mode=1"
Capture "mode20_indirect"     "--render-mode=20 --error-decomp-mode=2"
Capture "mode20_relative"     "--render-mode=20 --error-decomp-mode=3"
Capture "mode21_dominance"    "--render-mode=21"

# B) PT reference for visual sanity (mode 16)
Capture "mode16_pt_ref"       "--render-mode=16"

# C) Leave-one-out attribution on combined delta (mode 18)
Capture "mode18_loo_c0"       "--render-mode=18 --cascade-exclude=0"
Capture "mode18_loo_c1"       "--render-mode=18 --cascade-exclude=1"
Capture "mode18_loo_c2"       "--render-mode=18 --cascade-exclude=2"
Capture "mode18_loo_c3"       "--render-mode=18 --cascade-exclude=3"

Write-Host "[done] sweep complete"
