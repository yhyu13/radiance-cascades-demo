# MBRC v2.0 CV1 — Absolute cascade-vs-PT convergence sweep
#
# Context: P2-E (commit 8007296, 2026-05-24) falsified the cam0/cam2 P2
# framing — P2 mode-22 measured viewport composition, not cross-camera GI
# parity. v2.0 pivots to a cam-AGNOSTIC absolute cascade-vs-PT convergence
# study on the locked v2.0 scope: cornell (full Cornell box), single cam,
# hybrid OFF, MB-ON g=1.0 (engine default once MB is on).
#
# What this measures: at fixed frame counts N ∈ {128, 256, 512, 1024, 2048},
# render-mode 17 emits a paired (cascade_gi, pt_full, pt_direct) EXR triplet
# per capture. ptGI = pt_full − pt_direct. cascadeGI is 2x2-downsampled to
# match PT's half-viewport size before differencing (analyzer handles this).
#
# Two analyses per N:
#   (A) Self-paired: mean(cascadeGI@N) / mean(ptGI@N) — both noisy at low
#       N, both converge to same truth.
#   (B) Truth-anchored: mean(cascadeGI@N) / mean(ptGI@2048) — uses the
#       highest-N PT capture as truth proxy. Cleaner separation of
#       convergence from noise.
#
# Pre-committed verdict bands.
#
# BAND 1: Asymptotic ratio (analysis B at N=2048):
#   ratio_2048 = mean(cascadeGI@2048) / mean(ptGI@2048)
#     CV1_CASCADE_NEAR_PT      -> ratio ∈ [0.85, 1.15]
#     CV1_CASCADE_DIM_MILD     -> ratio ∈ [0.60, 0.85)
#     CV1_CASCADE_DIM_MODERATE -> ratio ∈ [0.30, 0.60)  (matches v1.3.1
#                                  cam0=0.474 on cornell-orig-alcove)
#     CV1_CASCADE_DIM_SEVERE   -> ratio ∈ (0, 0.30)
#     CV1_CASCADE_BRIGHT       -> ratio > 1.15
#
# BAND 2: Convergence trend (Δratio = ratio_2048 − ratio_128, analysis B):
#     CV1_TIGHT_CONVERGENCE -> |Δ| ≤ 0.05 (cascade ~converged by N=128;
#                              extra frames don't shift the gap)
#     CV1_SLOW_CONVERGENCE  -> |Δ| ∈ (0.05, 0.20]
#     CV1_FAST_CONVERGENCE  -> |Δ| > 0.20 toward 1.0 (extra frames close
#                              the gap substantially)
#     CV1_DIVERGING         -> ratio_2048 farther from 1.0 than ratio_128
#                              (extra frames make things WORSE)
#
# BAND 3: PT self-convergence sanity (analysis A vs B agreement at low N):
#   pt_drift = mean(ptGI@128) / mean(ptGI@2048)
#     CV1_PT_WELL_CONVERGED_AT_128 -> |pt_drift − 1| ≤ 0.10 (PT already
#                                       essentially converged at N=128;
#                                       (A) and (B) numerics will agree)
#     CV1_PT_STILL_CONVERGING      -> |pt_drift − 1| > 0.10 (PT itself
#                                       still drifting; only (B) numerics
#                                       are trustworthy)
#
# Capture cost: 5 captures × cornell @ ~2-15s/frame depending on N.
# Estimate: 128*0.04 + 256*0.04 + 512*0.04 + 1024*0.04 + 2048*0.04 ≈
# 3968 * 0.04s = 159s ≈ 2.6 min. Bound by mode-17 PT cost per frame.

$exe    = "./build/RadianceCascades3D.exe"
$outDir = "tools/v20_convergence/captures_cv1"
$frameList = @(128, 256, 512, 1024, 2048)
$cam = 0
$camFile = "tools/v20_pre_measurement/cameras.json"

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }

function Capture {
    param([int]$N)
    $tag  = "cv1_cornell_cam${cam}_mbon_g100_hyb0_N$('{0:D4}' -f $N)_m17"
    $base = "${tag}.png"
    Write-Host "[cv1] $tag (N=$N frames, MB-ON g=1.0, hybrid OFF, cornell)"
    $argList = @(
        "--load-obj=cornell",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=1.0",
        "--use-hybrid=0",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--use-probe-jitter=1",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$N",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|render mode|load-obj|multi-bounce|use-hybrid" |
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
Write-Host "===== v2.0 CV1 absolute cascade-vs-PT convergence (cornell, cam0, MB-ON g=1.0, hybrid OFF) ====="
foreach ($N in $frameList) {
    Capture -N $N
}
$elapsed = (Get-Date) - $tStart
Write-Host "[done] capture in $($elapsed.TotalMinutes.ToString('F1')) min"

Write-Host ""
Write-Host "===== Analysis (analyze_cv1.py) ====="
$json = "$outDir/cv1_results.json"
python tools/v20_convergence/analyze_cv1.py $outDir $json
