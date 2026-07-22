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
$relativeRunDir = "tools/10_refactor/phase0_baseline/runs/$runId"
$runDir = Join-Path $root $relativeRunDir
$runtimeReport = "$relativeRunDir/runtime.json"
$screenshot = "$relativeRunDir/legacy_volumetric.png"
$runtimeLog = Join-Path $runDir "runtime.log"
$reportPath = Join-Path $runDir "phase0_report.json"

$runtimeArgs = @(
    "--phase0-baseline",
    "--metadata-json=$runtimeReport",
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
    "--exit-frames=$Frames",
    "--screenshot=$screenshot"
)

if ($DryRun) {
    Write-Host "cmake -S $root -B $buildDir"
    Write-Host "cmake --build $buildDir --config $Configuration"
    Write-Host "$exe $($runtimeArgs -join ' ')"
    exit 0
}

if (Test-Path -LiteralPath $runDir) { throw "Refusing to overwrite run directory: $runDir" }
New-Item -ItemType Directory -Path $runDir | Out-Null

$statusLines = @(& git -C $root status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0) { throw "Could not inventory worktree" }
$branch = (& git -C $root branch --show-current).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not resolve git branch" }

& cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
if (-not (Test-Path -LiteralPath $exe)) { throw "Missing executable: $exe" }

Push-Location $root
try {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $exe @runtimeArgs *> $runtimeLog
    $runtimeExitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
} finally {
    $ErrorActionPreference = "Stop"
    Pop-Location
}

$runtimeReportPath = Join-Path $root $runtimeReport
$screenshotPath = Join-Path $root $screenshot
if (-not (Test-Path -LiteralPath $runtimeReportPath)) { throw "Missing runtime report: $runtimeReportPath" }
if (-not (Test-Path -LiteralPath $screenshotPath)) { throw "Missing screenshot: $screenshotPath" }

$runtime = Get-Content -Raw -LiteralPath $runtimeReportPath | ConvertFrom-Json
$shaderFiles = @(Get-ChildItem -LiteralPath (Join-Path $root "res/shaders") -File | Sort-Object Name | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
    }
})

$artifacts = @($runtimeReportPath, $screenshotPath, $runtimeLog) | ForEach-Object {
    $item = Get-Item -LiteralPath $_
    [ordered]@{
        path = $item.FullName.Substring($root.Length + 1).Replace('\', '/')
        bytes = $item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash.ToLowerInvariant()
    }
}

$reportResult = if ($runtimeExitCode -eq 0 -and $runtime.result -eq "PASS") { "PASS" } else { "FAIL" }
$report = [ordered]@{
    schema_version = "phase0-report-v1"
    gate = "G0"
    run_id = $runId
    created_utc = (Get-Date).ToUniversalTime().ToString("o")
    result = $reportResult
    claim = "Legacy baseline and source/runtime integrity only; no radiance-cascade parity claim."
    repository = [ordered]@{
        commit = $commit
        branch = $branch
        dirty = ($statusLines.Count -gt 0)
        worktree_entries = $statusLines
    }
    build = [ordered]@{
        configuration = $Configuration
        executable = [ordered]@{
            path = $exe.Substring($root.Length + 1).Replace('\', '/')
            bytes = (Get-Item -LiteralPath $exe).Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash.ToLowerInvariant()
        }
    }
    runtime = [ordered]@{
        exit_code = $runtimeExitCode
        report = $runtime
    }
    source_shaders = $shaderFiles
    artifacts = $artifacts
}

$report | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -LiteralPath $reportPath
Write-Host "[phase0] report=$reportPath result=$reportResult"
if ($reportResult -ne "PASS") { exit 1 }
