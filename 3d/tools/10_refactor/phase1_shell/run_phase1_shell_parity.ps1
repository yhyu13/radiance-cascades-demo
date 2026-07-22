param(
    [string]$Configuration = "Release",
    [int]$Frames = 2,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$buildDir = Join-Path $root "build"
$exe = Join-Path $buildDir "RadianceCascades3D.exe"

$commit = (& git -C $root rev-parse --short=12 HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not resolve git commit" }
$runId = "{0}-{1}-{2}" -f (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ"), $commit, ([guid]::NewGuid().ToString("N").Substring(0, 8))
$relativeRunDir = "tools/10_refactor/phase1_shell/runs/$runId"
$runDir = Join-Path $root $relativeRunDir

$commonArgs = @(
    "--phase0-baseline",
    "--window-size=640,480",
    "--use-cascade-gi=1",
    "--use-gi-blur=0",
    "--use-hybrid=0",
    "--use-surface-rc=0",
    "--enable-surface-rc-gi=0",
    "--use-multi-bounce=0",
    "--use-probe-jitter=0",
    "--noise-seed-offset=0",
    "--render-mode=0",
    "--auto-capture-delay=0",
    "--exit-frames=$Frames"
)

if ($DryRun) {
    Write-Host "cmake -S $root -B $buildDir"
    Write-Host "cmake --build $buildDir --config $Configuration"
    foreach ($shell in @("legacy", "app3d")) {
        Write-Host "$exe --runtime-shell=$shell $($commonArgs -join ' ')"
    }
    exit 0
}

if (Test-Path -LiteralPath $runDir) { throw "Refusing to overwrite run directory: $runDir" }
New-Item -ItemType Directory -Path $runDir | Out-Null

& cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$results = [ordered]@{}
foreach ($shell in @("legacy", "app3d")) {
    $reportRelative = "$relativeRunDir/$shell.runtime.json"
    $screenshotRelative = "$relativeRunDir/$shell.png"
    $logPath = Join-Path $runDir "$shell.runtime.log"
    $args = @("--runtime-shell=$shell", "--metadata-json=$reportRelative", "--screenshot=$screenshotRelative") + $commonArgs

    Push-Location $root
    try {
        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $exe @args *> $logPath
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorActionPreference
    } finally {
        $ErrorActionPreference = "Stop"
        Pop-Location
    }

    $reportPath = Join-Path $root $reportRelative
    $screenshotPath = Join-Path $root $screenshotRelative
    if (-not (Test-Path -LiteralPath $reportPath)) { throw "Missing runtime report: $reportPath" }
    if (-not (Test-Path -LiteralPath $screenshotPath)) { throw "Missing screenshot: $screenshotPath" }

    $runtime = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    $results[$shell] = [ordered]@{
        exit_code = $exitCode
        runtime = $runtime
        screenshot_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $screenshotPath).Hash.ToLowerInvariant()
    }
}

$legacy = $results.legacy
$app3d = $results.app3d
$selectionFields = @("backend", "render_view", "render_mode", "scene", "scene_revision", "shader_revision")
$selectionEqual = $true
foreach ($field in $selectionFields) {
    if ($legacy.runtime.selection.$field -ne $app3d.runtime.selection.$field) {
        $selectionEqual = $false
    }
}

$screenshotEqual = $legacy.screenshot_sha256 -eq $app3d.screenshot_sha256
$passed = $legacy.exit_code -eq 0 -and $app3d.exit_code -eq 0 -and
          $legacy.runtime.result -eq "PASS" -and $app3d.runtime.result -eq "PASS" -and
          $legacy.runtime.selection.shell -eq "legacy" -and
          $legacy.runtime.selection.runtime_backend -eq "legacy-direct" -and
          $app3d.runtime.selection.shell -eq "app3d" -and
          $app3d.runtime.selection.runtime_backend -eq "demo3d-legacy" -and
          $selectionEqual -and $screenshotEqual

$report = [ordered]@{
    schema_version = "phase1-shell-parity-report-v1"
    gate = "G1"
    run_id = $runId
    created_utc = (Get-Date).ToUniversalTime().ToString("o")
    result = if ($passed) { "PASS" } else { "FAIL" }
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant()
    executions = $results
    comparison = [ordered]@{
        selection_equal = $selectionEqual
        screenshot_equal = $screenshotEqual
        screenshot_method = "sha256-file"
    }
}

$reportPath = Join-Path $runDir "phase1_shell_parity_report.json"
$report | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -LiteralPath $reportPath
Write-Host "[phase1] report=$reportPath result=$($report.result)"
if (-not $passed) { exit 1 }
