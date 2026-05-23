"""MBRC v2.0 absolute-residual analyzer (doc/7/v20_cam2_asymmetry_diagnostic_impl.md section 5).

Reads cascade_gi / pt_full / pt_direct EXR triplets at cam0+cam2 captured
under the NEW engine defaults (alcove_cam{0,2}_newdefault_m17_*.exr in
tools/v20_arch_diagnostic/captures_abs_residual/) and computes the
ABSOLUTE-radiance signed residual Delta = cascadeGI - ptGI (per pixel,
per RGB channel; aggregated via luminance scalar).

Goal: produce a measured verdict on hypothesis (d) from the asymmetry
diagnostic -- basis-representation error -- with the pre-committed rule:

  |Sum+| approx |Sum-| (within 30%)  -> BASIS-ERROR CONFIRMED
      Cascade re-distributes radiance without losing it: peaks smeared
      into floor (pink) balance valleys lost from occluded surfaces (blue).
      Next architectural step: thin-merge shader variant.

  |Sum+| << |Sum-| (Sum+ < 0.5 * Sum-)  -> NET UNDER-BRIGHT (energy loss)
      Cascade is net-under-bright; the apparent over-bright pink is local
      re-distribution but the system loses energy overall.
      Next step: energy audit at bake time.

  |Sum+| >> |Sum-| (Sum+ > 2 * Sum-)  -> NET OVER-BRIGHT (energy fab)
      Cascade leaks energy from nowhere. Re-open the leak hypothesis.

Outputs:
- console table with Sum+, Sum-, |Sum+/Sum-| ratio, mean|Delta| per region,
  verdict per cam
- JSON dump (absolute_residual_results.json)
- 2-color signed-field PNG per cam (red = cascade > PT, blue = cascade < PT,
  brightness proportional to |Delta| luminance)

Usage: python tools/v20_arch_diagnostic/analyze_absolute_residual.py
"""

from __future__ import annotations
import os, json, sys
from dataclasses import dataclass, asdict
from typing import Optional
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr)
    sys.exit(2)

try:
    from PIL import Image
except ImportError:
    print("ERROR: pip install Pillow", file=sys.stderr)
    sys.exit(2)


HERE = os.path.dirname(os.path.abspath(__file__))
CAP  = os.path.join(HERE, "captures_abs_residual")
OUT_JSON = os.path.join(HERE, "absolute_residual_results.json")

EPS_PT = 1e-3   # luminance floor: pixels with ptGI below this are "no PT signal"
                # and excluded from Sum+/Sum- (background / unlit).


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
    H, W = a.shape[:2]
    H2, W2 = H // 2, W // 2
    a = a[: H2 * 2, : W2 * 2]
    if a.ndim == 3:
        return a.reshape(H2, 2, W2, 2, -1).mean(axis=(1, 3))
    return a.reshape(H2, 2, W2, 2).mean(axis=(1, 3))


@dataclass
class AbsResidual:
    cam: int
    valid_pixels: int
    total_pixels: int
    sum_pos: float           # sum over pixels where Delta > 0 of Delta (luminance units)
    sum_neg: float           # sum over pixels where Delta < 0 of |Delta|
    abs_ratio: float         # |Sum+ / Sum-|
    pos_count: int
    neg_count: int
    mean_pos: float          # mean of Delta over Delta>0 region
    mean_neg: float          # mean of |Delta| over Delta<0 region
    mean_pt: float           # mean ptGI luminance over valid pixels (scale ref)
    mean_cascade: float
    integrated_pt: float     # sum of ptGI luminance over valid pixels
    integrated_cascade: float
    verdict: str
    verdict_detail: str


