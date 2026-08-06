param(
    [ValidateSet("sponza", "cornell")]
    [string]$Scene = "sponza",
    [ValidateSet("baseline", "mb_off", "mb_gain_half", "jitter_off", "delta3_on", "hybrid_on")]
    [string]$Variant = "baseline",
    [int]$N = 2048,
    [switch]$DryRun
)

$exe = "./build/RadianceCascades3D.exe"
$cam = 0
$camFile = if ($Scene -eq "sponza") {
    "tools/v20_pre_measurement/sponza_cam.json"
} else {
    "tools/v20_pre_measurement/cameras.json"
}
$outDir = "tools/v3_m1_source_energy_ab/captures_${Scene}_${Variant}"
$tag = "m1stage8_${Scene}_${Variant}_N$('{0:D4}' -f $N)_m17"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

# Stage 7 baseline flags
$useMB = 1
$mbGain = 1.0
$useHybrid = 0
$useJitter = 1
$delta3 = 0

switch ($Variant) {
    "baseline"      { }
    "mb_off"        { $useMB = 0 }
    "mb_gain_half"  { $mbGain = 0.5 }
    "jitter_off"    { $useJitter = 0 }
    "delta3_on"     { $delta3 = 1 }
    "hybrid_on"     { $useHybrid = 1 }
}

$argList = @(
    "--load-obj=$Scene",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=$useMB",
    "--multi-bounce-gain=$mbGain",
    "--use-hybrid=$useHybrid",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=$useJitter",
    "--use-directional-gi=1",
    "--m1-delta3-gated-trilinear=$delta3",
    "--render-mode=17",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--probe-stats-json=$outDir/${tag}_probe_stats.json",
    "--screenshot=$tag.png"
)

Write-Host "[m1-stage8] $tag"
if ($DryRun) {
    Write-Host "  $exe $($argList -join ' ')"
    exit 0
}

& $exe @argList |
    Select-String -Pattern "measurement-camera|use-hybrid|useDirectionalGI|use-multi-bounce|multi-bounce-gain|use-probe-jitter|m1-delta3|hdr-exr|probe-stats|screenshot saved" |
    ForEach-Object { Write-Host "  $_" }

foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr", "_gbuffer.exr", "_probe_diag.exr", "_probe_contrib.exr", "_probe_bin.exr")) {
    $src = "${tag}${suffix}"
    if (Test-Path $src) {
        Move-Item -Force $src "$outDir/$src"
        Write-Host "  moved: $src"
    } else {
        Write-Host "  WARN: missing $src"
    }
}

if (Test-Path "$outDir/${tag}_probe_stats.json") {
    Write-Host "  kept: ${tag}_probe_stats.json"
} else {
    Write-Host "  WARN: missing ${tag}_probe_stats.json"
}
