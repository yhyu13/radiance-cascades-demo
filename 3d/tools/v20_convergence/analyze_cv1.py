"""MBRC v2.0 CV1 analyzer — absolute cascade-vs-PT convergence sweep.

Reads (cascade_gi, pt_full, pt_direct) EXR triplets emitted by render-mode 17
at multiple frame counts N ∈ {128, 256, 512, 1024, 2048} under one
configuration (cornell, cam0, MB-ON g=1.0, hybrid OFF) and reports:

  (A) self-paired ratio per N:    mean(cascadeGI@N) / mean(ptGI@N)
  (B) truth-anchored ratio per N: mean(cascadeGI@N) / mean(ptGI@N_max)
  PT self-drift:                  mean(ptGI@N) / mean(ptGI@N_max)

Pre-committed verdict bands (header of cv1_capture.ps1):

  BAND 1 (asymptotic ratio, analysis B at N_max):
    CV1_CASCADE_NEAR_PT      ratio ∈ [0.85, 1.15]
    CV1_CASCADE_DIM_MILD     ratio ∈ [0.60, 0.85)
    CV1_CASCADE_DIM_MODERATE ratio ∈ [0.30, 0.60)
    CV1_CASCADE_DIM_SEVERE   ratio ∈ (0,    0.30)
    CV1_CASCADE_BRIGHT       ratio > 1.15

  BAND 2 (convergence trend, |Δratio| from N_min to N_max under analysis B):
    CV1_TIGHT_CONVERGENCE   |Δ| ≤ 0.05
    CV1_SLOW_CONVERGENCE    |Δ| ∈ (0.05, 0.20]
    CV1_FAST_CONVERGENCE    |Δ| > 0.20  (toward 1.0)
    CV1_DIVERGING           ratio@N_max farther from 1.0 than ratio@N_min

  BAND 3 (PT self-convergence sanity):
    CV1_PT_WELL_CONVERGED_AT_N_MIN  |pt_drift − 1| ≤ 0.10
    CV1_PT_STILL_CONVERGING         > 0.10

Usage: python tools/v20_convergence/analyze_cv1.py <capture_dir> <out_json>
"""

from __future__ import annotations
import os, sys, json, glob, re
from dataclasses import dataclass, asdict
from typing import Optional
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr)
    sys.exit(2)

EPS_PT = 1e-3


def read_exr_rgb(path: str) -> np.ndarray:
    f = OpenEXR.InputFile(path)
    h = f.header()
    dw = h["dataWindow"]
    w = dw.max.x - dw.min.x + 1
    H = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    chans = {c: np.frombuffer(f.channel(c, pt), dtype=np.float32).reshape(H, w)
             for c in "RGB"}
    return np.dstack([chans["R"], chans["G"], chans["B"]])


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def downsample_2x2_avg(a: np.ndarray) -> np.ndarray:
    H, W = a.shape[:2]
    H2, W2 = H // 2, W // 2
    a = a[: H2 * 2, : W2 * 2]
    if a.ndim == 3:
        return a.reshape(H2, 2, W2, 2, -1).mean(axis=(1, 3))
    return a.reshape(H2, 2, W2, 2).mean(axis=(1, 3))


@dataclass
class Frame:
    N: int
    valid_pixels: int
    total_pixels: int
    mean_pt_lum: float
    mean_cascade_lum: float
    p05_rel: float
    p50_rel: float
    p95_rel: float
    abs_p50_rel: float
    abs_p95_rel: float
    cascade_dim_count: int      # cascade < 0.5x PT
    cascade_bright_count: int   # cascade > 2.0x PT


def analyze_one(cap_dir: str, N: int) -> Optional[Frame]:
    # match cv1_capture.ps1 tag format
    stem = os.path.join(cap_dir,
        f"cv1_cornell_cam0_mbon_g100_hyb0_N{N:04d}_m17")
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] N={N}: missing {k} ({p})")
            return None

    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])

    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    if casc.shape[:2] != ptF.shape[:2]:
        print(f"[skip] N={N}: shape mismatch after downsample"
              f" casc={casc.shape} ptF={ptF.shape}")
        return None

    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt  = lum(ptGI)
    Lc   = lum(casc)

    valid = Lpt > EPS_PT
    nVal  = int(valid.sum())
    nTot  = int(Lpt.size)
    if nVal == 0:
        return None

    rel = (Lc[valid] - Lpt[valid]) / Lpt[valid]
    absRel = np.abs(rel)
    ratio = Lc[valid] / Lpt[valid]

    return Frame(
        N=N, valid_pixels=nVal, total_pixels=nTot,
        mean_pt_lum=float(Lpt[valid].mean()),
        mean_cascade_lum=float(Lc[valid].mean()),
        p05_rel=float(np.quantile(rel, 0.05)),
        p50_rel=float(np.quantile(rel, 0.50)),
        p95_rel=float(np.quantile(rel, 0.95)),
        abs_p50_rel=float(np.quantile(absRel, 0.50)),
        abs_p95_rel=float(np.quantile(absRel, 0.95)),
        cascade_dim_count=int((ratio < 0.5).sum()),
        cascade_bright_count=int((ratio > 2.0).sum()),
    )


def band1_asymptotic(ratio: float) -> str:
    if ratio > 1.15:                return "CV1_CASCADE_BRIGHT"
    if ratio >= 0.85:               return "CV1_CASCADE_NEAR_PT"
    if ratio >= 0.60:               return "CV1_CASCADE_DIM_MILD"
    if ratio >= 0.30:               return "CV1_CASCADE_DIM_MODERATE"
    if ratio > 0.0:                 return "CV1_CASCADE_DIM_SEVERE"
    return "CV1_CASCADE_INVERTED"


