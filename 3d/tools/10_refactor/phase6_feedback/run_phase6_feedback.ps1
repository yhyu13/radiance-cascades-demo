param([string]$Configuration="Release",[switch]$DryRun)
$ErrorActionPreference="Stop"
$root=(Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$build=Join-Path $root "build";$exe=Join-Path $build "RadianceCascades3D.exe"
$commit=(& git -C $root rev-parse --short=12 HEAD).Trim()
$id="{0}-{1}-{2}" -f (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ"),$commit,([guid]::NewGuid().ToString("N").Substring(0,8))
$rel="tools/10_refactor/phase6_feedback/runs/$id/reference_feedback_report.json";$path=Join-Path $root $rel
if($DryRun){Write-Host "$exe --runtime-shell=app3d --validate-reference-feedback --reference-feedback-report=$rel";exit 0}
& cmake -S $root -B $build;if($LASTEXITCODE -ne 0){throw "configure failed"}
& cmake --build $build --config $Configuration;if($LASTEXITCODE -ne 0){throw "build failed"}
Push-Location $root;try{& $exe --runtime-shell=app3d --validate-reference-feedback --reference-feedback-report=$rel;$code=$LASTEXITCODE}finally{Pop-Location}
if(-not(Test-Path $path)){throw "missing report"};$r=Get-Content -Raw $path|ConvertFrom-Json
$ok=$code -eq 0 -and $r.result -eq "PASS" -and $r.gates."G7-temporal-feedback" -eq "PASS" -and $r.gates."G10-determinism-stability" -eq "PASS"
Write-Host "[phase6] report=$path result=$($r.result)";if(-not $ok){exit 1}
