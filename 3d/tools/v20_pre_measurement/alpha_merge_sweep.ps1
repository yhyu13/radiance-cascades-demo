# MBRC v2.0-pre - (alpha) merge-mode discriminator sweep.
#
# Tests whether disabling the directional / bilinear / spatial-trilinear merge
# weights shrinks the asymmetric cascade-vs-PT delta from doc/7 sec 13.
#
# 5 configs x 2 cams x 2 modes = 20 captures. Engine default = M0 (all ON).
#
# Configs (named for the toggle that is OFF vs baseline):
#   M0_baseline      : dirMerge=1 dirBilin=1 spatialTri=1  (current default)
#   M1_no_bilin      : dirMerge=1 dirBilin=0 spatialTri=1  (test 4-bin bilinear weighting)
#   M2_iso_merge     : dirMerge=0 dirBilin=1 spatialTri=1  (test directional-lookup vs isotropic-fallback)
#   M3_no_spatialtri : dirMerge=1 dirBilin=1 spatialTri=0  (test 8-neighbor spatial blend)
#   M4_iso_nearest   : dirMerge=0 dirBilin=0 spatialTri=1  (isotropic + nearest-bin; tests texture-vs-texelFetch)
#
# Pre-committed verdict (B1 area discriminator, written BEFORE running):
#   STRONG_ALPHA : >=20% mode-19 Delta-area reduction on BOTH cams in ANY of M1..M4
#   WEAK_ALPHA   : 10-20% on both, or >=20% on one cam only in any arm
#   ALPHA_REJECT : ALL arms within +/-10% of M0 on BOTH cams -> pivot to (delta)
#
# Hybrid OFF, MB OFF (clean baseline; matches mb_gain_sweep + cascade_config_sweep
# protocol so results are directly comparable to sec 12 and sec 13).
# --cascade-scaled-dir-res=1 = engine default. --noise-seed-offset=0 = single seed
# (bug-230 still open; gated on WEAK band per impl doc).
# frames=512 = matches v2.0-pre baseline.

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$outDir    = "tools/v20_pre_measurement/captures_alpha"
$frames    = 512
$cameras   = @(0, 2)
$modes     = @(18, 19)

# (name, dirMerge, dirBilinear, spatialTrilinear)
$configs = @(
    @("M0_baseline",     1, 1, 1),
    @("M1_no_bilin",     1, 0, 1),
    @("M2_iso_merge",    0, 1, 1),
    @("M3_no_spatialtri",1, 1, 0),
    @("M4_iso_nearest",  0, 0, 1)
)

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([string]$cfgName, [int]$dm, [int]$db, [int]$st, [int]$cam, [int]$mode)
    $tag  = "cam${cam}_${cfgName}_m$('{0:D2}' -f $mode)"
    $base = "${tag}.png"
    Write-Host "[capture] $tag (dm=$dm db=$db st=$st)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-directional-merge=$dm",
        "--use-dir-bilinear=$db",
        "--use-spatial-trilinear=$st",
        "--use-multi-bounce=0",
        "--use-hybrid=0",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--render-mode=$mode",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList 2>&1 |
        Select-String -Pattern "useDirectionalMerge|useDirBilinear|useSpatialTrilinear|render-mode|measurement-camera|screenshot saved|use-hybrid|use-multi-bounce" |
        ForEach-Object { Write-Host "  $_" }
    if (Test-Path $base) {
        Move-Item -Force $base "$outDir/$base"
        Write-Host "  saved: $outDir/$base"
    } else {
        Write-Host "  WARN: no png produced for $tag"
    }
}

$total = $configs.Count * $cameras.Count * $modes.Count
$i = 0
$tStart = Get-Date
foreach ($cfg in $configs) {
    $name=$cfg[0]; $dm=$cfg[1]; $db=$cfg[2]; $st=$cfg[3]
    foreach ($cam in $cameras) {
        foreach ($mode in $modes) {
            $i++
            Write-Host "----- [$i/$total] -----"
            Capture -cfgName $name -dm $dm -db $db -st $st -cam $cam -mode $mode
        }
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] alpha-merge sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
