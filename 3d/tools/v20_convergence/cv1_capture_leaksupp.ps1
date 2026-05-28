# MBRC v2.0 CV1 — Leak-suppressed preset sweep (DM=1 + ST=1 + WS=1)
#
# Mirrors cv1_capture.ps1 but adds --use-directional-merge=1 +
# --use-spatial-trilinear=1 + --use-weighted-sample=1 so Phase 3
# (sampleUpperDirWeighted, radiance_3d.comp:667) actually activates.
#
# 2026-05-25: first run of this script (with ST+WS only, no DM) produced
# EXRs byte-identical to Default — the DM gate was the silent third
# requirement. Indicator at demo3d.cpp now enumerates all 4 flags.
#
# Expected: |p95| shrinks vs Default's 2.27 (leak gate works); ratio shifts —
# direction unknown (ST=1 dilution vs WS recovery race).

$exe    = "./build/RadianceCascades3D.exe"
$outDir = "tools/v20_convergence/captures_cv1_postfix_leaksupp"
$frameList = @(128, 256, 512, 1024, 2048)
$cam = 0
$camFile = "tools/v20_pre_measurement/cameras.json"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$N)
    $tag  = "cv1_cornell_cam${cam}_mbon_g100_hyb0_N$('{0:D4}' -f $N)_m17_leaksupp"
    $base = "${tag}.png"
    Write-Host "[cv1-LS] $tag (N=$N, DM=1, ST=1, WS=1)"
    $argList = @(
        "--load-obj=cornell",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=1.0",
        "--use-hybrid=0",
        "--use-directional-merge=1",
        "--use-spatial-trilinear=1",
        "--use-weighted-sample=1",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--use-probe-jitter=1",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$N",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "spatial-trilinear|weighted-sample|use-hybrid|multi-bounce|screenshot saved|hdr-exr" |
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

$tStart = Get-Date
Write-Host "===== Leak-suppressed CV1 (cornell, cam0, MB-ON g=1.0, hybrid OFF, ST=1, WS=1) ====="
foreach ($N in $frameList) { Capture -N $N }
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
