$ErrorActionPreference = "Stop"
$script = "tools/v3_m1_cornell_light_distance/capture_distance.ps1"
$start = Get-Date

# Capture 3 new distance variants + 1 reverify (sanity-check baseline against existing).
$variants = @("near_baseline_reverify", "mid_0p8_x4", "far_5", "far_25")
foreach ($v in $variants) {
    $t0 = Get-Date
    Write-Host "[stage11d] $v"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Variant $v
    Write-Host "[stage11d] $v done in $([int]((Get-Date)-$t0).TotalSeconds)s"
}
Write-Host "[stage11d] all done in $([int]((Get-Date)-$start).TotalSeconds)s"
