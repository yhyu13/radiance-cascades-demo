param([string]$Configuration="Release",[switch]$DryRun)
$ErrorActionPreference="Stop"
$root=(Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$build=Join-Path $root "build";$exe=Join-Path $build "RadianceCascades3D.exe"
$commit=(& git -C $root rev-parse --short=12 HEAD).Trim()
$id="{0}-{1}-{2}" -f (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ"),$commit,([guid]::NewGuid().ToString("N").Substring(0,8))
$rel="tools/10_refactor/phase7_final/runs/$id/reference_final_report.json";$path=Join-Path $root $rel
if($DryRun){Write-Host "$exe --runtime-shell=app3d --validate-reference-final --reference-final-report=$rel";exit 0}
& cmake -S $root -B $build;if($LASTEXITCODE -ne 0){throw "configure failed"}
& cmake --build $build --config $Configuration;if($LASTEXITCODE -ne 0){throw "build failed"}
Push-Location $root;try{& $exe --runtime-shell=app3d --validate-reference-final --reference-final-report=$rel;$code=$LASTEXITCODE}finally{Pop-Location}
if(-not(Test-Path $path)){throw "missing report"};$r=Get-Content -Raw $path|ConvertFrom-Json
Write-Host "[phase7] report=$path result=$($r.result)";if($code -ne 0 -or $r.result -ne "PASS"){exit 1}
