"""MBRC v2.0-pre (alpha) M4 deep-dive stacking analyzer (doc/7/hdr_relitigation_impl.md sec 6.1).

Reads the 8 NEW M4-stacking captures + the 8 BASELINE cells from the prior
hdr_relitigate_sweep.ps1 output dirs and computes the cascade-vs-PT HDR ratio
on a 2x2x2 stacking grid:

    factors: merge in {M0, M4}  x  MB in {OFF, ON g=1.0}  x  D in {D8scaled, D16uniform}
    metric : meanCascadeGI / meanPTGI over valid-PT pixels (EPS_PT=1e-3)

For each cam, prints:
  1. raw ratio table (8 cells)
  2. main effects: <ratio | factor=X> averaged over the other two factors
  3. pairwise stack ratio (additivity check)
  4. triple-stack ceiling: max ratio observed and whether it exceeds 1.0

The hypothesis tests are:
  H1 (M4 stacks with MB): ratio(M4+MBon) > ratio(M0+MBon) AND > ratio(M4+MBoff)
                          additively or super-additively.
  H2 (M4 saturates):       ratio(M4+MBon+D16) approaches 1.0 (cascade matches PT).
  H3 (MB feedback amplification): ratio(M4+MBon+...) overshoots 1.0 (runaway).
  H4 (D16 is neutral with M4): ratio(M4+D16) ~ ratio(M4+D8).

Usage:
  python tools/v20_pre_measurement/analyze_m4_stack.py
"""
from __future__ import annotations
import os, sys, json
from dataclasses import dataclass, asdict
from typing import Optional
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr); sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
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
class Cell:
    label: str
    cam: int
    merge: str   # "M0" | "M4"
    mb: str      # "OFF" | "ON"
    dcfg: str    # "D8scaled" | "D16uniform"
    valid_pct: float
    p50_rel: float
    abs_p50: float
    abs_p95: float
    mean_pt: float
    mean_casc: float
    ratio: float
    dim_pct: float
    bright_pct: float


def analyze_capture(stem: str, cam: int, merge: str, mb: str, dcfg: str) -> Optional[Cell]:
    paths = {"gi": f"{stem}_cascade_gi.exr",
             "ptF": f"{stem}_pt_full.exr",
             "ptD": f"{stem}_pt_direct.exr"}
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] missing {k}: {p}"); return None
    casc = read_exr_rgb(paths["gi"])
    ptF = read_exr_rgb(paths["ptF"]); ptD = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)
    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt = lum(ptGI); Lc = lum(casc)
    valid = Lpt > EPS_PT
    n = int(valid.sum())
    if n == 0: return None
    rel = (Lc[valid] - Lpt[valid]) / Lpt[valid]
    ratio = Lc[valid] / Lpt[valid]
    mean_pt = float(Lpt[valid].mean()); mean_casc = float(Lc[valid].mean())
    label = f"{merge}_{mb}_{dcfg}"
    return Cell(
        label=label, cam=cam, merge=merge, mb=mb, dcfg=dcfg,
        valid_pct=100.0 * n / Lpt.size,
        p50_rel=float(np.quantile(rel, 0.50)),
        abs_p50=float(np.quantile(np.abs(rel), 0.50)),
        abs_p95=float(np.quantile(np.abs(rel), 0.95)),
        mean_pt=mean_pt, mean_casc=mean_casc,
        ratio=(mean_casc / mean_pt) if mean_pt > 0 else 0.0,
        dim_pct=100.0 * float((ratio < 0.5).sum()) / n,
        bright_pct=100.0 * float((ratio > 2.0).sum()) / n,
    )


# Mapping: (merge, mb, dcfg) -> (subdir, stem_pattern_or_camslot)
# Baseline cells from prior sweeps; NEW cells from m4stack sweep.
CELL_SOURCES = {
    # (merge, mb, dcfg)             (subdir,                 stem template with {cam})
    ("M0", "OFF", "D8scaled"):     ("captures_hdr_alpha",   "cam{cam}_M0_baseline_m17"),
    ("M4", "OFF", "D8scaled"):     ("captures_hdr_alpha",   "cam{cam}_M4_iso_nearest_m17"),
    ("M0", "ON",  "D8scaled"):     ("captures_hdr_beta",    "cam{cam}_g100_m17"),
    ("M0", "OFF", "D16uniform"):   ("captures_hdr_gamma",   "cam{cam}_d16_m17"),
    # NEW from m4stack sweep:
    ("M4", "ON",  "D8scaled"):     ("captures_hdr_m4stack", "cam{cam}_S1_M4_MBon_D8scaled_m17"),
    ("M0", "ON",  "D16uniform"):   ("captures_hdr_m4stack", "cam{cam}_S2_M0_MBon_D16uniform_m17"),
    ("M4", "OFF", "D16uniform"):   ("captures_hdr_m4stack", "cam{cam}_S3_M4_MBoff_D16unif_m17"),
    ("M4", "ON",  "D16uniform"):   ("captures_hdr_m4stack", "cam{cam}_S4_M4_MBon_D16unif_m17"),
}


def collect(cameras=(0, 2)) -> list[Cell]:
    rows: list[Cell] = []
    for (merge, mb, dcfg), (subdir, tmpl) in CELL_SOURCES.items():
        for cam in cameras:
            stem = os.path.join(HERE, subdir, tmpl.format(cam=cam))
            c = analyze_capture(stem, cam, merge, mb, dcfg)
            if c is not None:
                rows.append(c)
    return rows


