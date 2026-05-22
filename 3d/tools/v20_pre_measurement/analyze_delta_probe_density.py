"""MBRC v2.0-pre - (delta) spatial probe density sweep analyzer.

Reads tools/v20_pre_measurement/captures_delta/cam{0,2}_N{16,32,48,64}_m{18,19}.png
and emits B1 verdict (Delta-band area reduction vs N=32 baseline).

Mirrors analyze_alpha_merge.py / analyze_mb_gain.py classifier exactly
(SATURATION_THRESHOLD=0.55, BG_LUMA_FLOOR=0.05).

Pre-committed B1 verdict (delta_probe_density_sweep.ps1 header):
  STRONG_DELTA : any N reduces cam2 mode-19 Delta-area >=20% AND keeps cam0 within +/-10%
  WEAK_DELTA   : any N reduces cam2 10-20% AND keeps cam0 within +/-10%; OR >=20% on one cam only
  DELTA_REJECT : ALL N within +/-10% of N=32 on BOTH cams -> exit named-hypothesis tree
  DELTA_LEVERAGE_WRONG_DIR : any N increases Delta-area >+10% on either cam (bidirectional)

Additionally flags one-sided leverage when only one cam shows movement (per alpha
C6 self-critique: one-cam-sensitive axes are diagnostically valuable).
"""
import sys
import json
from pathlib import Path
import numpy as np
from PIL import Image

CAPTURES_DIR = Path(__file__).parent / "captures_delta"

SATURATION_THRESHOLD = 0.55
BG_LUMA_FLOOR        = 0.05

N_VALUES = [16, 32, 48, 64]
BASELINE_N = 32


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
    for n in N_VALUES:
        for cam in (0, 2):
            for mode in (18, 19):
                p = CAPTURES_DIR / f"cam{cam}_N{n:02d}_m{mode:02d}.png"
                if not p.exists():
                    print(f"MISSING: {p.name}", file=sys.stderr)
                    continue
                r = analyze_image(p)
                if r is None:
                    print(f"EMPTY_FG: {p.name}", file=sys.stderr)
                    continue
                r["N"] = n
                r["cam"] = cam
                r["mode"] = mode
                results.append(r)

    print(f"{'capture':<48} {'fg_px':>8} {'blue%':>8} {'red%':>8} {'total%':>8} "
          f"{'mean_luma':>10}")
    for r in results:
        print(f"{r['path']:<48} {r['fg_pixels']:>8} "
              f"{r['blue_frac']*100:>7.2f}% {r['red_frac']*100:>7.2f}% "
              f"{r['total_frac']*100:>7.2f}% {r['mean_fg_luma']:>10.4f}")

    def get(n, cam, mode):
        for r in results:
            if r["N"] == n and r["cam"] == cam and r["mode"] == mode:
                return r
        return None

    print()
    print("=== (delta) probe-density B1 discriminator: mode 19 Delta-area sweep ===")
    print(f"{'cam':<5} " + " ".join(f"{('N='+str(n)):>20}" for n in N_VALUES))
    deltas = {n: {} for n in N_VALUES}
    for cam in (0, 2):
        baseline = get(BASELINE_N, cam, 19)
        if baseline is None:
            print(f"cam{cam}: MISSING N={BASELINE_N} baseline, skipping verdict computation")
            continue
        cells = []
        for n in N_VALUES:
            r = get(n, cam, 19)
            if r is None:
                cells.append("    n/a")
                continue
            ta = r["total_frac"]
            if baseline["total_frac"] > 1e-6:
                d = (ta - baseline["total_frac"]) / baseline["total_frac"]
            else:
                d = 0.0
            deltas[n][cam] = d
            if n == BASELINE_N:
                cells.append(f"{ta*100:>11.2f}% ref ")
            else:
                cells.append(f"{ta*100:>10.2f}%{d*100:>+6.1f}%")
        print(f"cam{cam:<2} " + " ".join(cells))

    print()
    print("Pre-committed thresholds (delta_probe_density_sweep.ps1 header):")
    print("  STRONG_DELTA : any N reduces cam2 >=20% AND keeps cam0 within +/-10%")
    print("  WEAK_DELTA   : any N reduces cam2 10-20% AND keeps cam0 within +/-10%; OR >=20% on one cam")
    print("  DELTA_REJECT : ALL N within +/-10% of N=32 on BOTH cams -> exit named-hypothesis tree")
    print("  DELTA_LEVERAGE_WRONG_DIR : any N increases Delta-area >+10% on either cam")
    print()

    strong_arms = []
    weak_arms   = []
    wrong_dir   = []
    one_sided   = []  # tracks one-cam-only leverage (alpha C6 lesson)
    for n in N_VALUES:
        if n == BASELINE_N: continue
        d0 = deltas[n].get(0)
        d2 = deltas[n].get(2)
        if d0 is None or d2 is None: continue
        if d2 <= -0.20 and abs(d0) <= 0.10:
            strong_arms.append((n, d0, d2))
        elif (d2 <= -0.10 and abs(d0) <= 0.10) or d0 <= -0.20 or d2 <= -0.20:
            weak_arms.append((n, d0, d2))
        if d0 >= 0.10 or d2 >= 0.10:
            wrong_dir.append((n, d0, d2))
        # One-sided detector: one cam |d|>=10%, the other within +/-5%
        if (abs(d0) >= 0.10 and abs(d2) <= 0.05) or (abs(d2) >= 0.10 and abs(d0) <= 0.05):
            one_sided.append((n, d0, d2))

    if strong_arms:
        print("VERDICT: STRONG_DELTA")
        for n, d0, d2 in strong_arms:
            print(f"  N={n}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}%")
        print("  -> ship the winning N OR investigate why probe density alone is the cure.")
    elif weak_arms:
        print("VERDICT: WEAK_DELTA")
        for n, d0, d2 in weak_arms:
            print(f"  N={n}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}%")
        print("  -> finer-grained sweep + bug-230 fix mandatory before deciding.")
    else:
        all_within = True
        for n in N_VALUES:
            if n == BASELINE_N: continue
            d0 = deltas[n].get(0); d2 = deltas[n].get(2)
            if d0 is None or d2 is None: continue
            if abs(d0) > 0.10 or abs(d2) > 0.10:
                all_within = False; break
        if all_within:
            print("VERDICT: DELTA_REJECT -- ALL N within +/-10% on BOTH cams.")
            print("  -> exit named-hypothesis tree; promote (epsilon) per-direction-bin fetch geometry")
            print("     and broader cascade falloff review. bug-230 MUST be fixed before any further sweep.")
        else:
            print("VERDICT: MIXED -- requires manual inspection.")

    if wrong_dir:
        print()
        print("Bidirectional report -- DELTA_LEVERAGE_WRONG_DIR (arms that INCREASE Delta-area):")
        for n, d0, d2 in wrong_dir:
            print(f"  N={n}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}%")

    if one_sided:
        print()
        print("One-sided leverage detector (alpha C6 lesson): cams disagree by >2x:")
        for n, d0, d2 in one_sided:
            print(f"  N={n}: cam0 {d0*100:+.1f}%, cam2 {d2*100:+.1f}% (axis is view-dependent)")

    out = CAPTURES_DIR.parent / "delta_probe_density_results.json"
    out.write_text(json.dumps(results, indent=2))
    print(f"\nresults JSON: {out}")


if __name__ == "__main__":
    main()