def classify(sum_pos: float, sum_neg: float) -> tuple[str, str]:
    """Pre-committed verdict rule."""
    if sum_neg <= 1e-9 and sum_pos <= 1e-9:
        return "NULL", "no signal in either region"
    if sum_neg <= 1e-9:
        return "NET_OVER_BRIGHT", "all pixels over-bright (Sum- ~= 0)"
    if sum_pos <= 1e-9:
        return "NET_UNDER_BRIGHT", "all pixels under-bright (Sum+ ~= 0)"

    r = sum_pos / sum_neg
    if 0.70 <= r <= 1.30:
        return ("BASIS_ERROR_CONFIRMED",
                f"|Sum+/Sum-| = {r:.3f} in [0.70, 1.30] -> radiance re-distribution")
    if r < 0.50:
        return ("NET_UNDER_BRIGHT",
                f"|Sum+/Sum-| = {r:.3f} < 0.50 -> cascade loses energy")
    if r > 2.00:
        return ("NET_OVER_BRIGHT",
                f"|Sum+/Sum-| = {r:.3f} > 2.00 -> cascade fabricates energy")
    # In-between: lean
    if r < 1.0:
        return ("BORDERLINE_UNDER",
                f"|Sum+/Sum-| = {r:.3f} in [0.50, 0.70] -> leans under-bright")
    return ("BORDERLINE_OVER",
            f"|Sum+/Sum-| = {r:.3f} in [1.30, 2.00] -> leans over-bright")


def render_signed_png(delta_lum: np.ndarray, valid: np.ndarray,
                       out_path: str, scale: float) -> None:
    """Red where delta>0 (cascade>PT), blue where delta<0 (cascade<PT)."""
    H, W = delta_lum.shape
    img = np.zeros((H, W, 3), dtype=np.uint8)
    pos = (delta_lum > 0) & valid
    neg = (delta_lum < 0) & valid
    # Normalize by user-supplied scale (e.g. 99th percentile of |delta|)
    if scale <= 0:
        scale = 1.0
    pos_v = np.clip(delta_lum[pos] / scale, 0.0, 1.0)
    neg_v = np.clip(-delta_lum[neg] / scale, 0.0, 1.0)
    img_r = np.zeros((H, W), dtype=np.float32)
    img_b = np.zeros((H, W), dtype=np.float32)
    img_r[pos] = pos_v
    img_b[neg] = neg_v
    img[..., 0] = (img_r * 255.0).astype(np.uint8)
    img[..., 2] = (img_b * 255.0).astype(np.uint8)
    # Faint grey background where ptGI < eps (helps interpretability)
    bg = (~valid)
    img[bg] = [24, 24, 24]
    Image.fromarray(img, "RGB").save(out_path)


def analyze(cam: int) -> Optional[AbsResidual]:
    stem = os.path.join(CAP, f"alcove_cam{cam}_newdefault_m17")
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam}: missing {k} ({p})")
            return None

    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])

    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    if casc.shape[:2] != ptF.shape[:2]:
        print(f"[skip] cam{cam}: shape mismatch after downsample"
              f" casc={casc.shape} ptF={ptF.shape}")
        return None

    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt  = lum(ptGI)
    Lc   = lum(casc)
    Ld   = Lc - Lpt                       # signed delta luminance per pixel
    valid = Lpt > EPS_PT
    nVal  = int(valid.sum())
    nTot  = int(Lpt.size)
    if nVal == 0:
        print(f"[skip] cam{cam}: no valid PT pixels (Lpt<= {EPS_PT})")
        return None

    pos_mask = valid & (Ld > 0)
    neg_mask = valid & (Ld < 0)
    sum_pos  = float(Ld[pos_mask].sum())
    sum_neg  = float((-Ld[neg_mask]).sum())
    nPos = int(pos_mask.sum())
    nNeg = int(neg_mask.sum())
    mean_pos = float(Ld[pos_mask].mean()) if nPos else 0.0
    mean_neg = float((-Ld[neg_mask]).mean()) if nNeg else 0.0
    abs_ratio = sum_pos / sum_neg if sum_neg > 1e-9 else float("inf")
    verdict, detail = classify(sum_pos, sum_neg)

    # Signed PNG (scale = 99th-percentile of |delta| in valid region)
    scale = float(np.quantile(np.abs(Ld[valid]), 0.99)) if nVal > 0 else 1.0
    png_out = os.path.join(CAP, f"alcove_cam{cam}_signed_residual.png")
    render_signed_png(Ld, valid, png_out, scale)
    print(f"[png] cam{cam} signed-residual -> {png_out}"
          f" (norm scale = |Delta|99 = {scale:.5f})")

    return AbsResidual(
        cam=cam, valid_pixels=nVal, total_pixels=nTot,
        sum_pos=sum_pos, sum_neg=sum_neg, abs_ratio=abs_ratio,
        pos_count=nPos, neg_count=nNeg,
        mean_pos=mean_pos, mean_neg=mean_neg,
        mean_pt=float(Lpt[valid].mean()),
        mean_cascade=float(Lc[valid].mean()),
        integrated_pt=float(Lpt[valid].sum()),
        integrated_cascade=float(Lc[valid].sum()),
        verdict=verdict, verdict_detail=detail,
    )


