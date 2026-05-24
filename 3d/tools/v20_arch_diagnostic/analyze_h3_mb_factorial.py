"""MBRC v2.0 (h.3) MB-ON x merge-variant factorial at b=2.

Per doc/7/v20_h2_merge_asymmetry_impl.md sec 6 + (h.3) capture script:
the full 2x3x2 factorial of (MB-ON/OFF) x (M0/M2/M4) x (cam0/cam2) at b=2.

Reads:
    captures_h2_merge/alcove_cam{0,2}_M{0,2}_*_b2_mboff_m17_*.exr  (MB-OFF M0/M2)
    captures_h_disambig/alcove_cam{0,2}_b2_mboff_m17_*.exr         (MB-OFF M4, default)
    captures_h3_mb_factorial/alcove_cam{0,2}_M{0,2}_*_b2_mbon_m17_*.exr (MB-ON M0/M2, NEW)
    captures_pt_bounce_ladder/alcove_cam{0,2}_b2_m17_*.exr         (MB-ON M4, default)

Outputs per (mb, merge, cam): cascade/PT energy ratio at b=2.
Outputs per (merge, cam): MB multiplier (MB-ON ratio / MB-OFF ratio).
Outputs per merge: MB-OFF spread vs MB-ON spread (cam2/cam0 ratios).

Pre-committed verdict (per script header):
  max(MB_mult)/min(MB_mult) per cam:
    <= 1.10 -> MB_MULTIPLIER_INVARIANT
    1.10..1.50 -> MB_MULTIPLIER_MILDLY_MERGE_DEPENDENT
    > 1.50 -> MB_MULTIPLIER_STRONGLY_MERGE_DEPENDENT
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
CAP_MBOFF_M02 = os.path.join(HERE, "captures_h2_merge")
CAP_MBOFF_M4  = os.path.join(HERE, "captures_h_disambig")
CAP_MBON_M02  = os.path.join(HERE, "captures_h3_mb_factorial")
CAP_MBON_M4   = os.path.join(HERE, "captures_pt_bounce_ladder")
OUT_JSON      = os.path.join(HERE, "h3_mb_factorial_results.json")
EPS_PT        = 1e-3


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
    mb: str
    energy_ratio: float
    integrated_pt: float
    integrated_cascade: float


def stem_for(cam: int, merge: str, mb: str) -> str:
    """Locate the file stem for (cam, merge, mb) across the 4 capture dirs."""
    if mb == "OFF":
        if merge == "M4_iso_nearest":
            return os.path.join(CAP_MBOFF_M4, f"alcove_cam{cam}_b2_mboff_m17")
        return os.path.join(CAP_MBOFF_M02, f"alcove_cam{cam}_{merge}_b2_mboff_m17")
    else:  # MB ON
        if merge == "M4_iso_nearest":
            return os.path.join(CAP_MBON_M4, f"alcove_cam{cam}_b2_m17")
        return os.path.join(CAP_MBON_M02, f"alcove_cam{cam}_{merge}_b2_mbon_m17")


def analyze(cam: int, merge: str, mb: str) -> Row | None:
    stem = stem_for(cam, merge, mb)
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam} {merge} MB={mb}: missing {k} ({p})")
            return None
    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt = lum(ptGI); Lc = lum(casc)
    valid = Lpt > EPS_PT
    if valid.sum() == 0:
        return None
    ipt = float(Lpt[valid].sum()); ic = float(Lc[valid].sum())
    return Row(
        cam=cam, merge=merge, mb=mb,
        energy_ratio=(ic / ipt) if ipt > 0 else float("nan"),
        integrated_pt=ipt, integrated_cascade=ic,
    )


def classify_mb_uniformity(mb_mults: dict[int, dict[str, float]]) -> tuple[str, str]:
    """Returns (verdict, detail). mb_mults[cam][merge] = MB_ON_ratio/MB_OFF_ratio."""
    summary = {}
    for cam, by_merge in mb_mults.items():
        vals = [v for v in by_merge.values() if np.isfinite(v) and v > 0]
        if not vals:
            continue
        summary[cam] = max(vals) / min(vals)
    if not summary:
        return "INCOMPLETE", "no MB multiplier pairs computed"
    max_spread = max(summary.values())
    detail = f"per-cam max/min MB-mult spread = {summary}"
    if max_spread <= 1.10:
        return "MB_MULTIPLIER_INVARIANT", (
            f"MB amp is merge-independent (worst cam spread {max_spread:.3f} <= 1.10). "
            f"M4+MB super-additivity from alpha_m4_deepdive was b=8-specific; "
            f"at b=2 (apples-to-apples) MB amp doesn't depend on merge formula. " + detail)
    if max_spread <= 1.50:
        return "MB_MULTIPLIER_MILDLY_MERGE_DEPENDENT", (
            f"MB amp varies mildly across merges ({max_spread:.3f}). "
            f"M4+MB super-additivity is partially reproducible at b=2 but weaker than the "
            f"alpha_m4_deepdive measurement suggested. " + detail)
    return "MB_MULTIPLIER_STRONGLY_MERGE_DEPENDENT", (
        f"MB amp strongly depends on merge variant ({max_spread:.3f} > 1.50). "
        f"M4+MB super-additivity is real and merge-formula-specific. "
        f"The merge formula influences how much MB can amplify per-frame. " + detail)


def main() -> int:
    merges = ["M0_baseline", "M2_iso_merge", "M4_iso_nearest"]
    mbs = ["OFF", "ON"]
    rows: list[Row] = []
    for m in merges:
        for mb in mbs:
            for cam in (0, 2):
                r = analyze(cam, m, mb)
                if r is not None:
                    rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'merge':<16} {'mb':<4} {'cam':>3} "
          f"{'integPT':>9} {'integCasc':>10} {'casc/PT':>9}")
    print("-" * 60)
    for r in rows:
        print(f"{r.merge:<16} {r.mb:<4} {r.cam:>3} "
              f"{r.integrated_pt:>9.3f} {r.integrated_cascade:>10.3f} "
              f"{r.energy_ratio:>9.4f}")

    # index by (merge, mb, cam) -> ratio
    by = {}
    for r in rows:
        by.setdefault((r.merge, r.mb), {})[r.cam] = r.energy_ratio

    print()
    print("=== MB multiplier (MB-ON ratio / MB-OFF ratio) per (merge, cam) ===")
    print(f"{'merge':<16} {'cam':>3} {'OFF':>8} {'ON':>8} {'mult':>8}")
    print("-" * 50)
    mb_mults: dict[int, dict[str, float]] = {0: {}, 2: {}}
    for m in merges:
        off = by.get((m, "OFF"), {})
        on  = by.get((m, "ON"), {})
        for cam in (0, 2):
            if cam in off and cam in on and off[cam] > 1e-6:
                mult = on[cam] / off[cam]
                mb_mults[cam][m] = mult
                print(f"{m:<16} {cam:>3} {off[cam]:>8.4f} {on[cam]:>8.4f} {mult:>8.3f}")

    print()
    print("=== Spread (cam2/cam0) per (merge, mb) ===")
    print(f"{'merge':<16} {'mb':<4} {'cam0':>8} {'cam2':>8} {'c2/c0':>8}")
    print("-" * 50)
    spreads = {}
    for m in merges:
        for mb in mbs:
            r = by.get((m, mb), {})
            if 0 in r and 2 in r and r[0] > 1e-6:
                s = r[2] / r[0]
                spreads[(m, mb)] = s
                print(f"{m:<16} {mb:<4} {r[0]:>8.4f} {r[2]:>8.4f} {s:>8.4f}")

    verdict, detail = classify_mb_uniformity(mb_mults)
    print()
    print(f"VERDICT (MB-multiplier uniformity): {verdict}")
    print(f"detail: {detail}")

    print()
    print("=== Spread comparison: MB-OFF vs MB-ON per variant ===")
    for m in merges:
        off = spreads.get((m, "OFF"))
        on  = spreads.get((m, "ON"))
        if off is None or on is None:
            continue
        delta = on - off
        sym = "MB SYMMETRIZES" if delta > 0.02 else ("MB AMPLIFIES asymmetry" if delta < -0.02 else "MB INVARIANT spread")
        print(f"  {m:<16} OFF={off:.4f}  ON={on:.4f}  delta={delta:+.4f}  -> {sym}")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "rows": [asdict(r) for r in rows],
        "by_merge_mb_cam": {f"{m}|{mb}|cam{c}": v
                            for (m, mb), d in by.items() for c, v in d.items()},
        "mb_multipliers": {f"{m}|cam{c}": v
                           for c, dm in mb_mults.items() for m, v in dm.items()},
        "spreads_cam2_over_cam0": {f"{m}|{mb}": s for (m, mb), s in spreads.items()},
        "verdict_mb_uniformity": verdict,
        "verdict_detail": detail,
    }
    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT_JSON}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
