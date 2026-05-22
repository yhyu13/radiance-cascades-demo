"""MBRC v2.0-pre - (beta) MB-gain sweep analyzer.

Reads tools/v20_pre_measurement/captures_mb/cam{0,2}_g{050,100,150,200,300}_m{18,19}.png
and emits B1 verdict (Delta-band area reduction) for hypothesis (beta) MB-gain.

Mirrors analyze_cascade_config.py classifier exactly (same SATURATION_THRESHOLD
and BG_LUMA_FLOOR) so the (beta) result is directly comparable to the (gamma)
result on the same metric.

B1 decision rule (pre-committed in doc/7/cascade_config_sweep_impl.md sec 8.1):
  STRONG_BETA: gain=2.0 reduces mode-19 (blue+red) area by >=30% on BOTH cams
  WEAK_BETA  : 10-30%, or asymmetric (one cam only)
  BETA_REJECT: <=10% on BOTH cams

B2 (floorRatio = cascade_GI / PT_GI) is DEFERRED -- PT-GI is not exposed as a
standalone render mode and PNG-tonemapped luminance ratios are biased relative
to HDR-radiance ratios. See doc/7/mb_gain_sweep_impl.md section 7.
"""
import sys
import json
from pathlib import Path
import numpy as np
from PIL import Image

CAPTURES_DIR = Path(__file__).parent / "captures_mb"

# Match analyze_cascade_config.py exactly so results are comparable.
SATURATION_THRESHOLD = 0.55
BG_LUMA_FLOOR        = 0.05

GAINS = [0.5, 1.0, 1.5, 2.0, 3.0]


def gain_tag(g):
    return f"g{int(g * 100):03d}"


def analyze_image(path: Path):
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
    fg_mask = luma > BG_LUMA_FLOOR
    fg_total = int(fg_mask.sum())
    if fg_total == 0:
        return None

    sat = np.clip((np.maximum(r, b) - g) / 0.8, 0.0, 1.0)
    blue_mask = fg_mask & (b > r + 0.05) & (sat > SATURATION_THRESHOLD)
    red_mask  = fg_mask & (r > b + 0.05) & (sat > SATURATION_THRESHOLD)

    # B2-lite proxy: mean luma of foreground (NOT a per-pixel ratio -- PT-GI
    # unavailable -- but a single-number sanity check that gain raises overall
    # rendered brightness in mode 19 the way Phase MB tooltip claims).
    mean_fg_luma = float(luma[fg_mask].mean())

    return {
        "path": str(path.name),
        "fg_pixels": fg_total,
        "blue_pixels": int(blue_mask.sum()),
        "red_pixels":  int(red_mask.sum()),
        "blue_frac":   float(blue_mask.sum()) / fg_total,
        "red_frac":    float(red_mask.sum())  / fg_total,
        "total_frac":  float(blue_mask.sum() + red_mask.sum()) / fg_total,
        "mean_blue_sat": float(sat[blue_mask].mean()) if blue_mask.any() else 0.0,
        "mean_red_sat":  float(sat[red_mask].mean())  if red_mask.any()  else 0.0,
        "mean_fg_luma":  mean_fg_luma,
    }


