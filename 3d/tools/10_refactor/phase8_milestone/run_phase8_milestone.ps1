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
$relDir = "tools/10_refactor/phase8_milestone/runs/$runId"
$runDir = Join-Path $root $relDir

if ($DryRun) {
    Write-Host "Builds once, then runs G0-G10 plus PT quality into one milestone report at $relDir"
    exit 0
}

& cmake -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
if (Test-Path -LiteralPath $runDir) { throw "Refusing to overwrite run directory: $runDir" }
New-Item -ItemType Directory -Path $runDir | Out-Null

$gates = [ordered]@{}
function Run-Gate {
    param([string]$Name, [string]$ReportRel, [string[]]$GateArgs)
    Push-Location $root
    try {
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $exe @GateArgs *> (Join-Path $runDir "$Name.log")
        $code = $LASTEXITCODE
        $ErrorActionPreference = $prev
    } finally {
        $ErrorActionPreference = "Stop"
        Pop-Location
    }
    $reportPath = Join-Path $root $ReportRel
    $result = "FAIL"
    if ((Test-Path -LiteralPath $reportPath) -and $code -eq 0) {
        $j = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
        if ($j.result -eq "PASS") { $result = "PASS" }
    }
    $gates[$Name] = [ordered]@{ result = $result; exit_code = $code; report = $ReportRel }
    Write-Host "[$Name] $result"
    return ($result -eq "PASS")
}

$allOk = $true

# G0: reproducible legacy baseline (explicit deprecation-window shell)
$g0Report = "$relDir/g0_runtime.json"
$g0Shot = "$relDir/g0_legacy.png"
$allOk = (Run-Gate -Name "G0-baseline" -ReportRel $g0Report -GateArgs @(
    "--runtime-shell=legacy", "--phase0-baseline", "--metadata-json=$g0Report", "--window-size=640,480",
    "--use-cascade-gi=1", "--use-gi-blur=0", "--use-hybrid=0", "--use-surface-rc=0",
    "--enable-surface-rc-gi=0", "--use-multi-bounce=0", "--use-probe-jitter=0",
    "--noise-seed-offset=0", "--render-mode=0", "--auto-capture-delay=0",
    "--exit-frames=2", "--screenshot=$g0Shot")) -and $allOk

# G1 cut-over gate: App3D is the default shell, Demo3D is explicit-only.
#   a) the legacy shell reproduces the G0 baseline exactly (stable deprecation window);
#   b) the app3d default shell runs the reference surface-RC renderer;
#   c) legacy-only flags without --runtime-shell=legacy fail with a usage error
#      (no runtime feature silently falls back to old global state).
$shellCommon = @(
    "--phase0-baseline", "--window-size=640,480", "--use-cascade-gi=1", "--use-gi-blur=0",
    "--use-hybrid=0", "--use-surface-rc=0", "--enable-surface-rc-gi=0", "--use-multi-bounce=0",
    "--use-probe-jitter=0", "--noise-seed-offset=0", "--render-mode=0", "--auto-capture-delay=0",
    "--exit-frames=2")
$g1 = [ordered]@{}
# (a) legacy reproducibility vs G0
$legacyRep = "$relDir/g1_legacy.runtime.json"
$legacyShot = "$relDir/g1_legacy.png"
$g1LegacyArgs = @("--runtime-shell=legacy", "--metadata-json=$legacyRep", "--screenshot=$legacyShot") + $shellCommon
Push-Location $root
try {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $exe @g1LegacyArgs *> (Join-Path $runDir "g1_legacy.log")
    $codeLegacy = $LASTEXITCODE
    $ErrorActionPreference = $prev
} finally {
    $ErrorActionPreference = "Stop"
    Pop-Location
}
$legacyOk = $codeLegacy -eq 0
if ($legacyOk) {
    $lr = Get-Content -Raw (Join-Path $root $legacyRep) | ConvertFrom-Json
    $gr = Get-Content -Raw (Join-Path $root $g0Report) | ConvertFrom-Json
    $selFields = @("backend", "render_view", "render_mode", "scene", "scene_revision", "shader_revision")
    foreach ($f in $selFields) { if ($lr.selection.$f -ne $gr.selection.$f) { $legacyOk = $false } }
    if ($legacyOk) {
        $legacyOk = (Get-FileHash -Algorithm SHA256 (Join-Path $root $legacyShot)).Hash -eq
                    (Get-FileHash -Algorithm SHA256 (Join-Path $root $g0Shot)).Hash
    }
}
$g1.legacy_stable = $legacyOk
Write-Host "[G1-cutover] legacy_stable=$legacyOk"

