"""MBRC v2.0 (h.c)' spatial-trilinear A/B at cam0 + cam2.

Reads 4 cells (2 NEW at ST=0 from captures_h6_spatial_trilinear/, 2 REUSED
at ST=1 from captures_h4_smoothstep/ alcove_cam{0,2}_blend_smoothstep_*).

Computes per cell: cascade/PT energy ratio (single-bounce GI = pt_full - pt_direct).
Then per cam: delta ratio between ST=1 and ST=0.
Then spread delta: cam2/cam0 spread at ST=0 vs ST=1.

Pre-committed verdict bands (per h6 capture script header):

  delta = spread(ST=0) - spread(ST=1)   # spread = cam2_ratio / cam0_ratio
  Positive delta means ST=0 shrinks the cam0/cam2 gap.

    >= +0.10  -> SPATIAL_TRILINEAR_PRIMARY_CONTRIBUTOR
    +0.03..+0.10 -> SPATIAL_TRILINEAR_PARTIAL_CONTRIBUTOR
    -0.03..+0.03 -> SPATIAL_TRILINEAR_NOT_THE_DRIVER
    <= -0.03 -> SPATIAL_TRILINEAR_WIDENS_SPREAD

Shape-asymmetry sub-check (cerebrum DNR 2026-05-24): per-cam ratio deltas
reported independently. If cam0 also moves significantly under ST=0, the
"fract drives the asymmetry" framing weakens (cam0's fract is near-uniform
so ST=0 should change cam0 little).
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
CAP_ST0 = os.path.join(HERE, "captures_h6_spatial_trilinear")
CAP_ST1 = os.path.join(HERE, "captures_h4_smoothstep")
OUT    = os.path.join(HERE, "h6_spatial_trilinear_results.json")
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
    st:  int   # spatial-trilinear setting (0 or 1)
    energy_ratio: float
    integrated_pt: float
    integrated_cascade: float


def stem_for(cam: int, st: int) -> str:
    if st == 1:
        return os.path.join(CAP_ST1, f"alcove_cam{cam}_blend_smoothstep_M0_b2_mboff_m17")
    return os.path.join(CAP_ST0, f"alcove_cam{cam}_st0_M0_b2_mboff_m17")


def analyze(cam: int, st: int) -> Row | None:
    stem = stem_for(cam, st)
    paths = {"gi":  f"{stem}_cascade_gi.exr",
             "ptF": f"{stem}_pt_full.exr",
             "ptD": f"{stem}_pt_direct.exr"}
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam} st={st}: missing {k} ({p})"); return None
    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt  = lum(ptGI); Lc = lum(casc)
    valid = Lpt > EPS_PT
    if valid.sum() == 0: return None
    ipt = float(Lpt[valid].sum()); ic = float(Lc[valid].sum())
    return Row(cam=cam, st=st,
               energy_ratio=(ic / ipt) if ipt > 0 else float("nan"),
               integrated_pt=ipt, integrated_cascade=ic)


def classify(delta_spread: float) -> tuple[str, str]:
    detail = f"delta spread(ST=0) - spread(ST=1) = {delta_spread:+.4f}"
    if delta_spread >= 0.10:
        return "SPATIAL_TRILINEAR_PRIMARY_CONTRIBUTOR", (
            f"{detail}. Disabling spatial trilinear materially closes the cam0/cam2 "
            f"gap -> fract-bias -> trilinear-weight chain is the live driver of "
            f"cam2 under-supply. Mitigation: ship spatial-trilinear=0 default, OR "
            f"redesign trilinear weighting to be less projection-sensitive.")
    if delta_spread >= 0.03:
        return "SPATIAL_TRILINEAR_PARTIAL_CONTRIBUTOR", (
            f"{detail}. Small but measurable symmetrization under nearest-parent. "
            f"Fract chain contributes to the spread but other layers also matter; "
            f"per-direction-bin sampling is the next layer to probe.")
    if delta_spread >= -0.03:
        return "SPATIAL_TRILINEAR_NOT_THE_DRIVER", (
            f"{detail}. Spatial trilinear is INNOCENT of cam0/cam2 spread; fract "
            f"distribution is a red herring. The asymmetry lives in the "
            f"per-direction-bin atlas content at cam2-visible probes. Pivot to "
            f"per-direction-bin energy histogram and/or dominant-bin viz mode.")
    return "SPATIAL_TRILINEAR_WIDENS_SPREAD", (
        f"{detail}. Nearest-parent makes cam2 WORSE — ST=1 was partially "
        f"compensating an upstream issue, OR the single nearest-parent probe "
        f"selected at cam2 is unusually dim. Investigate the probe selected and "
        f"its direction-bin contents directly.")


def main() -> int:
    rows: list[Row] = []
    for st in (1, 0):
        for cam in (0, 2):
            r = analyze(cam, st)
            if r is not None: rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'ST':>3} {'cam':>3} {'integPT':>9} {'integCasc':>10} {'casc/PT':>9}")
    print("-" * 45)
    for r in rows:
        print(f"{r.st:>3} {r.cam:>3} {r.integrated_pt:>9.3f} "
              f"{r.integrated_cascade:>10.3f} {r.energy_ratio:>9.4f}")

    by = {(r.st, r.cam): r.energy_ratio for r in rows}

    print()
    print("=== Per-cam ratio delta: ST=0 vs ST=1 ===")
    print(f"{'cam':>3} {'ratio_st1':>10} {'ratio_st0':>10} {'delta':>9} {'pct':>8}")
    print("-" * 50)
    per_cam_delta = {}
    for cam in (0, 2):
        r1 = by.get((1, cam)); r0 = by.get((0, cam))
        if r1 is None or r0 is None: continue
        d = r0 - r1
        pct = (d / r1 * 100.0) if r1 != 0 else float("nan")
        per_cam_delta[cam] = d
        print(f"{cam:>3} {r1:>10.4f} {r0:>10.4f} {d:>+9.4f} {pct:>+7.2f}%")

    print()
    print("=== Spread (cam2 / cam0) per ST setting ===")
    print(f"{'ST':>3} {'cam0':>9} {'cam2':>9} {'c2/c0':>9}")
    print("-" * 40)
    spreads = {}
    for st in (1, 0):
        r0 = by.get((st, 0)); r2 = by.get((st, 2))
        if r0 is None or r2 is None or r0 < 1e-6: continue
        s = r2 / r0
        spreads[st] = s
        print(f"{st:>3} {r0:>9.4f} {r2:>9.4f} {s:>9.4f}")

    s1 = spreads.get(1); s0 = spreads.get(0)
    delta_spread = (s0 - s1) if (s0 is not None and s1 is not None) else float("nan")

    print()
    if np.isfinite(delta_spread):
        verdict, detail = classify(delta_spread)
        print(f"VERDICT: {verdict}")
        print(f"detail: {detail}")
    else:
        verdict, detail = "INCOMPLETE", "missing one or both spread values"
        print(f"VERDICT: {verdict}")

    # Shape-asymmetry sub-check: how much did cam0 vs cam2 each move?
    print()
    print("=== Shape-asymmetry sub-check (cerebrum DNR 2026-05-24) ===")
    if 0 in per_cam_delta and 2 in per_cam_delta:
        d0 = per_cam_delta[0]; d2 = per_cam_delta[2]
        print(f"  cam0 ratio moved {d0:+.4f}, cam2 ratio moved {d2:+.4f}")
        if abs(d0) > 0.05 and abs(d2 - d0) < 0.05:
            print("  ! both cams moved similarly -> ST flip changes the WHOLE pipeline, "
                  "not specifically a cam2-only fract effect. Interpretation weakens.")
        elif abs(d2) > 2.0 * abs(d0):
            print("  cam2 moved >= 2x more than cam0 in absolute terms -> consistent with "
                  "fract-bias -> trilinear-chain framing (cam0 fract was near-uniform so "
                  "ST=0 should leave cam0 mostly unchanged).")
        else:
            print("  ambiguous: deltas are comparable in magnitude. Interpretation requires "
                  "the per-direction-bin viz to be definitive.")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "rows": [asdict(r) for r in rows],
        "ratios": {f"st{st}|cam{c}": v for (st, c), v in by.items()},
        "per_cam_delta_st0_minus_st1": {f"cam{c}": v for c, v in per_cam_delta.items()},
        "spreads_cam2_over_cam0_by_st": spreads,
        "delta_spread_st0_minus_st1": delta_spread,
        "verdict": verdict,
        "verdict_detail": detail,
    }
    with open(OUT, "w") as f: json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