def main():
    results = []
    for gain in GAINS:
        for cam in (0, 2):
            for mode in (18, 19):
                p = CAPTURES_DIR / f"cam{cam}_{gain_tag(gain)}_m{mode:02d}.png"
                if not p.exists():
                    print(f"MISSING: {p.name}", file=sys.stderr)
                    continue
                r = analyze_image(p)
                if r is None:
                    print(f"EMPTY_FG: {p.name}", file=sys.stderr)
                    continue
                r["gain"] = gain
                r["cam"] = cam
                r["mode"] = mode
                results.append(r)

    print(f"{'capture':<28} {'fg_px':>8} {'blue%':>8} {'red%':>8} {'total%':>8} "
          f"{'blueSat':>9} {'redSat':>8} {'mean_luma':>10}")
    for r in results:
        print(f"{r['path']:<28} {r['fg_pixels']:>8} "
              f"{r['blue_frac']*100:>7.2f}% {r['red_frac']*100:>7.2f}% "
              f"{r['total_frac']*100:>7.2f}% {r['mean_blue_sat']:>9.3f} "
              f"{r['mean_red_sat']:>8.3f} {r['mean_fg_luma']:>10.4f}")

    def get(gain, cam, mode):
        for r in results:
            if r["gain"] == gain and r["cam"] == cam and r["mode"] == mode:
                return r
        return None

    print()
    print("=== (beta) MB-gain B1 discriminator: Delta-area sweep on mode 19 ===")
    print(f"{'cam':<5} " + " ".join(f"{'g=' + str(g):>10}" for g in GAINS) +
          f" {'(g2-g1)/g1':>13}")
    rows = []
    for cam in (0, 2):
        baseline = get(1.0, cam, 19)
        gain2    = get(2.0, cam, 19)
        if not (baseline and gain2):
            continue
        cells = []
        for g in GAINS:
            r = get(g, cam, 19)
            cells.append(f"{r['total_frac']*100:>9.2f}%" if r else "    n/a")
        if baseline["total_frac"] > 1e-6:
            delta_b1 = (gain2["total_frac"] - baseline["total_frac"]) / baseline["total_frac"]
        else:
            delta_b1 = 0.0
        rows.append((cam, delta_b1))
        print(f"cam{cam:<2} " + " ".join(cells) + f" {delta_b1*100:>+12.1f}%")

    print()
    print("=== B2-lite (mean_fg_luma) sanity: does gain raise brightness? ===")
    print(f"{'cam':<5} " + " ".join(f"{'g=' + str(g):>10}" for g in GAINS) +
          f" {'(g2-g1)/g1':>13}")
    for cam in (0, 2):
        baseline = get(1.0, cam, 19)
        gain2    = get(2.0, cam, 19)
        cells = []
        for g in GAINS:
            r = get(g, cam, 19)
            cells.append(f"{r['mean_fg_luma']:>10.4f}" if r else "       n/a")
        if baseline and gain2 and baseline["mean_fg_luma"] > 1e-6:
            delta_b2l = (gain2["mean_fg_luma"] - baseline["mean_fg_luma"]) / baseline["mean_fg_luma"]
        else:
            delta_b2l = 0.0
        print(f"cam{cam:<2} " + " ".join(cells) + f" {delta_b2l*100:>+12.1f}%")

    print()
    print(f"Pre-committed B1 thresholds (impl doc sec 8.1):")
    print(f"  STRONG_BETA: gain=2.0 vs 1.0 mode-19 Delta-area reduction >= 30% on BOTH cams")
    print(f"  WEAK_BETA:   10-30% on both, or one-cam-only")
    print(f"  BETA_REJECT: <=10% on both")
    print()

    if len(rows) == 2:
        d0 = rows[0][1]
        d2 = rows[1][1]
        print(f"mode 19 B1 (gain=2.0 vs gain=1.0): cam0 delta = {d0*100:+.1f}%, "
              f"cam2 delta = {d2*100:+.1f}%")
        if d0 <= -0.30 and d2 <= -0.30:
            print("VERDICT: STRONG_BETA -- ship gain=2.0 as v2.0 baseline.")
        elif (d0 <= -0.10 and d2 <= -0.10) or d0 <= -0.30 or d2 <= -0.30:
            print("VERDICT: WEAK_BETA -- (beta) contributes; consider promotion alongside (alpha).")
        elif abs(d0) <= 0.10 and abs(d2) <= 0.10:
            print("VERDICT: BETA_REJECT -- pivot to (alpha) merge-weighting or (delta) spatial probe density.")
        else:
            print("VERDICT: MIXED -- requires manual inspection.")

    out = CAPTURES_DIR.parent / "mb_gain_results.json"
    out.write_text(json.dumps(results, indent=2))
    print(f"\nresults JSON: {out}")


if __name__ == "__main__":
    main()
