param(
    [ValidateSet("sponza", "cornell")]
    [string]$Scene = "sponza",
    [double]$Gain = 0.5,
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
# Slug-friendly gain tag: 0.50 -> g050, 1.00 -> g100, 0.05 -> g005
$gainTag = "g{0:D3}" -f [int]([math]::Round($Gain * 100))
$outDir = "tools/v3_m1_mb_gain_ladder/captures_${Scene}_${gainTag}"
$tag = "m1stage9_${Scene}_${gainTag}_N$('{0:D4}' -f $N)_m17"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

# SC1: always use the gain slider path (--use-multi-bounce=1 --multi-bounce-gain=$Gain)
# so the ladder is internally consistent. The Stage 8 mb_off datapoint (which used
# --use-multi-bounce=0) is verified against gain=0.00 in the implementation.
$argList = @(
    "--load-obj=$Scene",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=$Gain",
    "--use-hybrid=0",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=1",
    "--use-directional-gi=1",
    "--m1-delta3-gated-trilinear=0",
    "--render-mode=17",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--probe-stats-json=$outDir/${tag}_probe_stats.json",
    "--screenshot=$tag.png"
)

Write-Host "[m1-stage9] $tag"
if ($DryRun) {
    Write-Host "  $exe $($argList -join ' ')"
    exit 0
}

& $exe @argList |
    Select-String -Pattern "measurement-camera|use-hybrid|multi-bounce-gain|hdr-exr|probe-stats|screenshot saved" |
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
