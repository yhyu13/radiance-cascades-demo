param(
    [ValidateSet("mid_0p8_x4", "far_5", "far_25", "near_baseline_reverify")]
    [string]$Variant = "mid_0p8_x4",
    [int]$N = 2048,
    [switch]$DryRun
)

$exe = "./build/RadianceCascades3D.exe"
$camFile = "tools/v20_pre_measurement/cameras.json"
$outDir = "tools/v3_m1_cornell_light_distance/captures_$Variant"
$tag = "m1stage11d_${Variant}_N$('{0:D4}' -f $N)_m17"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not $DryRun -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# All cornell, all gain=1.0, hybrid=0, mode 17, N=2048.
# Variant differs only in light position.
$lightFlag = "--light-position=0,0.8,0"
switch ($Variant) {
    "near_baseline_reverify" { $lightFlag = "--light-position=0,0.8,0" }
    "mid_0p8_x4"             { $lightFlag = "--light-position=0,3.2,0" }
    "far_5"                  { $lightFlag = "--light-position=0,5.0,0" }
    "far_25"                 { $lightFlag = "--light-position=0,25.0,0" }
}

$argList = @(
    "--load-obj=cornell",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=0",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=1.0",
    "--use-hybrid=0",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=1",
    "--use-directional-gi=1",
    "--m1-delta3-gated-trilinear=0",
    $lightFlag,
    "--render-mode=17",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--screenshot=$tag.png"
)

if ($DryRun) { Write-Host "$exe $($argList -join ' ')"; exit 0 }

$logFile = "$outDir/$tag.log"
Write-Host "[m1-stage11d] $tag -> $logFile"
& $exe @argList *> $logFile
Write-Host "[m1-stage11d] $tag exit=$LASTEXITCODE"

foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr", "_gbuffer.exr", "_probe_diag.exr", "_probe_contrib.exr", "_probe_bin.exr")) {
    $src = "${tag}${suffix}"
    if (Test-Path $src) { Move-Item -Force $src "$outDir/$src"; Write-Host "  moved: $src" }
    else { Write-Host "  WARN: missing $src" }
}
