"""MBRC v2.0-pre — cascade-config sweep analyzer.

Reads tools/v20_pre_measurement/captures_cfg/cam{0,2}_d{04,08,16}_m{18,19}.png and
emits a discriminator verdict for hypothesis (gamma) angular under-sampling.

Mode 18/19 use a bipolar colormap: blue = cascade-under-illuminated (cascade<PT),
red = cascade-over-illuminated (cascade>PT), white = match. We measure asymmetric
delta as the fraction of pixels exceeding a saturation threshold, broken out by
side (blue/red), per (camera, D).

Background pixels are filtered (the cornell scene is letterboxed by the headless
window — large black border at top/bottom). We keep only pixels with luminance
> 0.05 to focus on the rendered region.

Decision rule (set BEFORE running):
  STRONG_GAMMA: D=16 reduces blue+red saturated area on mode 19 by >=50% at BOTH
                cam0 AND cam2 relative to D=8.
  WEAK_GAMMA  : D=16 reduces blue+red area on one cam but not the other, OR
                reduction is in (10%, 50%) range on both.
  GAMMA_REJECT: D=16 mode 19 blue+red area is within +/-10% of D=8 (invariant).
"""
import sys
import json
from pathlib import Path
import numpy as np
from PIL import Image

CAPTURES_DIR = Path(__file__).parent / "captures_cfg"

SATURATION_THRESHOLD = 0.55  # how saturated a pixel must be to count as "delta"
BG_LUMA_FLOOR        = 0.05  # pixels darker than this are letterbox background


def analyze_image(path: Path):
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    # Foreground = anything brighter than near-black background.
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
    fg_mask = luma > BG_LUMA_FLOOR
    fg_total = int(fg_mask.sum())
    if fg_total == 0:
        return None

    # Blue dominance (cascade-under): blue >> red AND saturation high.
    # Red  dominance (cascade-over):  red  >> blue AND saturation high.
    # "Saturation" proxy: max(r,b) - g, normalised; bipolar heatmap drops green
    # toward 0.2-0.4 in both saturated bands while white center keeps g~1.
    sat = np.clip((np.maximum(r, b) - g) / 0.8, 0.0, 1.0)
    blue_mask = fg_mask & (b > r + 0.05) & (sat > SATURATION_THRESHOLD)
    red_mask  = fg_mask & (r > b + 0.05) & (sat > SATURATION_THRESHOLD)

    return {
        "path": str(path.name),
        "fg_pixels": fg_total,
        "blue_pixels": int(blue_mask.sum()),
        "red_pixels":  int(red_mask.sum()),
        "blue_frac":   float(blue_mask.sum()) / fg_total,
        "red_frac":    float(red_mask.sum())  / fg_total,
        "total_frac":  float(blue_mask.sum() + red_mask.sum()) / fg_total,
        # Mean saturation of pixels that ARE in either band (depth-of-error).
        "mean_blue_sat": float(sat[blue_mask].mean()) if blue_mask.any() else 0.0,
        "mean_red_sat":  float(sat[red_mask].mean())  if red_mask.any()  else 0.0,
    }


def verdict(d8_total, d16_total):
    """Return (label, delta_pct) where delta is (D16-D8)/D8."""
    if d8_total < 1e-6:
        return "INDETERMINATE_NO_D8_AREA", 0.0
    delta = (d16_total - d8_total) / d8_total
    return None, delta


def main():
    results = []
    for D in (4, 8, 16):
        for cam in (0, 2):
            for mode in (18, 19):
                p = CAPTURES_DIR / f"cam{cam}_d{D:02d}_m{mode:02d}.png"
                if not p.exists():
                    print(f"MISSING: {p.name}", file=sys.stderr)
                    continue
                r = analyze_image(p)
                if r is None:
                    print(f"EMPTY_FG: {p.name}", file=sys.stderr)
                    continue
                r["D"] = D
                r["cam"] = cam
                r["mode"] = mode
                results.append(r)

    # Table.
    print(f"{'capture':<28} {'fg_px':>8} {'blue%':>8} {'red%':>8} {'total%':>8} {'blueSat':>9} {'redSat':>8}")
    for r in results:
        print(f"{r['path']:<28} {r['fg_pixels']:>8} "
              f"{r['blue_frac']*100:>7.2f}% {r['red_frac']*100:>7.2f}% "
              f"{r['total_frac']*100:>7.2f}% {r['mean_blue_sat']:>9.3f} {r['mean_red_sat']:>8.3f}")

    # Discriminator: D8 vs D16 on mode 19 at cam0 AND cam2.
    def get(D, cam, mode):
        for r in results:
            if r["D"] == D and r["cam"] == cam and r["mode"] == mode:
                return r
        return None

    print()
    print("=== (gamma) angular under-sampling discriminator ===")
    print(f"{'cam/mode':<14} {'D=4 tot%':>10} {'D=8 tot%':>10} {'D=16 tot%':>11} "
          f"{'(D16-D8)/D8':>14}")
    rows = []
    for cam in (0, 2):
        for mode in (18, 19):
            r4  = get(4,  cam, mode)
            r8  = get(8,  cam, mode)
            r16 = get(16, cam, mode)
            if not (r4 and r8 and r16):
                continue
            _, delta = verdict(r8["total_frac"], r16["total_frac"])
            rows.append((cam, mode, r4["total_frac"], r8["total_frac"], r16["total_frac"], delta))
            print(f"cam{cam} m{mode:<8} "
                  f"{r4['total_frac']*100:>9.2f}% "
                  f"{r8['total_frac']*100:>9.2f}% "
                  f"{r16['total_frac']*100:>10.2f}% "
                  f"{delta*100:>+13.1f}%")

    # Verdict: focus on mode 19 (GI-only) on BOTH cams.
    cam0_m19 = next((r for r in rows if r[0] == 0 and r[1] == 19), None)
    cam2_m19 = next((r for r in rows if r[0] == 2 and r[1] == 19), None)
    print()
    if cam0_m19 and cam2_m19:
        d0 = cam0_m19[5]
        d2 = cam2_m19[5]
        print(f"mode 19 (D16 vs D8): cam0 delta = {d0*100:+.1f}%, cam2 delta = {d2*100:+.1f}%")
        if d0 <= -0.50 and d2 <= -0.50:
            print("VERDICT: STRONG_GAMMA -- ship D=16 or higher as v2.0 baseline.")
        elif (d0 <= -0.10 and d2 <= -0.10) or d0 <= -0.50 or d2 <= -0.50:
            print("VERDICT: WEAK_GAMMA -- (gamma) contributes but is not the dominant cause.")
        elif abs(d0) <= 0.10 and abs(d2) <= 0.10:
            print("VERDICT: GAMMA_REJECT -- pivot to (alpha) merge-weighting or (beta) MB-gain.")
        else:
            print("VERDICT: MIXED -- requires manual inspection (asymmetric response across cams).")

    out = CAPTURES_DIR.parent / "cascade_config_results.json"
    out.write_text(json.dumps(results, indent=2))
    print(f"\nresults JSON: {out}")


if __name__ == "__main__":
    main()
