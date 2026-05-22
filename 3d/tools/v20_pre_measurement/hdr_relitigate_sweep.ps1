# MBRC v2.0-pre HDR re-litigation sweep (doc/7/hdr_exr_metric_impl.md sec 4.1).
#
# Re-runs (alpha) merge-mode, (beta) MB-gain, and (gamma) angular-bin sweeps
# against the HDR-EXR honest metric. All three were REJECT-class under LDR
# (saturation-band classifier on colormap divisor=0.2). HDR replay of (delta)
# already showed the LDR DELTA_REJECT was a measurement artifact; the other
# three must be re-checked before the named-hypothesis tree can be declared
# exhausted.
#
# Each sweep matches the original LDR config exactly except:
#   - render-mode = 17  (cascade GI-only display path that triggers PT dispatch)
#   - --screenshot-exr=1  (dumps cascade_gi / pt_full / pt_direct EXR sidecars)
#   - N = 32 held constant (no need to span N -- (delta) HDR already covered)
#
# Totals: alpha 5x2 + beta 5x2 + gamma 3x2 = 26 captures.
# Frames=512 baseline. Cornell-orig-alcove. Seed=0 (bug-230 still open).

$exe       = "./build/RadianceCascades3D.exe"
$camFile   = "tools/v20_pre_measurement/cameras.json"
$frames    = 512
$cameras   = @(0, 2)

# ===== (alpha) merge-mode =====
$alphaDir  = "tools/v20_pre_measurement/captures_hdr_alpha"
# (name, dirMerge, dirBilinear, spatialTrilinear)
$alphaCfgs = @(
    @("M0_baseline",     1, 1, 1),
    @("M1_no_bilin",     1, 0, 1),
    @("M2_iso_merge",    0, 1, 1),
    @("M3_no_spatialtri",1, 1, 0),
    @("M4_iso_nearest",  0, 0, 1)
)

# ===== (beta) MB-gain =====
$betaDir   = "tools/v20_pre_measurement/captures_hdr_beta"
$betaGains = @(0.5, 1.0, 1.5, 2.0, 3.0)

# ===== (gamma) angular-bin uniform-D =====
$gammaDir  = "tools/v20_pre_measurement/captures_hdr_gamma"
$gammaDs   = @(4, 8, 16)

foreach ($d in @($alphaDir, $betaDir, $gammaDir)) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force $d | Out-Null }
}

function MoveSidecars {
    param([string]$tag, [string]$outDir)
    foreach ($suffix in @(".png", "_cascade_gi.exr", "_pt_full.exr", "_pt_direct.exr")) {
        $src = "${tag}${suffix}"
        if (Test-Path $src) {
            Move-Item -Force $src "$outDir/$src"
        } else {
            Write-Host "  WARN: missing $src"
        }
    }
}

function CaptureAlpha {
    param([string]$cfgName, [int]$dm, [int]$db, [int]$st, [int]$cam)
    $tag  = "cam${cam}_${cfgName}_m17"
    $base = "${tag}.png"
    Write-Host "[alpha] $tag (dm=$dm db=$db st=$st)"
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
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|useDirectional|useDirBilinear|useSpatial" |
        ForEach-Object { Write-Host "  $_" }
    MoveSidecars -tag $tag -outDir $alphaDir
}

function CaptureBeta {
    param([double]$gain, [int]$cam)
    $gainTag = "g$('{0:D3}' -f [int]($gain * 100))"
    $tag  = "cam${cam}_${gainTag}_m17"
    $base = "${tag}.png"
    Write-Host "[beta] $tag (gain=$gain)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--use-multi-bounce=1",
        "--multi-bounce-gain=$gain",
        "--use-hybrid=0",
        "--cascade-scaled-dir-res=1",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|multi-bounce|MultiBounce" |
        ForEach-Object { Write-Host "  $_" }
    MoveSidecars -tag $tag -outDir $betaDir
}

function CaptureGamma {
    param([int]$D, [int]$cam)
    $tag  = "cam${cam}_d$('{0:D2}' -f $D)_m17"
    $base = "${tag}.png"
    Write-Host "[gamma] $tag (D=$D uniform)"
    $argList = @(
        "--load-obj=cornell-orig-alcove",
        "--measurement-cameras-file=$camFile",
        "--measurement-camera=$cam",
        "--cascade-dir-res=$D",
        "--cascade-scaled-dir-res=0",
        "--use-multi-bounce=0",
        "--use-hybrid=0",
        "--noise-seed-offset=0",
        "--render-mode=17",
        "--screenshot-exr=1",
        "--auto-capture-delay=0",
        "--exit-frames=$frames",
        "--screenshot=$base"
    )
    & $exe @argList |
        Select-String -Pattern "hdr-exr|screenshot saved|cascade-dir-res|cascade-scaled-dir-res" |
        ForEach-Object { Write-Host "  $_" }
    MoveSidecars -tag $tag -outDir $gammaDir
}

$tStart = Get-Date

Write-Host "===== (alpha) merge-mode HDR re-litigation ====="
$i = 0; $total = $alphaCfgs.Count * $cameras.Count
foreach ($cfg in $alphaCfgs) {
    foreach ($cam in $cameras) {
        $i++
        Write-Host "----- alpha [$i/$total] -----"
        CaptureAlpha -cfgName $cfg[0] -dm $cfg[1] -db $cfg[2] -st $cfg[3] -cam $cam
    }
}

Write-Host "===== (beta) MB-gain HDR re-litigation ====="
$i = 0; $total = $betaGains.Count * $cameras.Count
foreach ($g in $betaGains) {
    foreach ($cam in $cameras) {
        $i++
        Write-Host "----- beta [$i/$total] -----"
        CaptureBeta -gain $g -cam $cam
    }
}

Write-Host "===== (gamma) angular-bin HDR re-litigation ====="
$i = 0; $total = $gammaDs.Count * $cameras.Count
foreach ($D in $gammaDs) {
    foreach ($cam in $cameras) {
        $i++
        Write-Host "----- gamma [$i/$total] -----"
        CaptureGamma -D $D -cam $cam
    }
}

$elapsed = (Get-Date) - $tStart
Write-Host "[done] HDR re-litigation sweep complete in $($elapsed.TotalMinutes.ToString('F1')) min"
