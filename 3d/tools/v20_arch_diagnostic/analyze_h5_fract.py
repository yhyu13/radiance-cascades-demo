"""MBRC v2.0 (h.c) Probe-cell fract(pg) cam0 vs cam2 distribution analysis.

Reads two mode-8 PNGs and computes per-pixel distance-from-cell-center:
    d = max(|f.x-0.5|, |f.y-0.5|, |f.z-0.5|),  where f = (R,G,B)/255
d in [0, 0.5]: 0 at probe-cell center, 0.5 at any-axis boundary.

Surface-hit pixels filtered as `R+G+B > 10` (sky pixels are pure black
because mode 8 returns early on surface hit; the remaining shader
path leaves sky black for this render).

Pre-committed verdict bands:
    mean_d(cam2) - mean_d(cam0):
      >= +0.05  -> CAM2_OVERSAMPLES_BOUNDARIES
      in [-0.05, +0.05] -> CAM2_PROBE_COVERAGE_NEUTRAL
      <= -0.05 -> CAM2_OVERSAMPLES_CELL_CENTERS
"""

from __future__ import annotations
import os, json, sys
import numpy as np

try:
    from PIL import Image
except ImportError:
    print("ERROR: pip install pillow", file=sys.stderr); sys.exit(2)


HERE = os.path.dirname(os.path.abspath(__file__))
CAP  = os.path.join(HERE, "captures_h5_fract")
OUT  = os.path.join(HERE, "h5_fract_results.json")


def analyze(cam: int) -> dict:
    path = os.path.join(CAP, f"alcove_cam{cam}_M0_b2_mboff_m8.png")
    img = np.array(Image.open(path).convert("RGB"))  # (H, W, 3) uint8
    H, W, _ = img.shape
    f = img.astype(np.float32) / 255.0  # fract values in [0, 1]
    # Surface mask: sky pixels are pure black (no shading happens before
    # mode-8 early return). Filter R+G+B > 10/255 ~ 0.04
    rgbsum = f.sum(axis=-1)
    surface = rgbsum > (10.0 / 255.0)
    n_surf = int(surface.sum())
    if n_surf == 0:
        return {"cam": cam, "error": "no surface pixels"}

    fs = f[surface]  # (N, 3) in [0, 1]
    # Distance from cell center per axis, then max-norm
    d_axis = np.abs(fs - 0.5)  # (N, 3) in [0, 0.5]
    d = d_axis.max(axis=-1)    # (N,) in [0, 0.5]

    # Histogram of d in 10 bins over [0, 0.5]
    bins = np.linspace(0.0, 0.5, 11)
    hist, _ = np.histogram(d, bins=bins)
    hist_pct = (hist.astype(np.float32) / n_surf * 100.0).tolist()

    return {
        "cam": cam,
        "image_shape": [H, W],
        "n_surface": n_surf,
        "n_total":   H * W,
        "surface_pct": float(n_surf) / float(H * W) * 100.0,
        "mean_d":  float(d.mean()),
        "p25_d":   float(np.percentile(d, 25)),
        "median_d":float(np.percentile(d, 50)),
        "p75_d":   float(np.percentile(d, 75)),
        "p95_d":   float(np.percentile(d, 95)),
        "mean_f": [float(fs[:, 0].mean()),
                   float(fs[:, 1].mean()),
                   float(fs[:, 2].mean())],
        "hist_d_bins":   bins.tolist(),
        "hist_d_pct":    hist_pct,  # % of surface pixels per bin
    }


def classify(delta_mean_d: float) -> tuple[str, str]:
    if delta_mean_d >= 0.05:
        return "CAM2_OVERSAMPLES_BOUNDARIES", (
            f"cam2 mean_d - cam0 mean_d = {delta_mean_d:+.4f} >= +0.05. "
            f"cam2 systematically samples closer to probe-cell boundaries; "
            f"trilinear weight aliasing is a credible driver of cam2 under-supply.")
    if delta_mean_d <= -0.05:
        return "CAM2_OVERSAMPLES_CELL_CENTERS", (
            f"cam2 mean_d - cam0 mean_d = {delta_mean_d:+.4f} <= -0.05. "
            f"cam2 samples closer to cell centers than cam0; OPPOSITE of "
            f"prediction. The cam2 under-supply is unlikely to come from "
            f"probe-cell boundary aliasing.")
    return "CAM2_PROBE_COVERAGE_NEUTRAL", (
        f"cam2 mean_d - cam0 mean_d = {delta_mean_d:+.4f} in [-0.05, +0.05]. "
        f"Probe-cell fract distribution is statistically similar at both "
        f"cams; cam0/cam2 spread is NOT driven by probe-cell spatial "
        f"aliasing. Pivot to per-direction-bin sampling hypothesis or "
        f"atlas-content compare.")


def main() -> int:
    rows = [analyze(0), analyze(2)]
    print()
    print(f"{'cam':>3} {'shape':<12} {'surfPct':>8} {'mean_d':>9} "
          f"{'p25':>7} {'p50':>7} {'p75':>7} {'p95':>7}")
    print("-" * 70)
    for r in rows:
        if "error" in r:
            print(f"cam{r['cam']}: {r['error']}"); continue
        sh = f"{r['image_shape'][0]}x{r['image_shape'][1]}"
        print(f"{r['cam']:>3} {sh:<12} {r['surface_pct']:>7.2f}% "
              f"{r['mean_d']:>9.4f} "
              f"{r['p25_d']:>7.4f} {r['median_d']:>7.4f} "
              f"{r['p75_d']:>7.4f} {r['p95_d']:>7.4f}")

    print()
    print("=== Mean fract per axis (RGB = probe-grid X/Y/Z) ===")
    for r in rows:
        if "error" in r: continue
        mf = r["mean_f"]
        print(f"  cam{r['cam']} mean_f = ({mf[0]:.4f}, {mf[1]:.4f}, {mf[2]:.4f})  "
              f"distance from (0.5,0.5,0.5) = "
              f"{max(abs(mf[0]-0.5), abs(mf[1]-0.5), abs(mf[2]-0.5)):.4f}")

    print()
    print("=== Histogram of d=max(|f-0.5|) per cam (% of surface pixels) ===")
    bins = rows[0]["hist_d_bins"]
    print(f"{'bin range':<20} {'cam0%':>8} {'cam2%':>8}")
    print("-" * 40)
    for i, (lo, hi) in enumerate(zip(bins[:-1], bins[1:])):
        c0 = rows[0]["hist_d_pct"][i]
        c2 = rows[1]["hist_d_pct"][i]
        print(f"  [{lo:.3f}, {hi:.3f})    {c0:>7.2f}  {c2:>7.2f}")

    # Verdict
    d0 = rows[0]["mean_d"]
    d2 = rows[1]["mean_d"]
    delta = d2 - d0
    verdict, detail = classify(delta)
    print()
    print(f"VERDICT (probe-cell coverage symmetry): {verdict}")
    print(f"detail: {detail}")

    with open(OUT, "w") as f:
        json.dump({
            "rows": rows,
            "delta_mean_d_cam2_minus_cam0": delta,
            "verdict": verdict,
            "verdict_detail": detail,
        }, f, indent=2)
    print(f"\n[wrote] {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
