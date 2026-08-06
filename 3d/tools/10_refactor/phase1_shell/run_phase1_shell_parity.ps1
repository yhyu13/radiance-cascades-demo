param(
    [string]$Configuration = "Release",
    [int]$Frames = 2,
    [switch]$DryRun
)

# Phase 1 G1 gate, updated for the Phase 9 shell cut-over.
# App3D is the default shell; Demo3D is reachable only via --runtime-shell=legacy.
# The old "legacy vs app3d-wrapped byte parity" assertion is retired with the
# removal of the silent Demo3D fallback. The gate now verifies:
#   a) the legacy shell reproduces the frozen baseline exactly;
#   b) the app3d shell runs the reference surface-RC renderer;
#   c) legacy-only flags without --runtime-shell=legacy fail with a usage error.

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
    Write-Host "$exe --runtime-shell=legacy $($commonArgs -join ' ')  # frozen baseline"
    Write-Host "$exe --runtime-shell=app3d --reference-render=2 --reference-render-shot=...  # reference default"
    Write-Host "$exe --use-cascade-gi=1 --exit-frames=2  # must fail (no silent fallback)"
    exit 0
}

if (Test-Path -LiteralPath $runDir) { throw "Refusing to overwrite run directory: $runDir" }
New-Item -ItemType Directory -Path $runDir | Out-Null

& cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

function Invoke-ExeCapture {
    param([string[]]$Arguments, [string]$LogName, [string]$Shell, [bool]$WritesRuntimeReport = $true)
    $logPath = Join-Path $runDir $LogName
    $reportRelative = if ($WritesRuntimeReport) { "$relativeRunDir/$Shell.runtime.json" } else { "" }
    $screenshotRelative = "$relativeRunDir/$Shell.png"
    Push-Location $root
    try {
        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $exe @Arguments *> $logPath
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorActionPreference
    } finally {
        $ErrorActionPreference = "Stop"
        Pop-Location
    }
    return [ordered]@{
        exit_code = $exitCode
        log = $logPath
        report_relative = $reportRelative
        screenshot_relative = $screenshotRelative
    }
}

# (a) frozen legacy baseline
$legacyArgs = @("--runtime-shell=legacy", "--metadata-json=$relativeRunDir/legacy.runtime.json", "--screenshot=$relativeRunDir/legacy.png") + $commonArgs
$legacy = Invoke-ExeCapture -Arguments $legacyArgs -LogName "legacy.runtime.log" -Shell "legacy"

# (b) app3d reference renderer (bounded); produces a screenshot, no runtime.json
$refArgs = @("--runtime-shell=app3d", "--reference-render=2", "--reference-render-shot=$relativeRunDir/app3d.png")
$app3d = Invoke-ExeCapture -Arguments $refArgs -LogName "app3d.log" -Shell "app3d" -WritesRuntimeReport $false

# (c) no silent fallback
$noFallbackLog = Join-Path $runDir "no_fallback.log"
Push-Location $root
try {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $exe "--use-cascade-gi=1" "--use-hybrid=0" "--exit-frames=$Frames" *> $noFallbackLog
    $noFallbackExit = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
} finally {
    $ErrorActionPreference = "Stop"
    Pop-Location
}

$legacyStable = $false
if ($legacy.exit_code -eq 0) {
    $legacyReport = Get-Content -Raw (Join-Path $root $legacy.report_relative) | ConvertFrom-Json
    $legacyStable = $legacyReport.result -eq "PASS" -and
                    $legacyReport.selection.shell -eq "legacy" -and
                    $legacyReport.selection.runtime_backend -eq "legacy-direct"
}
$app3dOk = $app3d.exit_code -eq 0 -and
           (Test-Path -LiteralPath (Join-Path $root $app3d.screenshot_relative))
$noFallbackOk = $noFallbackExit -ne 0

$passed = $legacyStable -and $app3dOk -and $noFallbackOk

$report = [ordered]@{
    schema_version = "phase1-cutover-report-v2"
    gate = "G1"
    phase = "Phase 9 shell cut-over (supersedes legacy-vs-app3d shell parity)"
    run_id = $runId
    created_utc = (Get-Date).ToUniversalTime().ToString("o")
    result = if ($passed) { "PASS" } else { "FAIL" }
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant()
    checks = [ordered]@{
        legacy_baseline_stable = $legacyStable
        app3d_runs_reference_default = $app3dOk
        no_silent_fallback_to_demo3d = $noFallbackOk
        no_fallback_exit_code = $noFallbackExit
    }
    executions = [ordered]@{
        legacy = $legacy
        app3d = $app3d
    }
}

$reportPath = Join-Path $runDir "phase1_shell_parity_report.json"
$report | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -LiteralPath $reportPath
Write-Host "[phase1] report=$reportPath result=$($report.result)"
if (-not $passed) { exit 1 }
