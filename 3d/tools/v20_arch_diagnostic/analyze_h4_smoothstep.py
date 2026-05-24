"""MBRC v2.0 (h.b) smoothstep blend-zone toggle A/B.

Reads 6 cells from captures_h4_smoothstep/ (M0, MB-OFF, b=2):
    alcove_cam{0,2}_blend_{smoothstep,linear,step}_M0_b2_mboff_m17_*.exr

Computes per cell: cascade/PT energy ratio (single-bounce GI = pt_full - pt_direct).
Then per cam: delta-vs-smoothstep for linear and step modes.
Then per blend mode: cam2/cam0 spread.

Pre-committed verdict bands (per script header in h4_smoothstep_capture.ps1):

  On cam2, abs delta vs smoothstep baseline:
    <= 0.02 -> BLEND_ZONE_NOT_THE_BUG          (pivot to atlas / probe-cell)
    0.02..0.10 -> BLEND_ZONE_PARTIAL_CONTRIBUTOR
    > 0.10 -> BLEND_ZONE_PRIMARY_SUSPECT
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
CAP  = os.path.join(HERE, "captures_h4_smoothstep")
OUT  = os.path.join(HERE, "h4_smoothstep_results.json")
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
    cam: int
    mode: str
    energy_ratio: float
    integrated_pt: float
    integrated_cascade: float


def analyze(cam: int, mode: str) -> Row | None:
    stem = os.path.join(CAP, f"alcove_cam{cam}_blend_{mode}_M0_b2_mboff_m17")
    paths = {"gi": f"{stem}_cascade_gi.exr",
             "ptF": f"{stem}_pt_full.exr",
             "ptD": f"{stem}_pt_direct.exr"}
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam} mode={mode}: missing {k} ({p})"); return None
    casc = read_exr_rgb(paths["gi"])
    ptF = read_exr_rgb(paths["ptF"])
    ptD = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt = lum(ptGI); Lc = lum(casc)
    valid = Lpt > EPS_PT
    if valid.sum() == 0: return None
    ipt = float(Lpt[valid].sum()); ic = float(Lc[valid].sum())
    return Row(cam=cam, mode=mode,
               energy_ratio=(ic / ipt) if ipt > 0 else float("nan"),
               integrated_pt=ipt, integrated_cascade=ic)


def classify(cam2_deltas: dict[str, float]) -> tuple[str, str]:
    """cam2_deltas[mode] = |ratio(mode) - ratio(smoothstep)| for mode in {linear, step}."""
    if not cam2_deltas:
        return "INCOMPLETE", "no cam2 deltas computed"
    max_delta = max(cam2_deltas.values())
    detail = f"cam2 abs-deltas vs smoothstep: {cam2_deltas}"
    if max_delta <= 0.02:
        return "BLEND_ZONE_NOT_THE_BUG", (
            f"smoothstep S-curve doesn't move cam2 ratio (max |delta| {max_delta:.4f} <= 0.02). "
            f"Blend zone math is innocent; pivot to (c) atlas content / probe-cell oversampling. " + detail)
    if max_delta <= 0.10:
        return "BLEND_ZONE_PARTIAL_CONTRIBUTOR", (
            f"smoothstep contributes a small leak to cam2 under-supply (max |delta| {max_delta:.4f}). "
            f"Useful adjustment but not sufficient to close cam0/cam2 spread alone. " + detail)
    return "BLEND_ZONE_PRIMARY_SUSPECT", (
        f"smoothstep materially under-supplies cam2 (max |delta| {max_delta:.4f} > 0.10). "
        f"Replacing default with linear/adaptive blend should be top priority. " + detail)


def main() -> int:
    modes = ["smoothstep", "linear", "step"]
    rows: list[Row] = []
    for m in modes:
        for cam in (0, 2):
            r = analyze(cam, m)
            if r is not None: rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'mode':<12} {'cam':>3} {'integPT':>9} {'integCasc':>10} {'casc/PT':>9}")
    print("-" * 50)
    for r in rows:
        print(f"{r.mode:<12} {r.cam:>3} {r.integrated_pt:>9.3f} "
              f"{r.integrated_cascade:>10.3f} {r.energy_ratio:>9.4f}")

    # Index by (mode, cam) -> ratio
    by = {(r.mode, r.cam): r.energy_ratio for r in rows}

    print()
    print("=== Delta vs smoothstep baseline (per cam, per non-smoothstep mode) ===")
    print(f"{'cam':>3} {'mode':<10} {'ratio':>9} {'delta':>9} {'abs':>9}")
    print("-" * 50)
    cam_deltas: dict[int, dict[str, float]] = {0: {}, 2: {}}
    for cam in (0, 2):
        base = by.get(("smoothstep", cam))
        if base is None: continue
        for m in ("linear", "step"):
            v = by.get((m, cam))
            if v is None: continue
            d = v - base
            cam_deltas[cam][m] = abs(d)
            print(f"{cam:>3} {m:<10} {v:>9.4f} {d:>+9.4f} {abs(d):>9.4f}")

    print()
    print("=== Spread (cam2/cam0 ratio) per blend mode ===")
    print(f"{'mode':<12} {'cam0':>9} {'cam2':>9} {'c2/c0':>9}")
    print("-" * 50)
    spreads = {}
    for m in modes:
        r0 = by.get((m, 0)); r2 = by.get((m, 2))
        if r0 is None or r2 is None or r0 < 1e-6: continue
        s = r2 / r0
        spreads[m] = s
        print(f"{m:<12} {r0:>9.4f} {r2:>9.4f} {s:>9.4f}")

    # Verdict driven by cam2 (the under-supplied camera)
    verdict, detail = classify(cam_deltas.get(2, {}))
    print()
    print(f"VERDICT (smoothstep responsibility): {verdict}")
    print(f"detail: {detail}")

    # Spread comparison
    print()
    print("=== Spread comparison vs smoothstep baseline ===")
    base_s = spreads.get("smoothstep")
    if base_s is not None:
        for m in ("linear", "step"):
            s = spreads.get(m)
            if s is None: continue
            ds = s - base_s
            sym = ("MODE SYMMETRIZES" if ds > 0.05
                   else "MODE ANTI-SYMMETRIZES" if ds < -0.05
                   else "MODE INVARIANT spread")
            print(f"  {m:<10} c2/c0={s:.4f}  delta={ds:+.4f}  -> {sym}")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "rows": [asdict(r) for r in rows],
        "ratios": {f"{m}|cam{c}": v for (m, c), v in by.items()},
        "cam_abs_deltas_vs_smoothstep": {f"cam{c}|{m}": v
                                         for c, d in cam_deltas.items() for m, v in d.items()},
        "spreads_cam2_over_cam0": spreads,
        "verdict_smoothstep_responsibility": verdict,
        "verdict_detail": detail,
    }
    with open(OUT, "w") as f: json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
