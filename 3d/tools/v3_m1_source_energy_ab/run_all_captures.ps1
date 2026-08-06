$ErrorActionPreference = "Stop"
$script = "tools/v3_m1_source_energy_ab/capture_source_energy.ps1"
$start = Get-Date
Write-Host "[stage8] starting all captures at $start"

$jobs = @(
    @{ Scene = "sponza";  Variant = "mb_off" },
    @{ Scene = "sponza";  Variant = "mb_gain_half" },
    @{ Scene = "sponza";  Variant = "jitter_off" },
    @{ Scene = "sponza";  Variant = "delta3_on" },
    @{ Scene = "sponza";  Variant = "hybrid_on" },
    @{ Scene = "cornell"; Variant = "mb_off" },
    @{ Scene = "cornell"; Variant = "delta3_on" }
)

# Cornell baseline is also needed for verdict comparison.
$jobs = @(@{ Scene = "cornell"; Variant = "baseline" }) + $jobs

foreach ($j in $jobs) {
    $t0 = Get-Date
    Write-Host "[stage8] running $($j.Scene) $($j.Variant)"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $script -Scene $j.Scene -Variant $j.Variant
    $dt = (Get-Date) - $t0
    Write-Host "[stage8] $($j.Scene) $($j.Variant) finished in $([int]$dt.TotalSeconds)s"
}

$total = (Get-Date) - $start
Write-Host "[stage8] all captures done in $([int]$total.TotalSeconds)s"
