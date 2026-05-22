# MBRC v2.0-pre engine-default validation sweep
# (doc/7/alpha_m4_deepdive_impl.md §8.2 blocker).
#
# The (alpha) M4 deep-dive recommended shipping useDirectionalMerge=0 +
# useMultiBounce=1 (g=1.0) as new engine defaults. Before merging, the impl
# doc's §8.2 blockers must pass:
#   B1. Sponza visual A/B (M4 voxel-grid moire more visible on large flat
#       surfaces)
#   B2. Plain Cornell (no alcove) visual A/B (different probe-density-vs-
#       radiance distribution; light source not in alcove)
#   B3. Mode 0 (full composite) A/B (mode 17 GI-only may overstate impact
#       under direct-light dominance)
#
# This sweep addresses all three with 10 captures, ~5 min.
#
# Scenes x cams x configs x modes:
#   cornell-orig-alcove: cam0 + cam2, 2 configs, mode 0      = 4 captures (B3 baseline)
#   cornell-orig (plain): cam0 + cam2, 2 configs, mode 0     = 4 captures (B2)
#   sponza-master: cam_md, 2 configs, mode 0                  = 2 captures (B1)
#
# Configs:
#   baseline  : M0 (all merge ON) + MB OFF                   = current default
#   recommend : M4 (dirMerge OFF, dirBilinear OFF, spatTri ON) + MB ON g=1.0 = proposed new default
# Note: D=8 scaled (engine default), NOT D=16 — the triple-stack from §4 was a
# ceiling test; the recommendation is D8 (D=16 main effect was +0.024 ratio, tie).
#
# All captures: hybrid OFF, seed 0, 512 frames, mode 0 (full composite incl. direct).

$exe       = "./build/RadianceCascades3D.exe"
$frames    = 512
$outDir    = "tools/v20_pre_measurement/captures_engine_default"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function MoveScreenshot {
    param([string]$tag)
    $src = "${tag}.png"
    if (Test-Path $src) {
        Move-Item -Force $src "$outDir/$src"
    } else {
        Write-Host "  WARN: missing $src"
    }
}

# (configName, useDirMerge, useDirBilinear, useSpatialTri, useMB, mbGain)
$configs = @(
    @("baseline",  1, 1, 1, 0, 1.0 ),   # M0 + MBoff (current default)
    @("recommend", 0, 0, 1, 1, 1.0 )    # M4 + MBon g=1.0 (proposed default)
)

# (sceneTag, loadObjName, camFile, camIndices)
$scenes = @(
    @("alcove",      "cornell-orig-alcove", "tools/v20_pre_measurement/cameras.json", @(0, 2)),
    @("plain",       "cornell-orig",        "tools/v20_pre_measurement/cameras.json", @(0, 2)),
    @("sponza",      "sponza-master",       "tools/v20_pre_measurement/sponza_cam.json", @(0))
)

function CaptureCell {
    param(
        [string]$sceneTag, [string]$obj, [string]$camFile, [int]$cam,
        [string]$cfgName, [int]$dm, [int]$db, [int]$st, [int]$mb, [double]$mbg
    )
    $tag  = "${sceneTag}_cam${cam}_${cfgName}_m0"
    $base = "${tag}.png"
    Write-Host "[default] $tag (dm=$dm db=$db st=$st mb=$mb g=$mbg)"
    $argList = @(
        "--load-obj=$obj",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-directional-merge=$dm",
        "--use-dir-bilinear=$db",
        "--use-spatial-trilinear=$st",
        "--use-multi-bounce=$mb",
        "--multi-bounce-gain=$mbg",
        "--use-hybrid=0",
        "--noise-seed-offset=0",
        "--render-mode=0",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "screenshot saved|useDirectional|useDirBilinear|useSpatial|multi-bounce|MB.*cascade-bake|MAIN.*load-obj" |
        ForEach-Object { Write-Host "  $_" }
    MoveScreenshot -tag $tag
}

$tStart = Get-Date
Write-Host "===== Engine-default validation sweep ====="
$i = 0
$total = 0
foreach ($s in $scenes) { $total += $s[3].Count * $configs.Count }

foreach ($scene in $scenes) {
    foreach ($cam in $scene[3]) {
        foreach ($cfg in $configs) {
            $i++
            Write-Host "----- defval [$i/$total] -----"
            CaptureCell -sceneTag $scene[0] -obj $scene[1] -camFile $scene[2] -cam $cam `
                        -cfgName $cfg[0] -dm $cfg[1] -db $cfg[2] -st $cfg[3] `
                        -mb $cfg[4] -mbg $cfg[5]
        }
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] engine-default validation sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
