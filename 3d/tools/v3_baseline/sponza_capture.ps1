param(
    [int[]]$FrameList = @(128, 256, 512, 1024, 2048),
    [ValidateSet(0, 1)]
    [int]$UseHybrid = 0,
    [switch]$DryRun
)

# v3 M0 Stage 1 - Sponza default-flag baseline capture.
#
# Forked from tools/v20_convergence/cv1_capture_leaksupp.ps1 only as a process
# template. This script intentionally does NOT force:
#   --use-directional-merge=1
#   --use-spatial-trilinear=1
#   --use-weighted-sample=1
# Engine defaults govern, matching the Cornell cv1_postfix baseline semantics.

$exe     = "./build/RadianceCascades3D.exe"
$cam     = 0
$camFile = "tools/v20_pre_measurement/sponza_cam.json"
$outDir  = if ($UseHybrid -eq 1) {
    "tools/v3_baseline/captures_sponza_hybon"
} else {
    "tools/v3_baseline/captures_sponza_default"
}

if (-not (Test-Path $exe)) {
    throw "Missing executable: $exe"
}
if (-not (Test-Path $camFile)) {
    throw "Missing Sponza camera file: $camFile"
}
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

function Invoke-Capture {
    param([int]$N)

    $tag  = "v3base_sponza_cam${cam}_mbon_g100_hyb${UseHybrid}_N$('{0:D4}' -f $N)_m17"
    $base = "${tag}.png"
    $argList = @(
        "--load-obj=sponza",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=1.0",
        "--use-hybrid=$UseHybrid",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--use-probe-jitter=1",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$N",
        "--screenshot=$base"
    )

    Write-Host "[v3-sponza] $tag (N=$N, hybrid=$UseHybrid, default flags)"
    if ($DryRun) {
        Write-Host "  $exe $($argList -join ' ')"
        return
    }

    & $exe @argList |
        Select-String -Pattern "measurement-camera|use-hybrid|multi-bounce|screenshot saved|hdr-exr|render-mode" |
        ForEach-Object { Write-Host "  $_" }

    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
            Write-Host "  moved: $src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

$tStart = Get-Date
Write-Host "===== Sponza v3 baseline (hybrid=$UseHybrid, default flags, mode 17) ====="
foreach ($N in $FrameList) {
    Invoke-Capture -N $N
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture script in $($elapsed.TotalMinutes.ToString('F1')) min"
