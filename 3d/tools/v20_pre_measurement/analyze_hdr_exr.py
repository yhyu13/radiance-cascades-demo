"""MBRC v2.0-pre HDR-EXR honest-metric analyzer (doc/7/hdr_exr_metric_impl.md).

Reads cascade_gi / pt_full / pt_direct EXR triplets emitted by
--screenshot-exr=1 (render-mode=17) and computes per-pixel HDR ratio
statistics that bypass the LDR-PNG saturation-band classifier used in
the original (delta) probe-density sweep (captures_delta/).

Per-pixel signed relative error r = (cascadeGI - ptGI) / max(ptGI, eps)
where ptGI = pt_full - pt_direct. cascadeGI is downsampled 2x2-avg to
match PT's half-viewport size before differencing.

Output: per-(cam, N) quantile rows + a verdict-style summary so the
analyst can compare directly with the LDR DELTA_REJECT call. If the
HDR p50 stays close to 0 and tails are narrow, the LDR-floor hypothesis
is CONFIRMED; if HDR shows wide tails or persistent bias, LDR was hiding
a real (delta) effect and DELTA_REJECT must be revisited.

Usage: python tools/v20_pre_measurement/analyze_hdr_exr.py
"""

from __future__ import annotations
import os, json, glob, sys
from dataclasses import dataclass
from typing import Optional
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR  (and ensure imageio[freeimage] if you "
          "prefer that path)", file=sys.stderr)
    sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
CAP  = os.path.join(HERE, "captures_hdr_exr")
OUT  = os.path.join(HERE, "hdr_exr_results.json")

EPS_PT = 1e-3  # luminance floor below which ptGI is considered "no signal"
                # (avoids division blow-up in dark sky / background pixels)


def read_exr_rgb(path: str) -> np.ndarray:
    f = OpenEXR.InputFile(path)
    h = f.header()
    dw = h["dataWindow"]
    w  = dw.max.x - dw.min.x + 1
    H  = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    chans = {c: np.frombuffer(f.channel(c, pt), dtype=np.float32).reshape(H, w)
             for c in "RGB"}
    return np.dstack([chans["R"], chans["G"], chans["B"]])


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def downsample_2x2_avg(a: np.ndarray) -> np.ndarray:
    """Average 2x2 blocks. Assumes even H,W (true for 1280x720 -> 640x360)."""
    H, W = a.shape[:2]
    H2, W2 = H // 2, W // 2
    a = a[: H2 * 2, : W2 * 2]
    # reshape -> (H2, 2, W2, 2, C) -> mean over (1,3)
    if a.ndim == 3:
        return a.reshape(H2, 2, W2, 2, -1).mean(axis=(1, 3))
    return a.reshape(H2, 2, W2, 2).mean(axis=(1, 3))


@dataclass
class HdrStats:
    cam: int
    N: int
    valid_pixels: int
    total_pixels: int
    p05_rel: float        # 5th percentile of signed relative error
    p50_rel: float
    p95_rel: float
    abs_p50_rel: float    # median |relErr|
    abs_p95_rel: float
    mean_pt_lum: float    # mean ptGI luminance over valid pixels
    mean_cascade_lum: float
    cascade_dim_count: int   # cascade < 0.5x PT (under-bright)
    cascade_bright_count: int  # cascade > 2x PT (over-bright)