def fmt_cell(c: Cell) -> str:
    return (f"  cam{c.cam}  {c.merge:>3s}+MB={c.mb:<3s}+{c.dcfg:<11s}  "
            f"ratio={c.ratio:7.3f}  |p50|={c.abs_p50:.3f}  |p95|={c.abs_p95:6.3f}  "
            f"dim={c.dim_pct:5.1f}%  bright={c.bright_pct:5.1f}%")


def main() -> int:
    rows = collect()
    if not rows:
        print("no captures found"); return 1

    # ---- raw table per cam ----
    print("\n===== 2x2x2 stacking grid (8 cells x 2 cams) =====")
    by_cam = {0: [], 2: []}
    for r in rows:
        by_cam[r.cam].append(r)
    for cam in (0, 2):
        cells = sorted(by_cam[cam], key=lambda c: (c.merge, c.mb, c.dcfg))
        print(f"\n--- cam{cam} ---")
        for c in cells:
            print(fmt_cell(c))

    # ---- main effects per cam ----
    print("\n===== Main effects (mean ratio averaged over the other two factors) =====")
    for cam in (0, 2):
        cells = by_cam[cam]
        def mean_where(key, val):
            return float(np.mean([c.ratio for c in cells if getattr(c, key) == val]))
        print(f"\n--- cam{cam} ---")
        print(f"  merge: M0={mean_where('merge','M0'):.3f}  M4={mean_where('merge','M4'):.3f}  "
              f"(Delta M4-M0 = {mean_where('merge','M4')-mean_where('merge','M0'):+.3f})")
        print(f"  MB   : OFF={mean_where('mb','OFF'):.3f}  ON={mean_where('mb','ON'):.3f}  "
              f"(Delta ON-OFF = {mean_where('mb','ON')-mean_where('mb','OFF'):+.3f})")
        print(f"  D    : D8={mean_where('dcfg','D8scaled'):.3f}  D16={mean_where('dcfg','D16uniform'):.3f}  "
              f"(Delta D16-D8 = {mean_where('dcfg','D16uniform')-mean_where('dcfg','D8scaled'):+.3f})")

    # ---- additivity check: sum of singletons vs joint ----
    print("\n===== Stacking-vs-additivity check =====")
    # For each cam: compare (M4_MBon_D8) - (M0_MBoff_D8) [joint stack of two interventions]
    # to [(M4_MBoff_D8 - M0_MBoff_D8) + (M0_MBon_D8 - M0_MBoff_D8)] [sum of singletons]
    for cam in (0, 2):
        def find(merge, mb, dcfg):
            return next((c for c in by_cam[cam]
                         if c.merge == merge and c.mb == mb and c.dcfg == dcfg), None)
        base   = find("M0","OFF","D8scaled")
        m4only = find("M4","OFF","D8scaled")
        mbonly = find("M0","ON", "D8scaled")
        both   = find("M4","ON", "D8scaled")
        if not all([base, m4only, mbonly, both]):
            print(f"  cam{cam}: missing cells for additivity check"); continue
        d_m4   = m4only.ratio - base.ratio
        d_mb   = mbonly.ratio - base.ratio
        d_both = both.ratio   - base.ratio
        d_sum  = d_m4 + d_mb
        nonlin = d_both - d_sum
        print(f"  cam{cam}: base={base.ratio:.3f}  d_M4={d_m4:+.3f}  d_MB={d_mb:+.3f}  "
              f"d_both={d_both:+.3f}  sum={d_sum:+.3f}  nonlin={nonlin:+.3f}")
        if nonlin > 0.05:
            print(f"    -> SUPER-ADDITIVE (M4 + MB stack > sum; +{100*nonlin/d_sum:.1f}% vs sum)")
        elif nonlin < -0.05:
            print(f"    -> SUB-ADDITIVE (M4 + MB stack < sum; {100*nonlin/d_sum:+.1f}% vs sum)")
        else:
            print(f"    -> ADDITIVE-ISH (within +/-0.05)")

    # ---- triple-stack ceiling ----
    print("\n===== Triple-stack (M4+MBon+D16) -- does cascade approach PT? =====")
    for cam in (0, 2):
        t = next((c for c in by_cam[cam]
                  if c.merge == "M4" and c.mb == "ON" and c.dcfg == "D16uniform"), None)
        if t is None:
            print(f"  cam{cam}: missing triple cell"); continue
        gap = abs(1.0 - t.ratio)
        verdict = ("CASCADE_MATCHES_PT" if gap < 0.10
                   else ("FEEDBACK_OVERSHOOT" if t.ratio > 1.10
                         else ("RESIDUAL_GAP_PRESENT" if t.ratio < 0.90
                               else "NEAR_MATCH")))
        print(f"  cam{cam}: triple ratio = {t.ratio:.3f}  gap-from-1.0 = {gap:.3f}  "
              f"=> {verdict}")

    # ---- JSON dump ----
    out_path = os.path.join(HERE, "m4_stack_results.json")
    with open(out_path, "w") as f:
        json.dump([asdict(r) for r in rows], f, indent=2)
    print(f"\n[wrote] {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
