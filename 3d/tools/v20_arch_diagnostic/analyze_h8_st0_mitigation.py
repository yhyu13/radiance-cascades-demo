"""MBRC v2.0 (h.c)''' ST=0 mitigation validation across 2 scenes.

Reads 4 cells from captures_h8_st0_mitigation/:
  {cornell_default, cornell_orig} x {st1, st0}

Computes per-cell cascade/PT ratio + RMSE vs PT-GI luminance + per-scene
ratio delta ST=0 - ST=1.

Pre-committed verdict bands (per h8 capture script):
  ratio_st0 - ratio_st1:
    >= +0.05 -> ST0_IMPROVES_QUALITY
    -0.05..+0.05 -> ST_NEUTRAL
    <= -0.05 -> ST1_BETTER

Composite recommendation:
  Both scenes ST0_IMPROVES_QUALITY -> recommend flipping default to ST=0
  Mixed or NEUTRAL on either scene -> hold default at ST=1
"""
from __future__ import annotations
import os, json, sys
from dataclasses import dataclass, asdict
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr); sys.exit(2)


HERE   = os.path.dirname(os.path.abspath(__file__))
CAP    = os.path.join(HERE, "captures_h8_st0_mitigation")
OUT    = os.path.join(HERE, "h8_st0_mitigation_results.json")
EPS_PT = 1e-3


def read_exr_rgb(path: str) -> np.ndarray:
    f = OpenEXR.InputFile(path)
    h = f.header(); dw = h["dataWindow"]
    w = dw.max.x - dw.min.x + 1
    H = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    chans = {c: np.frombuffer(f.channel(c, pt), dtype=np.float32).reshape(H, w)
             for c in "RGB"}
    return np.dstack([chans["R"], chans["G"], chans["B"]])


def lum(rgb): return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def downsample_2x2_avg(a):
    H, W = a.shape[:2]
    H2, W2 = H // 2, W // 2
    a = a[: H2 * 2, : W2 * 2]
    if a.ndim == 3:
        return a.reshape(H2, 2, W2, 2, -1).mean(axis=(1, 3))
    return a.reshape(H2, 2, W2, 2).mean(axis=(1, 3))


@dataclass
class Row:
    scene: str
    st: int
    energy_ratio: float
    integrated_pt: float
    integrated_cascade: float
    rmse_lum: float
    n_valid: int


def analyze(scene: str, st: int) -> Row | None:
    stTag = "st1" if st == 1 else "st0"
    stem = os.path.join(CAP, f"{scene}_{stTag}_M0_b2_mboff_m17")
    paths = {"gi":  f"{stem}_cascade_gi.exr",
             "ptF": f"{stem}_pt_full.exr",
             "ptD": f"{stem}_pt_direct.exr"}
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] {scene} st={st}: missing {k} ({p})"); return None
    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt  = lum(ptGI); Lc = lum(casc)
    valid = Lpt > EPS_PT
    n = int(valid.sum())
    if n == 0: return None
    ipt = float(Lpt[valid].sum()); ic = float(Lc[valid].sum())
    rmse = float(np.sqrt(np.mean((Lc[valid] - Lpt[valid]) ** 2)))
    return Row(scene=scene, st=st,
               energy_ratio=(ic / ipt) if ipt > 0 else float("nan"),
               integrated_pt=ipt, integrated_cascade=ic,
               rmse_lum=rmse, n_valid=n)


def classify(delta_ratio: float, delta_rmse: float) -> str:
    """Two-signal classification: ratio closer to 1.0 AND lower RMSE both indicate quality."""
    ratio_better = delta_ratio >= 0.05    # ratio increased >=5pt (closer to 1.0 = better integration)
    rmse_better  = delta_rmse <= -0.001   # RMSE decreased

    if ratio_better and rmse_better:
        return "ST0_IMPROVES_QUALITY"
    if (not ratio_better) and (not rmse_better) and abs(delta_ratio) < 0.05:
        return "ST_NEUTRAL"
    if delta_ratio <= -0.05 or delta_rmse > 0.005:
        return "ST1_BETTER"
    return "ST_MIXED"  # ratio better but RMSE worse, or vice versa


def main() -> int:
    scenes = ["cornell_default", "cornell_orig"]
    rows: list[Row] = []
    for s in scenes:
        for st in (1, 0):
            r = analyze(s, st)
            if r is not None: rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'scene':<18} {'ST':>3} {'integPT':>9} {'integCasc':>10} "
          f"{'casc/PT':>9} {'rmse':>8} {'n_valid':>8}")
    print("-" * 75)
    for r in rows:
        print(f"{r.scene:<18} {r.st:>3} {r.integrated_pt:>9.3f} "
              f"{r.integrated_cascade:>10.3f} {r.energy_ratio:>9.4f} "
              f"{r.rmse_lum:>8.4f} {r.n_valid:>8d}")

    by = {(r.scene, r.st): r for r in rows}

    print()
    print("=== Per-scene delta ST=0 vs ST=1 ===")
    print(f"{'scene':<18} {'ratio_st1':>9} {'ratio_st0':>9} {'delta_r':>9} "
          f"{'rmse_st1':>9} {'rmse_st0':>9} {'delta_rmse':>11}")
    print("-" * 80)
    per_scene = {}
    for s in scenes:
        r1 = by.get((s, 1)); r0 = by.get((s, 0))
        if r1 is None or r0 is None: continue
        dr = r0.energy_ratio - r1.energy_ratio
        drmse = r0.rmse_lum - r1.rmse_lum
        verdict = classify(dr, drmse)
        per_scene[s] = {
            "ratio_st1": r1.energy_ratio,
            "ratio_st0": r0.energy_ratio,
            "delta_ratio": dr,
            "rmse_st1": r1.rmse_lum,
            "rmse_st0": r0.rmse_lum,
            "delta_rmse": drmse,
            "verdict": verdict
        }
        print(f"{s:<18} {r1.energy_ratio:>9.4f} {r0.energy_ratio:>9.4f} "
              f"{dr:>+9.4f} {r1.rmse_lum:>9.4f} {r0.rmse_lum:>9.4f} {drmse:>+11.4f}")
        print(f"  -> {verdict}")

    # Composite recommendation
    verdicts = [v["verdict"] for v in per_scene.values()]
    all_improve = all(v == "ST0_IMPROVES_QUALITY" for v in verdicts)
    any_worse   = any(v == "ST1_BETTER" for v in verdicts)
    composite = None
    print()
    if all_improve:
        composite = "RECOMMEND_FLIP_DEFAULT_TO_ST0"
        print(f"COMPOSITE: {composite}")
        print("  All scenes show ST=0 improves both ratio (closer to 1.0) AND")
        print("  RMSE (lower error vs PT). Mitigation flag recommended as new default.")
    elif any_worse:
        composite = "HOLD_DEFAULT_ST1_SCENE_REGRESSION"
        print(f"COMPOSITE: {composite}")
        print("  At least one scene shows ST=1 is better. Don't flip default;")
        print("  alcove ST=0 quality gain doesn't generalize.")
    else:
        composite = "MIXED_HOLD_DEFAULT_ST1"
        print(f"COMPOSITE: {composite}")
        print("  No scene shows clean ST=0 improvement. Hold default at ST=1;")
        print("  surface as an opt-in flag with scene-specific guidance.")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "rows": [asdict(r) for r in rows],
        "per_scene": per_scene,
        "composite_recommendation": composite,
    }
    with open(OUT, "w") as f: json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
