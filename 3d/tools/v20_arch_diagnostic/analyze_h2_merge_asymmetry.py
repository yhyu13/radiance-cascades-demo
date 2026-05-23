"""MBRC v2.0 (h.2) merge-asymmetry analyzer at MB-OFF b=2 baseline.

Per doc/7/v20_h_source_disambig_impl.md sec 6: at the single-bounce
baseline (MB OFF, PT b=2), cascade delivers 67% on cam0 and 33% on
cam2 -- a 2x geometric-asymmetry spread. This analyzer measures
whether different merge variants (M0/M2/M4) close, preserve, or
widen the spread.

Reads:
    captures_h2_merge/alcove_cam{0,2}_M{0,2}_*_b2_mboff_m17_*.exr  (4 NEW)
    captures_h_disambig/alcove_cam{0,2}_b2_mboff_m17_*.exr         (M4, reused)

Outputs per (merge, cam): cascade/PT energy ratio at b=2 MB-OFF.
Outputs per merge variant: cam2/cam0 ratio (the asymmetry measure).

Pre-committed verdict (per script header):
  - All 3 variants in [0.45, 0.55] cam2/cam0 -> MERGE_NOT_THE_SOURCE
  - Variant X in [0.80, 1.20] while others [0.40, 0.55] -> MERGE_VARIANT_X_SYMMETRIZES
  - All < 0.6 but one brightens cam0 closer to 1.0 -> MERGE_BRIGHTENS_BUT_SPREAD_PERSISTS
"""

from __future__ import annotations
import os, json, sys
from dataclasses import dataclass, asdict
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr); sys.exit(2)


HERE = os.path.dirname(os.path.abspath(__file__))
CAP_NEW  = os.path.join(HERE, "captures_h2_merge")
CAP_M4   = os.path.join(HERE, "captures_h_disambig")
OUT_JSON = os.path.join(HERE, "h2_merge_asymmetry_results.json")
EPS_PT   = 1e-3


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
class Row:
    cam: int
    merge: str
    sum_pos: float
    sum_neg: float
    abs_ratio: float
    energy_ratio: float
    integrated_pt: float
    integrated_cascade: float


def analyze(cam: int, merge_label: str) -> Row | None:
    if merge_label == "M4_iso_nearest":
        stem = os.path.join(CAP_M4, f"alcove_cam{cam}_b2_mboff_m17")
    else:
        stem = os.path.join(CAP_NEW, f"alcove_cam{cam}_{merge_label}_b2_mboff_m17")
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam} {merge_label}: missing {k} ({p})")
            return None
    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt  = lum(ptGI); Lc = lum(casc)
    Ld   = Lc - Lpt
    valid = Lpt > EPS_PT
    if valid.sum() == 0:
        return None
    pos = valid & (Ld > 0); neg = valid & (Ld < 0)
    sp = float(Ld[pos].sum()); sn = float((-Ld[neg]).sum())
    ipt = float(Lpt[valid].sum()); ic = float(Lc[valid].sum())
    return Row(
        cam=cam, merge=merge_label,
        sum_pos=sp, sum_neg=sn,
        abs_ratio=(sp / sn) if sn > 1e-9 else float("inf"),
        energy_ratio=(ic / ipt) if ipt > 0 else float("nan"),
        integrated_pt=ipt, integrated_cascade=ic,
    )


def classify(by_merge: dict[str, dict[int, float]]) -> tuple[str, str]:
    """Returns (verdict, detail). by_merge[merge][cam] = energy_ratio."""
    spreads = {}
    for m, ratios in by_merge.items():
        if 0 in ratios and 2 in ratios and ratios[0] > 1e-6:
            spreads[m] = ratios[2] / ratios[0]
    if not spreads:
        return "INCOMPLETE", "missing pairs"
    sym_band = [(m, s) for m, s in spreads.items() if 0.80 <= s <= 1.20]
    asym_band = [(m, s) for m, s in spreads.items() if s < 0.60]
    if len(sym_band) == 1 and len(asym_band) >= 1:
        m, s = sym_band[0]
        return ("MERGE_VARIANT_SYMMETRIZES",
                f"{m} cam2/cam0 = {s:.3f} (symmetric); others asymmetric. "
                f"This variant is the asymmetry-reducing target.")
    if all(0.45 <= s <= 0.55 for s in spreads.values()):
        return ("MERGE_NOT_THE_SOURCE",
                f"all variants cam2/cam0 in [0.45, 0.55] "
                f"(spreads = {spreads}); cause is deeper "
                f"(camera projection / probe placement / ray geometry).")
    # Otherwise: variants differ in cam0 magnitude but spread persists < 0.6
    if all(s < 0.60 for s in spreads.values()):
        # Find variant that brings cam0 closest to 1.0
        best_cam0 = max(by_merge.items(), key=lambda kv: kv[1].get(0, 0))
        return ("MERGE_BRIGHTENS_BUT_SPREAD_PERSISTS",
                f"all variants cam2/cam0 < 0.60 (spreads = {spreads}); "
                f"{best_cam0[0]} maximizes cam0 = {best_cam0[1].get(0, 0):.3f} "
                f"but the spread is invariant -- spread is architectural.")
    return ("MIXED", f"spreads do not fit any pre-committed band: {spreads}")


def main() -> int:
    merges = ["M0_baseline", "M2_iso_merge", "M4_iso_nearest"]
    rows: list[Row] = []
    for m in merges:
        for cam in (0, 2):
            r = analyze(cam, m)
            if r is not None:
                rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'merge':<16} {'cam':>3} "
          f"{'Sum+':>10} {'Sum-':>10} {'|+/-|':>7} "
          f"{'integPT':>9} {'integCasc':>10} {'casc/PT':>9}")
    print("-" * 92)
    for r in rows:
        rs = f"{r.abs_ratio:7.3f}" if np.isfinite(r.abs_ratio) else "    inf"
        print(f"{r.merge:<16} {r.cam:>3} "
              f"{r.sum_pos:>10.3f} {r.sum_neg:>10.3f} {rs} "
              f"{r.integrated_pt:>9.3f} {r.integrated_cascade:>10.3f} "
              f"{r.energy_ratio:>9.4f}")

    by_merge: dict[str, dict[int, float]] = {}
    for r in rows:
        by_merge.setdefault(r.merge, {})[r.cam] = r.energy_ratio

    print()
    print("=== ASYMMETRY (cam2/cam0 energy ratio, MB OFF b=2) ===")
    print(f"{'merge':<16} {'cam0':>8} {'cam2':>8} {'cam2/cam0':>10}")
    print("-" * 46)
    for m, ratios in by_merge.items():
        if 0 in ratios and 2 in ratios:
            spread = ratios[2] / ratios[0] if ratios[0] > 1e-6 else float("nan")
            print(f"{m:<16} {ratios[0]:>8.4f} {ratios[2]:>8.4f} {spread:>10.4f}")

    verdict, detail = classify(by_merge)
    print()
    print(f"VERDICT: {verdict}")
    print(f"detail:  {detail}")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "rows": [asdict(r) for r in rows],
        "by_merge_cam2_over_cam0": {
            m: (r.get(2, 0) / r[0]) if 0 in r and r.get(0, 0) > 1e-6 else None
            for m, r in by_merge.items()
        },
        "verdict": verdict,
        "verdict_detail": detail,
    }
    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT_JSON}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
