# MBRC v2.0 P2 — Per-Pixel Dominant Direction Bin Viz capture
#
# Context: 4-A/B chain (h.b/h.c/h.c'/h.c'') locked-in the downstream cascade
# consumption path as INNOCENT of the cam0/cam2 spread on cornell-orig-alcove.
# All 4 downstream toggles tested are symmetrizers (see
# doc/7/v20_downstream_symmetrizer_architecture.md), so the asymmetry source
# MUST be bake-side per-direction-bin atlas content (by elimination, not by
# direct measurement).
#
# P2 is the direct measurement. Mode 22 (added 2026-05-24) computes per-pixel
# the (dx,dy) atlas direction-bin index that contributes most to the visible
# irradiance, at the nearest-parent probe (independent of all downstream
# toggles — so this measures atlas content, not consumption math). EXR output
# encodes the bin index in R,G channels (round-trippable via floor(x*D)) and
# dominance fraction in B.
#
# Captures cam0 + cam2 at mode 22 on cornell-orig-alcove, M0, MB-OFF, b=2,
# with current engine defaults (ST=0 as of 2026-05-24; ST is irrelevant for
# mode 22 since it uses nearest-parent probe).
#
# Pre-committed verdict bands (analyzer computes histogram overlap of dominant
# bins between cam0 and cam2):
#
#   overlap >= 0.70  -> P2_OVERLAP_HIGH (bake-side framing INCOMPLETE — the
#                       symmetrizer interpretation is missing a piece)
#   overlap in [0.40, 0.70)
#                    -> P2_OVERLAP_MEDIUM (partial bake-side asymmetry; mixed
#                       picture)
#   overlap < 0.40   -> P2_OVERLAP_LOW (bake-side per-bin framing CONFIRMED:
#                       cam0 and cam2 sample fundamentally different atlas
#                       direction bins, scoping bake-side fixes)

$exe    = "./build/RadianceCascades3D.exe"
$frames = 256  # mode 22 needs no MC accumulation; 256 frames lets cascades settle
$outDir = "tools/v20_arch_diagnostic/captures_p2_dombin"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$cam)
    $tag  = "alcove_cam${cam}_M0_b2_mboff_dombin_m22"
    $base = "${tag}.png"
    Write-Host "[p2] $tag (mode 22 dominant-bin, M0, MB OFF, b=2, ST=0 default)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=tools/v20_pre_measurement/cameras.json",
        "--measurement-camera=$cam",
        "--use-hybrid=0",
        "--use-multi-bounce=0",
        "--blend-mode=0",
        "--noise-seed-offset=0",
        "--render-mode=22",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|render mode|blend-mode|use-multi-bounce|use-spatial" |
        ForEach-Object { Write-Host "  $_" }
    foreach ($suffix in @(".png", "_dombin.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

$tStart = Get-Date
Write-Host "===== v2.0 P2 dominant-direction-bin capture (cam0, cam2) ====="
foreach ($cam in @(0, 2)) {
    Capture -cam $cam
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"
