"""MBRC v2.0 (h) source disambiguation analyzer.

Per doc/7/v20_pt_bounce_ladder_impl.md section 6: the bounce-ladder
falsified hypothesis (f) energy-loss and revealed (h) -- at PT b=2
(apples-to-apples single-bounce-equivalent for cascade), cam0 cascade
is +39% over-bright (ratio 1.39) and cam2 is -23% under-bright (ratio
0.77). The 0.62 energy spread on a SINGLE-BOUNCE comparison is the
(h) signal; this analyzer disambiguates its source.

Compares 4 cases (b=2 PT in all):
    captures_pt_bounce_ladder/alcove_cam{0,2}_b2_m17_*        (MB ON)
    captures_h_disambig/alcove_cam{0,2}_b2_mboff_m17_*        (MB OFF)

Pre-committed disambig rule (cam0 cascade/PT ratio at b=2):
  - MB-OFF in [0.90, 1.10]  -> MB_FEEDBACK_IS_SOURCE (h.1 confirmed)
      Cascade single-bounce + first-bounce-merge integrates correctly;
      MB feedback at g=1.0 over-shoots equilibrium on M4_iso_nearest.
      Next target: (beta) g=2.0 runaway numerical-instability root-cause
      OR design MB-feedback stable at safe-higher-g.
  - MB-OFF ratio - MB-ON ratio within +/- 0.10 (both > 1.20)
      -> FIRSTBOUNCE_MERGE_IS_SOURCE (h.2 confirmed)
      MB toggle barely moves the needle; the merge formula at
      radiance_3d.comp:656-682 is over-integrating regardless.
      Next target: M0/M2/M4 stack at cam0/cam2 b=2 to find sweet spot.
  - In-between -> MIXED (both contribute, partial credit each)

Usage: python tools/v20_arch_diagnostic/analyze_h_source_disambig.py
"""

from __future__ import annotations
import os, json, sys
from dataclasses import dataclass, asdict
from typing import Optional
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr); sys.exit(2)


HERE = os.path.dirname(os.path.abspath(__file__))
CAP_MBON  = os.path.join(HERE, "captures_pt_bounce_ladder")
CAP_MBOFF = os.path.join(HERE, "captures_h_disambig")
OUT_JSON  = os.path.join(HERE, "h_source_disambig_results.json")

EPS_PT = 1e-3


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
    mb: str          # "ON" / "OFF"
    sum_pos: float
    sum_neg: float
    abs_ratio: float
    energy_ratio: float    # integrated cascade / integrated PT (apples-to-apples at b=2)
    integrated_pt: float
    integrated_cascade: float


def analyze_pair(cam: int, mb_on: bool) -> Optional[Row]:
    if mb_on:
        stem = os.path.join(CAP_MBON, f"alcove_cam{cam}_b2_m17")
    else:
        stem = os.path.join(CAP_MBOFF, f"alcove_cam{cam}_b2_mboff_m17")
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cam{cam} MB={'ON' if mb_on else 'OFF'}: missing {k} ({p})")
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
    pos_mask = valid & (Ld > 0); neg_mask = valid & (Ld < 0)
    sum_pos = float(Ld[pos_mask].sum())
    sum_neg = float((-Ld[neg_mask]).sum())
    integ_pt = float(Lpt[valid].sum())
    integ_c  = float(Lc[valid].sum())
    return Row(
        cam=cam, mb=("ON" if mb_on else "OFF"),
        sum_pos=sum_pos, sum_neg=sum_neg,
        abs_ratio=(sum_pos / sum_neg) if sum_neg > 1e-9 else float("inf"),
        energy_ratio=(integ_c / integ_pt) if integ_pt > 0 else float("nan"),
        integrated_pt=integ_pt, integrated_cascade=integ_c,
    )


def classify(mb_off_cam0: float, mb_on_cam0: float) -> tuple[str, str]:
    if 0.90 <= mb_off_cam0 <= 1.10:
        return ("MB_FEEDBACK_IS_SOURCE",
                f"MB-OFF cam0 ratio = {mb_off_cam0:.3f} in [0.90, 1.10] "
                f"-> single-bounce cascade is symmetric; MB feedback is the +39% over-source")
    if mb_off_cam0 > 1.20 and abs(mb_off_cam0 - mb_on_cam0) <= 0.10:
        return ("FIRSTBOUNCE_MERGE_IS_SOURCE",
                f"MB-OFF cam0 ratio = {mb_off_cam0:.3f}; MB-ON = {mb_on_cam0:.3f} (delta = "
                f"{abs(mb_off_cam0 - mb_on_cam0):.3f} within +/-0.10) -> first-bounce 3-way merge "
                f"at radiance_3d.comp:656-682 over-integrates regardless of MB")
    return ("MIXED",
            f"MB-OFF cam0 ratio = {mb_off_cam0:.3f}; MB-ON = {mb_on_cam0:.3f} "
            f"-> partial credit each; neither clean attribution")


def main() -> int:
    rows: list[Row] = []
    for cam in (0, 2):
        for mb_on in (True, False):
            r = analyze_pair(cam, mb_on)
            if r is not None:
                rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'cam':>3} {'MB':>4} "
          f"{'Sum+':>10} {'Sum-':>10} {'|+/-|':>7} "
          f"{'integPT':>9} {'integCasc':>10} {'casc/PT':>9}")
    print("-" * 80)
    for r in rows:
        rs = f"{r.abs_ratio:7.3f}" if np.isfinite(r.abs_ratio) else "    inf"
        print(f"{r.cam:>3} {r.mb:>4} "
              f"{r.sum_pos:>10.3f} {r.sum_neg:>10.3f} {rs} "
              f"{r.integrated_pt:>9.3f} {r.integrated_cascade:>10.3f} "
              f"{r.energy_ratio:>9.4f}")

    rmap = {(r.cam, r.mb): r for r in rows}
    if (0, "ON") in rmap and (0, "OFF") in rmap:
        mb_on_e  = rmap[(0, "ON")].energy_ratio
        mb_off_e = rmap[(0, "OFF")].energy_ratio
        verdict, detail = classify(mb_off_e, mb_on_e)
        print()
        print("=== PRE-COMMITTED VERDICT (cam0 energy ratio cascade/PT at b=2) ===")
        print(f"  MB ON  cam0 ratio = {mb_on_e:.4f}")
        print(f"  MB OFF cam0 ratio = {mb_off_e:.4f}")
        print(f"  delta (MB ON - MB OFF) = {mb_on_e - mb_off_e:+.4f}")
        print()
        print(f"  VERDICT: {verdict}")
        print(f"  detail:  {detail}")

        if (2, "ON") in rmap and (2, "OFF") in rmap:
            print()
            print("  cam2 cross-check (not in rule, informational):")
            print(f"    MB ON  cam2 ratio = {rmap[(2, 'ON')].energy_ratio:.4f}")
            print(f"    MB OFF cam2 ratio = {rmap[(2, 'OFF')].energy_ratio:.4f}")
            print(f"    delta = {rmap[(2, 'ON')].energy_ratio - rmap[(2, 'OFF')].energy_ratio:+.4f}")
    else:
        verdict, detail = "INCOMPLETE", "missing required pairs"

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "verdict_thresholds": {
            "mb_source_band":     [0.90, 1.10],
            "firstbounce_min":    1.20,
            "firstbounce_delta_max": 0.10,
        },
        "rows": [asdict(r) for r in rows],
        "verdict": verdict,
        "verdict_detail": detail,
    }
    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT_JSON}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
