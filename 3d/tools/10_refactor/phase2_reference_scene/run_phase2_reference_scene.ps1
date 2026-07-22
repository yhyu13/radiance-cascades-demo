param(
    [string]$Configuration = "Release",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$buildDir = Join-Path $root "build"
$exe = Join-Path $buildDir "RadianceCascades3D.exe"
$commit = (& git -C $root rev-parse --short=12 HEAD).Trim()
$runId = "{0}-{1}-{2}" -f (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ"), $commit, ([guid]::NewGuid().ToString("N").Substring(0, 8))
$relativeReport = "tools/10_refactor/phase2_reference_scene/runs/$runId/reference_scene_report.json"
$reportPath = Join-Path $root $relativeReport

if ($DryRun) {
    Write-Host "cmake -S $root -B $buildDir"
    Write-Host "cmake --build $buildDir --config $Configuration"
    Write-Host "$exe --runtime-shell=app3d --validate-reference-cornell-scene --reference-scene-report=$relativeReport"
    exit 0
}

& cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Push-Location $root
try {
    & $exe --runtime-shell=app3d --validate-reference-cornell-scene --reference-scene-report=$relativeReport
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($exitCode -ne 0) { exit $exitCode }
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing report: $reportPath" }

$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
if ($report.result -ne "PASS" -or $report.gate -ne "G1-chart-contract") { exit 1 }
if ($report.chart_count -ne 8 -or $report.gpu_contract.bytes -ne 2368) { exit 1 }

Write-Host "[phase2] report=$reportPath result=PASS"
