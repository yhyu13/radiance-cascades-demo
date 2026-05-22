"""MBRC v2.0-pre - (alpha) merge-mode sweep analyzer.

Reads tools/v20_pre_measurement/captures_alpha/cam{0,2}_<cfg>_m{18,19}.png and
emits B1 verdict (Delta-band area reduction vs baseline M0).

Mirrors analyze_mb_gain.py / analyze_cascade_config.py classifier exactly
(SATURATION_THRESHOLD=0.55, BG_LUMA_FLOOR=0.05) so (alpha) result is directly
comparable to (beta) and (gamma) on the same metric.

Pre-committed B1 verdict (alpha_merge_sweep.ps1 header, also doc/7/mb_gain_sweep_impl.md sec 8.1):
  STRONG_ALPHA : >=20% mode-19 Delta-area REDUCTION on BOTH cams in ANY of M1..M4
  WEAK_ALPHA   : 10-20% on both, OR >=20% on one cam only
  ALPHA_REJECT : ALL arms within +/-10% of M0 on BOTH cams -> pivot to (delta)

Bidirectional rule shape (per cerebrum 2026-05-22 DNR): the analyzer also reports
when an arm INCREASES Delta-area beyond +10%, labeling it ALPHA_LEVERAGE_WRONG_DIR
(parallels (beta)'s BETA_LEVERAGE_NOT_CURE).
"""
import sys
import json
from pathlib import Path
import numpy as np
from PIL import Image

CAPTURES_DIR = Path(__file__).parent / "captures_alpha"

SATURATION_THRESHOLD = 0.55
BG_LUMA_FLOOR        = 0.05

CONFIGS = [
    "M0_baseline",
    "M1_no_bilin",
    "M2_iso_merge",
    "M3_no_spatialtri",
    "M4_iso_nearest",
]


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
    mean_fg_luma = float(luma[fg_mask].mean())

    return {
        "path": str(path.name),
        "fg_pixels": fg_total,
        "blue_pixels": int(blue_mask.sum()),
        "red_pixels":  int(red_mask.sum()),
        "blue_frac":   float(blue_mask.sum()) / fg_total,
        "red_frac":    float(red_mask.sum())  / fg_total,
        "total_frac":  float(blue_mask.sum() + red_mask.sum()) / fg_total,
        "mean_fg_luma":  mean_fg_luma,
    }


def main():
    results = []
    for cfg in CONFIGS:
        for cam in (0, 2):
            for mode in (18, 19):
                p = CAPTURES_DIR / f"cam{cam}_{cfg}_m{mode:02d}.png"
                if not p.exists():
                    print(f"MISSING: {p.name}", file=sys.stderr)
                    continue
                r = analyze_image(p)
                if r is None:
                    print(f"EMPTY_FG: {p.name}", file=sys.stderr)
                    continue
                r["cfg"] = cfg
                r["cam"] = cam
                r["mode"] = mode
                results.append(r)

    print(f"{'capture':<48} {'fg_px':>8} {'blue%':>8} {'red%':>8} {'total%':>8} "
          f"{'mean_luma':>10}")
    for r in results:
        print(f"{r['path']:<48} {r['fg_pixels']:>8} "
              f"{r['blue_frac']*100:>7.2f}% {r['red_frac']*100:>7.2f}% "
              f"{r['total_frac']*100:>7.2f}% {r['mean_fg_luma']:>10.4f}")

    def get(cfg, cam, mode):
        for r in results:
            if r["cfg"] == cfg and r["cam"] == cam and r["mode"] == mode:
                return r
        return None

    print()
    print("=== (alpha) merge-mode B1 discriminator: mode 19 Delta-area sweep ===")
    print(f"{'cam':<5} " + " ".join(f"{c:>20}" for c in CONFIGS))
    deltas = {cfg: {} for cfg in CONFIGS}
    for cam in (0, 2):
        baseline = get("M0_baseline", cam, 19)
        if baseline is None:
            print(f"cam{cam}: MISSING M0 baseline, skipping verdict computation")
            continue
        cells = []
        for cfg in CONFIGS:
            r = get(cfg, cam, 19)
            if r is None:
                cells.append("    n/a")
                continue
            ta = r["total_frac"]
            if baseline["total_frac"] > 1e-6:
                d = (ta - baseline["total_frac"]) / baseline["total_frac"]
            else:
                d = 0.0
            deltas[cfg][cam] = d
            if cfg == "M0_baseline":
                cells.append(f"{ta*100:>11.2f}% ref ")
            else:
                cells.append(f"{ta*100:>10.2f}%{d*100:>+6.1f}%")
        print(f"cam{cam:<2} " + " ".join(cells))

    print()
    print("Pre-committed thresholds (alpha_merge_sweep.ps1 header):")
    print("  STRONG_ALPHA : >=20% reduction on BOTH cams in ANY of M1..M4")
    print("  WEAK_ALPHA   : 10-20% on both, OR >=20% on one cam only")
    print("  ALPHA_REJECT : ALL arms within +/-10% of M0 on BOTH cams")
    print("  ALPHA_LEVERAGE_WRONG_DIR : any arm >+10% on either cam (bidirectional reporting)")
    print()

    strong_arms = []
    weak_arms   = []
    wrong_dir   = []
    for cfg in CONFIGS:
        if cfg == "M0_baseline": continue
        d0 = deltas[cfg].get(0)
        d2 = deltas[cfg].get(2)
        if d0 is None or d2 is None: continue
        if d0 <= -0.20 and d2 <= -0.20:
            strong_arms.append((cfg, d0, d2))
        elif (d0 <= -0.10 and d2 <= -0.10) or d0 <= -0.20 or d2 <= -0.20:
            weak_arms.append((cfg, d0, d2))
        if d0 >= 0.10 or d2 >= 0.10:
            wrong_dir.append((cfg, d0, d2))

    if strong_arms:
        print("VERDICT: STRONG_ALPHA")
        for cfg, d0, d2 in strong_arms:
            print(f"  {cfg}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}%")
        print("  -> ship the winning toggle OR investigate the specific weighting bug.")
    elif weak_arms:
        print("VERDICT: WEAK_ALPHA")
        for cfg, d0, d2 in weak_arms:
            print(f"  {cfg}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}%")
        print("  -> finer-grained sweep + bug-230 fix mandatory before deciding.")
    else:
        all_within = True
        for cfg in CONFIGS:
            if cfg == "M0_baseline": continue
            d0 = deltas[cfg].get(0); d2 = deltas[cfg].get(2)
            if d0 is None or d2 is None: continue
            if abs(d0) > 0.10 or abs(d2) > 0.10:
                all_within = False; break
        if all_within:
            print("VERDICT: ALPHA_REJECT -- ALL arms within +/-10% on BOTH cams.")
            print("  -> pivot to (delta) spatial probe density discriminator.")
        else:
            print("VERDICT: MIXED -- requires manual inspection.")

    if wrong_dir:
        print()
        print("Bidirectional report -- ALPHA_LEVERAGE_WRONG_DIR (arms that INCREASE Delta-area):")
        for cfg, d0, d2 in wrong_dir:
            print(f"  {cfg}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}%")

    out = CAPTURES_DIR.parent / "alpha_merge_results.json"
    out.write_text(json.dumps(results, indent=2))
    print(f"\nresults JSON: {out}")


if __name__ == "__main__":
    main()
