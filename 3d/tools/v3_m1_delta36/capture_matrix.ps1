param(
    [ValidateSet("cornell", "sponza")]
    [string]$Scene = "cornell",
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
$outDir = "tools/v3_m1_delta36/captures_$Scene"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

$conditions = @(
    @{ name = "baseline"; d3 = 0; d6 = 0 },
    @{ name = "delta3";   d3 = 1; d6 = 0 },
    @{ name = "delta6";   d3 = 0; d6 = 1 },
    @{ name = "both";     d3 = 1; d6 = 1 }
)

foreach ($c in $conditions) {
    $name = $c["name"]
    $tag = "m1d36_${Scene}_${name}_N$('{0:D4}' -f $N)_m17"
    $base = "$tag.png"
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
        "--m1-delta3-gated-trilinear=$($c.d3)",
        "--m1-delta6-geometric-cone=$($c.d6)",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$N",
        "--screenshot=$base"
    )

    Write-Host "[m1-d36] $tag d3=$($c.d3) d6=$($c.d6)"
    if ($DryRun) {
        Write-Host "  $exe $($argList -join ' ')"
        continue
    }

    & $exe @argList |
        Select-String -Pattern "m1-delta|measurement-camera|use-hybrid|hdr-exr|screenshot saved" |
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
