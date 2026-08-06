param(
    [ValidateSet("sponza", "cornell")]
    [string]$Scene = "sponza",
    [int]$N = 2048,
    [string]$Cells = "7,5,4|6,5,4|6,4,4",
    [switch]$DryRun
)

$exe = "./build/RadianceCascades3D.exe"
$cam = 0
$camFile = if ($Scene -eq "sponza") {
    "tools/v20_pre_measurement/sponza_cam.json"
} else {
    "tools/v20_pre_measurement/cameras.json"
}
$outDir = "tools/v3_m1_atlas_attribution/captures_$Scene"
$tag = "m1atlas_${Scene}_baseline_N$('{0:D4}' -f $N)_m17"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

$argList = @(
    "--load-obj=$Scene",
    "--measurement-cameras-file=$camFile",
    "--measurement-camera=$cam",
    "--use-multi-bounce=1",
    "--multi-bounce-gain=1.0",
    "--use-hybrid=0",
    "--cascade-scaled-dir-res=1",
    "--noise-seed-offset=0",
    "--use-probe-jitter=1",
    "--use-directional-gi=1",
    "--render-mode=17",
    "--screenshot-exr=1",
    "--auto-capture-delay=0",
    "--exit-frames=$N",
    "--probe-stats-json=$outDir/${tag}_probe_stats.json",
    "--atlas-attribution-json=$outDir/${tag}_atlas_attribution.json",
    "--atlas-attribution-cells=$Cells",
    "--screenshot=$tag.png"
)

Write-Host "[m1-atlas] $tag cells=$Cells"
if ($DryRun) {
    Write-Host "  $exe $($argList -join ' ')"
    exit 0
}

& $exe @argList |
    Select-String -Pattern "measurement-camera|use-hybrid|useDirectionalGI|hdr-exr|probe-stats|atlas-attrib|screenshot saved" |
    ForEach-Object { Write-Host "  $_" }

foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr", "_gbuffer.exr", "_probe_diag.exr")) {
    $src = "${tag}${suffix}"
    if (Test-Path $src) {
        Move-Item -Force $src "$outDir/$src"
        Write-Host "  moved: $src"
    } else {
        Write-Host "  WARN: missing $src"
    }
}

foreach ($jsonName in @("${tag}_probe_stats.json", "${tag}_atlas_attribution.json")) {
    if (Test-Path "$outDir/$jsonName") {
        Write-Host "  kept: $jsonName"
    } else {
        Write-Host "  WARN: missing $jsonName"
    }
}
