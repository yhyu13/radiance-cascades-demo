$ErrorActionPreference = "Stop"
$script = "tools/v3_m1_mb_gain_ladder/capture_gain.ps1"
$start = Get-Date
Write-Host "[stage9] starting ladder at $start"

# Sponza: 5 new gain points (0.0 reused as sanity, 0.5 and 1.0 already captured by Stage 8)
# We capture 0.00 via the gain-slider path to verify SC1 equivalence vs Stage 8 mb=0.
$sponza = @(0.00, 0.10, 0.20, 0.30, 0.40)
# Cornell: 3 new (1.0 already in Stage 8)
$cornell = @(0.25, 0.50, 0.75)

foreach ($g in $sponza) {
    $t0 = Get-Date
    Write-Host "[stage9] sponza gain=$g"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Scene sponza -Gain $g
    Write-Host "[stage9] sponza gain=$g done in $([int]((Get-Date)-$t0).TotalSeconds)s"
}
foreach ($g in $cornell) {
    $t0 = Get-Date
    Write-Host "[stage9] cornell gain=$g"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Scene cornell -Gain $g
    Write-Host "[stage9] cornell gain=$g done in $([int]((Get-Date)-$t0).TotalSeconds)s"
}

$total = (Get-Date) - $start
Write-Host "[stage9] ladder done in $([int]$total.TotalSeconds)s"