# (b) app3d reference render (bounded). The no-arg default shell runs the same
# runReferenceRenderInteractive backend but is unbounded (runs until ESC), so CI
# exercises the explicit --reference-render=N subcommand as its proxy.
$refShot = "$relDir/g1_app3d_ref.png"
Push-Location $root
try {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $exe --runtime-shell=app3d --reference-render=2 --reference-render-shot="$refShot" *> (Join-Path $runDir "g1_app3d.log")
    $codeRef = $LASTEXITCODE
    $ErrorActionPreference = $prev
} finally {
    $ErrorActionPreference = "Stop"
    Pop-Location
}
$refOk = $codeRef -eq 0 -and (Test-Path -LiteralPath (Join-Path $root $refShot))
$g1.app3d_reference_render = $refOk
$g1.app3d_default_note = "no-arg default shares the runReferenceRenderInteractive backend; CI proxy is --reference-render=N (unbounded default cannot exit in CI)"
Write-Host "[G1-cutover] app3d_reference_render=$refOk"

# (c) no silent fallback: legacy-only flags without the legacy shell must fail
Push-Location $root
try {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $exe @("--use-cascade-gi=1", "--use-hybrid=0", "--exit-frames=2") *> (Join-Path $runDir "g1_no_fallback.log")
    $codeNoFallback = $LASTEXITCODE
    $ErrorActionPreference = $prev
} finally {
    $ErrorActionPreference = "Stop"
    Pop-Location
}
$noFallbackOk = $codeNoFallback -ne 0
$g1.no_silent_fallback = $noFallbackOk
Write-Host "[G1-cutover] no_silent_fallback=$noFallbackOk"

$g1Ok = $legacyOk -and $refOk -and $noFallbackOk
$gates["G1-cutover"] = [ordered]@{ result = $(if ($g1Ok) { "PASS" } else { "FAIL" }); report = "$relDir/g1_app3d.log"; details = $g1 }
Write-Host "[G1-cutover] $($gates['G1-cutover'].result)"
$allOk = $g1Ok -and $allOk

$allOk = (Run-Gate -Name "G1-chart" -ReportRel "$relDir/g1_scene.json" -GateArgs @(
    "--runtime-shell=app3d", "--validate-reference-cornell-scene",
    "--reference-scene-report=$relDir/g1_scene.json")) -and $allOk

$allOk = (Run-Gate -Name "G2G3G4-layout" -ReportRel "$relDir/layout.json" -GateArgs @(
    "--runtime-shell=app3d", "--validate-reference-layout",
    "--reference-layout-report=$relDir/layout.json")) -and $allOk

$allOk = (Run-Gate -Name "G5G8-transport" -ReportRel "$relDir/transport.json" -GateArgs @(
    "--runtime-shell=app3d", "--validate-reference-transport",
    "--reference-transport-report=$relDir/transport.json")) -and $allOk

$allOk = (Run-Gate -Name "G6-merge" -ReportRel "$relDir/merge.json" -GateArgs @(
    "--runtime-shell=app3d", "--validate-reference-merge",
    "--reference-merge-report=$relDir/merge.json")) -and $allOk

$allOk = (Run-Gate -Name "G7G10-feedback" -ReportRel "$relDir/feedback.json" -GateArgs @(
    "--runtime-shell=app3d", "--validate-reference-feedback",
    "--reference-feedback-report=$relDir/feedback.json")) -and $allOk

