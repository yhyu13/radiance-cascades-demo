# MBRC v2.0 (h.2) merge-asymmetry sweep at single-bounce baseline
#
# Per doc/7/v20_h_source_disambig_impl.md sec 6: the (h) disambig
# isolated a 2x geometric-asymmetry spread in the single-bounce merge
# at b=2 with MB OFF (cam0 0.67 / cam2 0.33). The alpha_m4_deepdive
# headline "M4 maximizes brightness" was measured at MB ON only. This
# sweep tests whether the asymmetry varies across merge variants:
#
#   M0_baseline    dm=1 db=1 st=1  (engine PRE-v2.0-pre default; all directional ON)
#   M2_iso_merge   dm=0 db=1 st=1  (no directional merge; hardware bilinear ON)
#   M4_iso_nearest dm=0 db=0 st=1  (engine NEW default; nearest-fetch isotropic)
#
# M4 captures already exist in captures_h_disambig/ (MB OFF b=2 from
# disambig). This sweep adds 4 new cells (M0 + M2, each on cam0+cam2),
# total 4 captures ~1 min.
#
# Pre-committed verdict (cam2/cam0 ratio at MB OFF b=2):
#   - All three merges in [0.45, 0.55] -> MERGE_NOT_THE_SOURCE
#         The 2x asymmetry is invariant under merge choice; cause is
#         deeper (camera projection, probe placement, ray geometry).
#   - One merge variant has cam2/cam0 in [0.80, 1.20] (symmetric)
#         while others are [0.40, 0.55] -> MERGE_VARIANT_X_SYMMETRIZES
#         That variant is the asymmetry-reducing target; revisit
#         engine default flip.
#   - All three have cam2/cam0 < 0.6 BUT one variant brings cam0
#         closer to 1.0 -> MERGE_VARIANT_X_BRIGHTENS_BUT_PRESERVES_SPREAD
#         The spread is fundamental; brightness is tunable but the
#         asymmetry persists. (h.2) demotes to "intrinsic to merge
#         architecture" -- pivot to (b) smoothstep / (c) projection.

$exe    = "./build/RadianceCascades3D.exe"
$frames = 512
$outDir = "tools/v20_arch_diagnostic/captures_h2_merge"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

# (cellName, dm, db, st)
$cells = @(
    @("M0_baseline",   1, 1, 1),
    @("M2_iso_merge",  0, 1, 1)
    # M4 reused from captures_h_disambig/alcove_cam{0,2}_b2_mboff_m17_*
)

function Capture {
    param([string]$cellName, [int]$dm, [int]$db, [int]$st, [int]$cam)
    $tag  = "alcove_cam${cam}_${cellName}_b2_mboff_m17"
    $base = "${tag}.png"
    Write-Host "[h2] $tag (dm=$dm db=$db st=$st, MB OFF, b=2)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--pt-max-bounces=2",
        "--use-multi-bounce=0",
        "--use-directional-merge=$dm",
        "--use-dir-bilinear=$db",
        "--use-spatial-trilinear=$st",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|use-directional|use-dir-bilinear|use-spatial|multi-bounce|pt-max-bounces|PT accumulators" |
        ForEach-Object { Write-Host "  $_" }
    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

$tStart = Get-Date
Write-Host "===== v2.0 (h.2) merge-asymmetry at single-bounce MB-OFF b=2 ====="
foreach ($cell in $cells) {
    foreach ($cam in @(0, 2)) {
        Capture -cellName $cell[0] -dm $cell[1] -db $cell[2] -st $cell[3] -cam $cam
    }
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
