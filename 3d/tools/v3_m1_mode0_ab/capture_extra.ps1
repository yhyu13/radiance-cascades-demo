param(
    [ValidateSet("cornell_g010", "cornell_hybrid")]
    [string]$Variant = "cornell_g010",
    [int]$N = 2048,
    [switch]$DryRun
)

$exe = "./build/RadianceCascades3D.exe"
$camFile = "tools/v20_pre_measurement/cameras.json"
$outDir = "tools/v3_m1_mode0_ab/captures_$Variant"
$tag = "m1stage10_${Variant}_N$('{0:D4}' -f $N)_m17"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

$useHybrid = 0
$gain = 1.0
switch ($Variant) {
    "cornell_g010"   { $gain = 0.10; $useHybrid = 0 }
    "cornell_hybrid" { $gain = 1.00; $useHybrid = 1 }
}

$argList = @(
    "--load-obj=cornell",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=0",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=$gain",
    "--use-hybrid=$useHybrid",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=1",
    "--use-directional-gi=1",
    "--m1-delta3-gated-trilinear=0",
    "--render-mode=17",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--screenshot=$tag.png"
)

if ($DryRun) {
    Write-Host "$exe $($argList -join ' ')"
    exit 0
}

# SC9: redirect stdout to a log file (no Select-String pipe) so the demo's
# per-frame writes don't block on a slow consumer.
$logFile = "$outDir/$tag.log"
Write-Host "[m1-stage10] $tag -> $logFile"
& $exe @argList *> $logFile
$ec = $LASTEXITCODE
Write-Host "[m1-stage10] $tag exit=$ec"

foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr", "_gbuffer.exr", "_probe_diag.exr", "_probe_contrib.exr", "_probe_bin.exr")) {
    $src = "${tag}${suffix}"
    if (Test-Path $src) {
        Move-Item -Force $src "$outDir/$src"
        Write-Host "  moved: $src"
    } else {
        Write-Host "  WARN: missing $src"
    }
}
