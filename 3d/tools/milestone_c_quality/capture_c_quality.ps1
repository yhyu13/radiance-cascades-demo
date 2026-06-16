param(
    [int]$N = 512,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$exe = "./build/RadianceCascades3D.exe"
$outDir = "tools/milestone_c_quality/captures"
$camFile = "tools/v20_pre_measurement/sponza_cam.json"

if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
if (-not (Test-Path $camFile)) { throw "Missing camera file: $camFile" }
if (-not $DryRun -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

function Invoke-CQualityCapture {
    param(
        [string]$Variant,
        [string[]]$ExtraArgs
    )

    $tag = "cquality_sponza_${Variant}_N$('{0:D4}' -f $N)_m17"
    $screenshot = "$outDir/$tag.png"
    $log = "$outDir/$tag.log"
    $argList = @(
        "--load-obj=sponza",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=0",
        "--mb-gain-per-scene",
        "--use-multi-bounce=1",
        "--use-directional-gi=1",
        "--pt-cascade-match=0",
        "--pt-rays-per-frame=1",
        "--pt-max-bounces=3",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$N",
        "--screenshot=$screenshot"
    ) + $ExtraArgs

    Write-Host "[c-quality] $Variant -> $tag"
    if ($DryRun) {
        Write-Host "  $exe $($argList -join ' ')"
        return
    }

    & $exe @argList *> $log
    if ($LASTEXITCODE -ne 0) {
        throw "Capture failed for $Variant with exit code $LASTEXITCODE. See $log"
    }

    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr", "_gbuffer.exr")) {
        $path = "$outDir/$tag$suffix"
        if (-not (Test-Path $path)) {
            throw "Missing expected capture artifact: $path"
        }
    }
}

function Invoke-CQualityDiagnostic {
    param(
        [string]$Name,
        [int]$Mode
    )

    $png = "$outDir/cquality_diag_${Name}.png"
    $log = "$outDir/cquality_diag_${Name}.log"
    $argList = @(
        "--load-obj=sponza",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=0",
        "--mb-gain-per-scene",
        "--use-multi-bounce=1",
        "--use-directional-gi=1",
        "--use-surface-rc=1",
        "--enable-surface-rc-gi=1",
        "--blend-with-volumetric=0",
        "--surface-gi-scale=10",
        "--render-mode=$Mode",
        "--auto-capture-delay=0",
        "--exit-frames=64",
        "--screenshot=$png"
    )

    Write-Host "[c-quality] diagnostic $Name mode $Mode"
    if ($DryRun) {
        Write-Host "  $exe $($argList -join ' ')"
        return
    }

    & $exe @argList *> $log
    if ($LASTEXITCODE -ne 0) {
        throw "Diagnostic capture failed for $Name with exit code $LASTEXITCODE. See $log"
    }
    if (-not (Test-Path $png)) {
        throw "Missing expected diagnostic screenshot: $png"
    }
}

Invoke-CQualityCapture -Variant "volumetric" -ExtraArgs @(
    "--enable-surface-rc-gi=0"
)

Invoke-CQualityCapture -Variant "surface_rc" -ExtraArgs @(
    "--use-surface-rc=1",
    "--enable-surface-rc-gi=1",
    "--blend-with-volumetric=0",
    "--surface-gi-scale=1"
)

Invoke-CQualityCapture -Variant "surface_rc_scale10" -ExtraArgs @(
    "--use-surface-rc=1",
    "--enable-surface-rc-gi=1",
    "--blend-with-volumetric=0",
    "--surface-gi-scale=10"
)

Invoke-CQualityDiagnostic -Name "m21_gi_only" -Mode 21
Invoke-CQualityDiagnostic -Name "m22_chart" -Mode 22
Invoke-CQualityDiagnostic -Name "m23_c0_raw" -Mode 23

if (-not $DryRun) {
    python tools/milestone_c_quality/analyze_c_quality.py --n $N
}