def main() -> int:
    rows: list[AbsResidual] = []
    for cam in (0, 2):
        r = analyze(cam)
        if r is not None:
            rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr)
        return 1

    # Console table
    print()
    print(f"{'cam':>3} {'valid%':>7} "
          f"{'Sum+':>9} {'Sum-':>9} {'|+/-|':>7} "
          f"{'pos%':>6} {'neg%':>6} "
          f"{'mean+':>8} {'mean-':>8} "
          f"{'meanPT':>8} {'meanCasc':>9}")
    print("-" * 100)
    for r in rows:
        vp = 100.0 * r.valid_pixels / max(1, r.total_pixels)
        pp = 100.0 * r.pos_count / max(1, r.valid_pixels)
        np_ = 100.0 * r.neg_count / max(1, r.valid_pixels)
        ratio_s = f"{r.abs_ratio:7.3f}" if np.isfinite(r.abs_ratio) else "    inf"
        print(f"{r.cam:>3} {vp:>6.1f}% "
              f"{r.sum_pos:>9.3f} {r.sum_neg:>9.3f} {ratio_s} "
              f"{pp:>5.1f}% {np_:>5.1f}% "
              f"{r.mean_pos:>8.4f} {r.mean_neg:>8.4f} "
              f"{r.mean_pt:>8.4f} {r.mean_cascade:>9.4f}")

    print()
    print("=== VERDICTS (pre-committed rule) ===")
    print("  |Sum+/Sum-| in [0.70, 1.30] -> BASIS_ERROR_CONFIRMED")
    print("  |Sum+/Sum-| < 0.50          -> NET_UNDER_BRIGHT (energy loss)")
    print("  |Sum+/Sum-| > 2.00          -> NET_OVER_BRIGHT (energy fabricated)")
    print()
    for r in rows:
        print(f"  cam{r.cam}: {r.verdict}  -- {r.verdict_detail}")
    print()
    print(f"  integrated cascade / integrated PT (energy ratio):")
    for r in rows:
        e = r.integrated_cascade / r.integrated_pt if r.integrated_pt > 0 else float("nan")
        print(f"    cam{r.cam}: cascade={r.integrated_cascade:.3f}"
              f"  PT={r.integrated_pt:.3f}  ratio={e:.4f}")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "verdict_thresholds": {
            "basis_error_lo": 0.70, "basis_error_hi": 1.30,
            "net_under_lo":   0.50,
            "net_over_hi":    2.00,
        },
        "delta_definition": "Lc - Lpt; Lpt = lum(pt_full - pt_direct); cascade 2x2-avg downsampled to PT 640x360",
        "rows": [asdict(r) for r in rows],
    }
    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT_JSON}")
    print(f"[wrote] signed-field PNGs in {CAP}/alcove_cam{{0,2}}_signed_residual.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
