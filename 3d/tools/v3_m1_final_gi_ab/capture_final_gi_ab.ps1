param(
    [ValidateSet("cornell", "sponza")]
    [string]$Scene = "sponza",
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
$outDir = "tools/v3_m1_final_gi_ab/captures_$Scene"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

$conditions = @(
    @{ name = "diron";  directional = 1 },
    @{ name = "diroff"; directional = 0 }
)

foreach ($c in $conditions) {
    $name = $c["name"]
    $tag = "m1finalgi_${Scene}_${name}_N$('{0:D4}' -f $N)_m17"
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
        "--use-directional-gi=$($c.directional)",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$N",
        "--probe-stats-json=$outDir/${tag}_probe_stats.json",
        "--screenshot=$tag.png"
    )

    Write-Host "[m1-final-gi] $tag directional=$($c.directional)"
    if ($DryRun) {
        Write-Host "  $exe $($argList -join ' ')"
        continue
    }

    & $exe @argList |
        Select-String -Pattern "measurement-camera|use-hybrid|useDirectionalGI|use-directional-gi|hdr-exr|probe-stats|screenshot saved" |
        ForEach-Object { Write-Host "  $_" }

    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr", "_gbuffer.exr")) {
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
}
