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
$relativeReport = "tools/10_refactor/phase4_transport/runs/$runId/reference_transport_report.json"
$reportPath = Join-Path $root $relativeReport

if ($DryRun) {
    Write-Host "cmake -S $root -B $buildDir"
    Write-Host "cmake --build $buildDir --config $Configuration"
    Write-Host "$exe --runtime-shell=app3d --validate-reference-transport --reference-transport-report=$relativeReport"
    exit 0
}

& cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Push-Location $root
try {
    & $exe --runtime-shell=app3d --validate-reference-transport --reference-transport-report=$relativeReport
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing report: $reportPath" }

$report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
$passed = $exitCode -eq 0 -and $report.result -eq "PASS" -and
          $report.gates."G5-payload-contract" -eq "PASS" -and
          $report.gates."G8-material-direct-light" -eq "PASS"
Write-Host "[phase4] report=$reportPath result=$($report.result)"
if (-not $passed) { exit 1 }