def band2_trend(ratio_min: float, ratio_max: float) -> str:
    dist_min = abs(ratio_min - 1.0)
    dist_max = abs(ratio_max - 1.0)
    if dist_max > dist_min + 0.01:
        return "CV1_DIVERGING"
    d = abs(ratio_max - ratio_min)
    if d <= 0.05:  return "CV1_TIGHT_CONVERGENCE"
    if d <= 0.20:  return "CV1_SLOW_CONVERGENCE"
    return "CV1_FAST_CONVERGENCE"


def band3_pt(pt_drift: float) -> str:
    if abs(pt_drift - 1.0) <= 0.10:
        return "CV1_PT_WELL_CONVERGED_AT_N_MIN"
    return "CV1_PT_STILL_CONVERGING"


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: analyze_cv1.py <capture_dir> <out_json>", file=sys.stderr)
        return 2
    cap_dir = sys.argv[1]
    out_json = sys.argv[2]

    Ns = [128, 256, 512, 1024, 2048]
    frames: list[Frame] = []
    for N in Ns:
        f = analyze_one(cap_dir, N)
        if f is not None:
            frames.append(f)

    if not frames:
        print("ERROR: no frames analyzed", file=sys.stderr)
        return 1

    N_min = min(f.N for f in frames)
    N_max = max(f.N for f in frames)
    f_max = next(f for f in frames if f.N == N_max)
    f_min = next(f for f in frames if f.N == N_min)

    # Analysis A: self-paired ratio per N
    # Analysis B: truth-anchored ratio per N (pt @ N_max as truth)
    rows = []
    for f in frames:
        rA = f.mean_cascade_lum / f.mean_pt_lum if f.mean_pt_lum > 0 else 0.0
        rB = f.mean_cascade_lum / f_max.mean_pt_lum if f_max.mean_pt_lum > 0 else 0.0
        drift = f.mean_pt_lum / f_max.mean_pt_lum if f_max.mean_pt_lum > 0 else 0.0
        rows.append({
            "N": f.N,
            "valid_pct": 100.0 * f.valid_pixels / max(1, f.total_pixels),
            "mean_cascade": f.mean_cascade_lum,
            "mean_pt": f.mean_pt_lum,
            "ratio_self": rA,
            "ratio_truth": rB,
            "pt_drift": drift,
            "p50_rel": f.p50_rel,
            "abs_p50_rel": f.abs_p50_rel,
            "abs_p95_rel": f.abs_p95_rel,
            "cascade_dim_pct": 100.0 * f.cascade_dim_count / max(1, f.valid_pixels),
            "cascade_bright_pct": 100.0 * f.cascade_bright_count / max(1, f.valid_pixels),
        })

    # Verdicts
    rB_max = rows[-1]["ratio_truth"]
    rB_min = rows[0]["ratio_truth"]
    pt_drift_min = rows[0]["pt_drift"]
    verdict_band1 = band1_asymptotic(rB_max)
    verdict_band2 = band2_trend(rB_min, rB_max)
    verdict_band3 = band3_pt(pt_drift_min)

    # Console report
    print()
    print("=" * 90)
    print("CV1 absolute cascade-vs-PT convergence  (cornell, cam0, MB-ON g=1.0, hybrid OFF)")
    print("=" * 90)
    print(f"{'N':>5} {'valid%':>7} {'meanCasc':>10} {'meanPT':>10} "
          f"{'ratio_A':>8} {'ratio_B':>8} {'pt_drift':>9} "
          f"{'|p50|':>7} {'|p95|':>8} {'dim%':>6} {'br%':>5}")
    print("-" * 90)
    for r in rows:
        print(f"{r['N']:>5} {r['valid_pct']:>6.1f}% "
              f"{r['mean_cascade']:>10.4f} {r['mean_pt']:>10.4f} "
              f"{r['ratio_self']:>8.4f} {r['ratio_truth']:>8.4f} "
              f"{r['pt_drift']:>9.4f} "
              f"{r['abs_p50_rel']:>7.3f} {r['abs_p95_rel']:>8.3f} "
              f"{r['cascade_dim_pct']:>5.1f}% {r['cascade_bright_pct']:>4.1f}%")
    print()
    print(f"BAND 1 (asymptotic @ N={N_max}):    ratio_B = {rB_max:.4f}  -> {verdict_band1}")
    print(f"BAND 2 (trend N={N_min}->{N_max}):  ratio_B {rB_min:.4f} -> {rB_max:.4f}  "
          f"(|Δ|={abs(rB_max-rB_min):.4f})  -> {verdict_band2}")
    print(f"BAND 3 (PT self-drift @ N={N_min}): pt_drift = {pt_drift_min:.4f}  -> {verdict_band3}")
    print()

    out = {
        "scene": "cornell",
        "cam": 0,
        "mb": "on", "mb_gain": 1.0, "hybrid": "off",
        "epsilon_pt_luminance": EPS_PT,
        "frame_counts": [f.N for f in frames],
        "frames": [asdict(f) for f in frames],
        "rows": rows,
        "verdict_band1_asymptotic_ratio_B_at_N_max": {
            "ratio": rB_max,
            "band": verdict_band1,
        },
        "verdict_band2_trend_ratio_B": {
            "ratio_at_N_min": rB_min,
            "ratio_at_N_max": rB_max,
            "delta_abs": abs(rB_max - rB_min),
            "band": verdict_band2,
        },
        "verdict_band3_pt_self_drift": {
            "pt_drift_at_N_min": pt_drift_min,
            "band": verdict_band3,
        },
    }
    with open(out_json, "w") as f:
        json.dump(out, f, indent=2)
    print(f"[wrote] {out_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
