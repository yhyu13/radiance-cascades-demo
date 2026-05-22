# MBRC v2.0-pre (alpha) M4_iso_nearest deep-dive stacking sweep
# (doc/7/hdr_relitigation_impl.md sec 6.1).
#
# After the HDR re-litigation found M4 = +53% cam0 ratio (largest single-knob
# brightness lever measured), test whether M4 stacks with the other two HDR-
# confirmed levers:
#   (beta)  MB ON g=1.0 -- +136% cam0 ratio over MB OFF baseline.
#   (gamma) D=16 uniform -- borderline TIE (+9.5%) but might amplify M4.
#
# Existing captures from hdr_relitigate_sweep.ps1 provide baseline cells:
#   M0+MBoff+D8(scaled)        = captures_hdr_alpha/cam{N}_M0_baseline_m17
#   M4+MBoff+D8(scaled)        = captures_hdr_alpha/cam{N}_M4_iso_nearest_m17
#   M0+MBon (g=1.0)+D8(scaled) = captures_hdr_beta/cam{N}_g100_m17
#   M0+MBoff+D16(uniform)      = captures_hdr_gamma/cam{N}_d16_m17
#
# NEW cells captured here (4 cells x 2 cams = 8 captures, ~2 min):
#   S1  M4 + MBon (g=1.0) + D8(scaled)         -- pair-stack with MB
#   S2  M0 + MBon (g=1.0) + D16(uniform)       -- MB + D=16 control
#   S3  M4 + MBoff + D16(uniform)              -- pair-stack with D=16
#   S4  M4 + MBon (g=1.0) + D16(uniform)       -- triple-stack ceiling
#
# Hypothesis test grid for each cam:
#   - If M4 + MB stacks linearly: M4+MBon ~ M0+MBoff*(1+0.53)*(1+1.36)
#   - If M4 fully exposes a single attenuation pathway that MB compensates
#     for, M4+MBon converges to a ratio near 1.0 (cascade matches PT).
#   - If MB feedback eigenvalue blows up earlier when M4 disables the merge
#     attenuator, S4 may show >1.0 ratio (overshoot, like beta g>=1.5 did).
#
# All captures: cornell-orig-alcove, hybrid OFF, mode 17 + --screenshot-exr=1,
# seed 0, 512 frames.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$frames    = 512
$cameras   = @(0, 2)
$outDir    = "tools/v20_pre_measurement/captures_hdr_m4stack"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function MoveSidecars {
    param([string]$tag)
    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

# (cellName, useDirMerge, useDirBilinear, useSpatialTri, useMB, mbGain, scaledDirRes, dirRes)
$cells = @(
    @("S1_M4_MBon_D8scaled",   0, 0, 1, 1, 1.0, 1, 0 ),  # dirRes=0 => use engine default scaling
    @("S2_M0_MBon_D16uniform", 1, 1, 1, 1, 1.0, 0, 16),
    @("S3_M4_MBoff_D16unif",   0, 0, 1, 0, 1.0, 0, 16),
    @("S4_M4_MBon_D16unif",    0, 0, 1, 1, 1.0, 0, 16)
)

function CaptureCell {
    param(
        [string]$cellName, [int]$dm, [int]$db, [int]$st,
        [int]$mb, [double]$mbg, [int]$sdr, [int]$dr, [int]$cam
    )
    $tag  = "cam${cam}_${cellName}_m17"
    $base = "${tag}.png"
    Write-Host "[m4stack] $tag (dm=$dm db=$db st=$st mb=$mb g=$mbg sdr=$sdr dr=$dr)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-directional-merge=$dm",
        "--use-dir-bilinear=$db",
        "--use-spatial-trilinear=$st",
        "--use-multi-bounce=$mb",
        "--multi-bounce-gain=$mbg",
        "--use-hybrid=0",
        "--cascade-scaled-dir-res=$sdr",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    if ($dr -gt 0) { $argList += "--cascade-dir-res=$dr" }
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|useDirectional|useDirBilinear|useSpatial|multi-bounce|cascade-dir-res|MB.*cascade-bake" |
        ForEach-Object { Write-Host "  $_" }
    MoveSidecars -tag $tag
}

$tStart = Get-Date
Write-Host "===== (alpha) M4 deep-dive stacking sweep ====="
$i = 0; $total = $cells.Count * $cameras.Count
foreach ($cell in $cells) {
    foreach ($cam in $cameras) {
        $i++
        Write-Host "----- m4stack [$i/$total] -----"
        CaptureCell -cellName $cell[0] -dm $cell[1] -db $cell[2] -st $cell[3] `
                    -mb $cell[4] -mbg $cell[5] -sdr $cell[6] -dr $cell[7] -cam $cam
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] m4 stacking sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