$allOk = (Run-Gate -Name "G9-final" -ReportRel "$relDir/final.json" -GateArgs @(
    "--runtime-shell=app3d", "--validate-reference-final",
    "--reference-final-report=$relDir/final.json")) -and $allOk

# PT quality comparison (non-blocking): NEW vs parity-scene PT, same camera.
$quality = [ordered]@{ result = "INCOMPLETE" }
Push-Location $root
try {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $exe --runtime-shell=app3d --reference-render=24 --reference-render-shot="$relDir/q_new.png" *> (Join-Path $runDir "q_new.log")
    & $exe --reference-pt-shot="$relDir/q_pt.png" --reference-pt-spp=64 *> (Join-Path $runDir "q_pt.log")
    & $exe --reference-pt-shot="$relDir/q_pt_direct.png" --reference-pt-spp=64 --reference-pt-bounces=1 *> (Join-Path $runDir "q_pt_direct.log")
    & $exe --reference-pt-shot="$relDir/q_pt_nomirror.png" --reference-pt-spp=64 --reference-pt-reflective-zero *> (Join-Path $runDir "q_pt_nomirror.log")
    $ErrorActionPreference = $prev
} finally {
    $ErrorActionPreference = "Stop"
    Pop-Location
}

function Luma([string]$path) {
    Add-Type -AssemblyName System.Drawing
    $img = [System.Drawing.Bitmap]::FromFile($path)
    $sum = 0.0; $n = 0
    for ($y = 0; $y -lt $img.Height; $y++) {
        for ($x = 0; $x -lt $img.Width; $x++) {
            $p = $img.GetPixel($x, $y)
            $sum += 0.2126 * $p.R + 0.7152 * $p.G + 0.0722 * $p.B
            $n++
        }
    }
    $img.Dispose()
    return [math]::Round($sum / $n, 3)
}
try {
    $qNew = Luma (Join-Path $root "$relDir/q_new_linear.png")
    $qPt = Luma (Join-Path $root "$relDir/q_pt_linear.png")
    $qDirect = Luma (Join-Path $root "$relDir/q_pt_direct_linear.png")
    $qNoMirror = Luma (Join-Path $root "$relDir/q_pt_nomirror_linear.png")
    $ratio = if ($qPt -gt 0) { [math]::Round($qNew / $qPt, 3) } else { 0 }
    $quality = [ordered]@{
        result = "INFO"
        note = "non-blocking quality report; semantic parity is established by gates G0-G10, not pixel equality"
        new_linear_luma = $qNew
        pt_linear_luma = $qPt
        pt_direct_only_luma = $qDirect
        pt_no_mirror_luma = $qNoMirror
        new_vs_pt_ratio = $ratio
    }
} catch {
    $quality = [ordered]@{ result = "INCOMPLETE"; error = $_.Exception.Message }
}
Write-Host "[PT-quality] $($quality.result)"

$differencesDoc = "doc/10_refactor/semantic_parity_differences.md"
$diffExists = Test-Path -LiteralPath (Join-Path $root $differencesDoc)

$report = [ordered]@{
    schema_version = "semantic-parity-milestone-v1"
    milestone = "Phase 8: Cornell semantic parity (not general mesh support)"
    run_id = $runId
    created_utc = (Get-Date).ToUniversalTime().ToString("o")
    commit = $commit
    result = $(if ($allOk) { "PASS" } else { "FAIL" })
    gates = $gates
    pt_quality = $quality
    deliberate_differences_document = $differencesDoc
    deliberate_differences_present = $diffExists
    claims = [ordered]@{
        no_hidden_calibration_constants = $allOk
        stable_indirect_transport_and_color_bleeding = $allOk
        label = "semantic parity, not general mesh support"
    }
}
$reportPath = Join-Path $runDir "semantic_parity_report.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath $reportPath
Write-Host "[phase8] report=$reportPath result=$($report.result)"
if (-not $allOk) { exit 1 }