def analyze(cam: int, N: int) -> Optional[HdrStats]:
    stem = os.path.join(CAP, f"cam{cam}_N{N:02d}_m17")
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam} N={N}: missing {k} ({p})")
            return None

    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])

    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    if casc.shape[:2] != ptF.shape[:2]:
        print(f"[skip] cam{cam} N={N}: shape mismatch after downsample"
              f" casc={casc.shape} ptF={ptF.shape}")
        return None

    ptGI = np.clip(ptF - ptD, 0.0, None)   # negative would be sampling noise
    Lpt  = lum(ptGI)
    Lc   = lum(casc)

    valid = Lpt > EPS_PT
    nVal  = int(valid.sum())
    nTot  = int(Lpt.size)
    if nVal == 0:
        print(f"[skip] cam{cam} N={N}: no valid PT pixels (Lpt<= {EPS_PT})")
        return None

    rel = (Lc[valid] - Lpt[valid]) / Lpt[valid]
    absRel = np.abs(rel)
    ratio  = Lc[valid] / Lpt[valid]

    return HdrStats(
        cam=cam, N=N, valid_pixels=nVal, total_pixels=nTot,
        p05_rel=float(np.quantile(rel, 0.05)),
        p50_rel=float(np.quantile(rel, 0.50)),
        p95_rel=float(np.quantile(rel, 0.95)),
        abs_p50_rel=float(np.quantile(absRel, 0.50)),
        abs_p95_rel=float(np.quantile(absRel, 0.95)),
        mean_pt_lum=float(Lpt[valid].mean()),
        mean_cascade_lum=float(Lc[valid].mean()),
        cascade_dim_count=int((ratio < 0.5).sum()),
        cascade_bright_count=int((ratio > 2.0).sum()),
    )


def main() -> int:
    cams = [0, 2]
    Ns   = [16, 32, 64]

    rows: list[HdrStats] = []
    for cam in cams:
        for N in Ns:
            s = analyze(cam, N)
            if s is not None:
                rows.append(s)

    if not rows:
        print("ERROR: no captures found", file=sys.stderr)
        return 1

    # Report
    print()
    print(f"{'cam':>3} {'N':>3} {'valid%':>7} "
          f"{'p05_rel':>9} {'p50_rel':>9} {'p95_rel':>9} "
          f"{'|p50|':>7} {'|p95|':>7} "
          f"{'meanPT':>8} {'meanCasc':>9} "
          f"{'dim%':>6} {'bright%':>7}")
    print("-" * 110)
    for s in rows:
        v = 100.0 * s.valid_pixels / max(1, s.total_pixels)
        d = 100.0 * s.cascade_dim_count / max(1, s.valid_pixels)
        b = 100.0 * s.cascade_bright_count / max(1, s.valid_pixels)
        print(f"{s.cam:>3} {s.N:>3} {v:>6.1f}% "
              f"{s.p05_rel:>+9.3f} {s.p50_rel:>+9.3f} {s.p95_rel:>+9.3f} "
              f"{s.abs_p50_rel:>7.3f} {s.abs_p95_rel:>7.3f} "
              f"{s.mean_pt_lum:>8.4f} {s.mean_cascade_lum:>9.4f} "
              f"{d:>5.1f}% {b:>6.1f}%")

    # Save raw
    out = {
        "epsilon_pt_luminance": EPS_PT,
        "downsample": "cascade 2x2 avg to match PT 640x360",
        "ratio_definition": "(cascadeGI - ptGI) / max(ptGI, eps); ptGI = pt_full - pt_direct",
        "rows": [s.__dict__ for s in rows],
    }
    with open(OUT, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT}")

    # Verdict-style summary -- focus on the analog of the LDR "delta-area"
    # signal: do (cam, N) trends look like LDR DELTA_REJECT (all tight, no
    # leverage), or does HDR reveal a hidden cam2 vs cam0 asymmetry that
    # LDR clamped away?
    print()
    print("=== HDR-vs-LDR cross-check ===")
    print("LDR DELTA_REJECT (capture_delta): cam0 spans 26.5-27.9% Delta-area; "
          "cam2 19.5-20.4%. All within +/-10% of N=32 -> reject.")
    print()
    print("HDR signed-p50 by cam, N (zero = symmetric):")
    for cam in cams:
        cells = []
        for N in Ns:
            r = next((x for x in rows if x.cam == cam and x.N == N), None)
            cells.append(f"N={N}: p50={r.p50_rel:+.3f}" if r else f"N={N}: --")
        print(f"  cam{cam}: " + "  ".join(cells))
    print()
    print("HDR |p95| (max-magnitude pixel error) by cam, N:")
    for cam in cams:
        cells = []
        for N in Ns:
            r = next((x for x in rows if x.cam == cam and x.N == N), None)
            cells.append(f"N={N}: |p95|={r.abs_p95_rel:.3f}" if r else f"N={N}: --")
        print(f"  cam{cam}: " + "  ".join(cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())
