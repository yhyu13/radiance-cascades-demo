"""MBRC v2.0-pre HDR re-litigation analyzer (doc/7/hdr_exr_metric_impl.md sec 4.1).

Reads the three captures_hdr_{alpha,beta,gamma}/ directories produced by
hdr_relitigate_sweep.ps1 and emits per-hypothesis tables comparing each
arm's HDR statistics to its baseline. The HDR honest-metric replay of
(delta) already showed that LDR DELTA_REJECT was a measurement artifact;
this script extends the same cross-check to the other three rejections.

Pre-committed re-litigation rule (per-cam, vs baseline at same cam):
  HDR_LEVERAGE  : meanCasc/meanPT changes by >= 20% on either cam, OR
                  signed_p50 magnitude changes by >= 20% on either cam.
  HDR_TIE       : both deltas within +/-10% on both cams.
  HDR_MIXED     : in between (cam-asymmetric weak signal).

These bars are *deliberately strict* -- if the LDR floor was hiding
sub-tonemap signal, the HDR data will show movement above 20% on
the metrics LDR couldn't see. If it doesn't, the LDR verdict stands.

Usage:
  python tools/v20_pre_measurement/analyze_hdr_relitigate.py
"""
from __future__ import annotations
import os, sys, re, json
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
class HdrStats:
    label: str
    cam: int
    arm: str
    valid_pct: float
    p50_rel: float
    abs_p50: float
    abs_p95: float
    mean_pt: float
    mean_casc: float
    ratio: float        # mean_casc / mean_pt
    dim_pct: float      # cascade < 0.5x PT
    bright_pct: float   # cascade > 2x PT


def analyze_capture(stem: str, cam: int, arm: str) -> Optional[HdrStats]:
    paths = {
        "gi":  f"{stem}_cascade_gi.exr",
        "ptF": f"{stem}_pt_full.exr",
        "ptD": f"{stem}_pt_direct.exr",
    }
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] {os.path.basename(stem)}: missing {k} ({p})")
            return None

    casc = read_exr_rgb(paths["gi"])
    ptF  = read_exr_rgb(paths["ptF"])
    ptD  = read_exr_rgb(paths["ptD"])
    if casc.shape[:2] != ptF.shape[:2]:
        casc = downsample_2x2_avg(casc)

    ptGI = np.clip(ptF - ptD, 0.0, None)
    Lpt  = lum(ptGI); Lc = lum(casc)
    valid = Lpt > EPS_PT
    n = int(valid.sum())
    if n == 0:
        return None

    rel = (Lc[valid] - Lpt[valid]) / Lpt[valid]
    ratio = Lc[valid] / Lpt[valid]
    mean_pt = float(Lpt[valid].mean())
    mean_casc = float(Lc[valid].mean())
    return HdrStats(
        label=os.path.basename(stem), cam=cam, arm=arm,
        valid_pct=100.0 * n / Lpt.size,
        p50_rel=float(np.quantile(rel, 0.50)),
        abs_p50=float(np.quantile(np.abs(rel), 0.50)),
        abs_p95=float(np.quantile(np.abs(rel), 0.95)),
        mean_pt=mean_pt, mean_casc=mean_casc,
        ratio=(mean_casc / mean_pt) if mean_pt > 0 else 0.0,
        dim_pct=100.0 * float((ratio < 0.5).sum()) / n,
        bright_pct=100.0 * float((ratio > 2.0).sum()) / n,
    )


def collect_dir(dirname: str, arm_regex: str, arm_group: int) -> list[HdrStats]:
    """Scan dirname for *_cascade_gi.exr files; parse arm via regex."""
    d = os.path.join(HERE, dirname)
    if not os.path.isdir(d):
        print(f"[skip] missing dir: {d}", file=sys.stderr)
        return []
    rows: list[HdrStats] = []
    pat = re.compile(arm_regex)
    for fn in sorted(os.listdir(d)):
        if not fn.endswith("_cascade_gi.exr"):
            continue
        stem_full = fn[: -len("_cascade_gi.exr")]
        m = pat.match(stem_full)
        if not m:
            continue
        cam = int(m.group(1))
        arm = m.group(arm_group)
        stem = os.path.join(d, stem_full)
        s = analyze_capture(stem, cam, arm)
        if s is not None:
            rows.append(s)
    return rows


def fmt_row(s: HdrStats) -> str:
    return (f"  cam{s.cam} {s.arm:>20s}  valid={s.valid_pct:5.1f}%  "
            f"p50={s.p50_rel:+.3f}  |p50|={s.abs_p50:.3f}  |p95|={s.abs_p95:5.3f}  "
            f"meanPT={s.mean_pt:.4f}  meanCasc={s.mean_casc:.4f}  "
            f"ratio={s.ratio:.3f}  dim={s.dim_pct:4.1f}%  bright={s.bright_pct:4.1f}%")


def baseline_compare(rows: list[HdrStats], baseline_arm: str, hypo: str) -> dict:
    """For each non-baseline arm, compute (ratio delta %, |p50| delta %) per cam."""
    by_cam_arm: dict[tuple[int, str], HdrStats] = {(r.cam, r.arm): r for r in rows}
    cams = sorted({r.cam for r in rows})
    arms = [r.arm for r in rows if r.cam == cams[0]]
    deltas = {}
    for arm in arms:
        if arm == baseline_arm:
            continue
        d_per_cam = {}
        for cam in cams:
            b = by_cam_arm.get((cam, baseline_arm))
            r = by_cam_arm.get((cam, arm))
            if b is None or r is None:
                continue
            ratio_dpct = 100.0 * (r.ratio - b.ratio) / max(b.ratio, 1e-6)
            absp50_dpct = 100.0 * (r.abs_p50 - b.abs_p50) / max(b.abs_p50, 1e-6)
            d_per_cam[cam] = {"ratio_dpct": ratio_dpct, "abs_p50_dpct": absp50_dpct,
                              "ratio_b": b.ratio, "ratio_r": r.ratio,
                              "abs_p50_b": b.abs_p50, "abs_p50_r": r.abs_p50}
        # verdict
        max_ratio = max((abs(d["ratio_dpct"])  for d in d_per_cam.values()), default=0.0)
        max_p50   = max((abs(d["abs_p50_dpct"]) for d in d_per_cam.values()), default=0.0)
        max_move  = max(max_ratio, max_p50)
        if max_move >= 20.0:
            verdict = "HDR_LEVERAGE"
        elif max_move <= 10.0:
            verdict = "HDR_TIE"
        else:
            verdict = "HDR_MIXED"
        deltas[arm] = {"per_cam": d_per_cam, "verdict": verdict,
                       "max_ratio_move_pct": max_ratio,
                       "max_abs_p50_move_pct": max_p50}
    return {"hypothesis": hypo, "baseline_arm": baseline_arm, "arms": deltas}


def print_section(title: str, rows: list[HdrStats], compare: dict):
    print(f"\n===== {title} =====")
    print(f"baseline arm: {compare['baseline_arm']}")
    rows_sorted = sorted(rows, key=lambda s: (s.arm, s.cam))
    for s in rows_sorted:
        print(fmt_row(s))
    print()
    for arm, d in compare["arms"].items():
        print(f"  vs baseline -- arm '{arm}':  VERDICT = {d['verdict']}")
        for cam, x in d["per_cam"].items():
            print(f"    cam{cam}: ratio {x['ratio_b']:.3f}->{x['ratio_r']:.3f} "
                  f"({x['ratio_dpct']:+.1f}%)  |p50| {x['abs_p50_b']:.3f}->{x['abs_p50_r']:.3f} "
                  f"({x['abs_p50_dpct']:+.1f}%)")
        print(f"    max move: ratio={d['max_ratio_move_pct']:.1f}%  |p50|={d['max_abs_p50_move_pct']:.1f}%")


def main() -> int:
    # (alpha): cam{N}_{name}_m17  where name = M0_baseline | M1_no_bilin | ...
    alpha_rows = collect_dir(
        "captures_hdr_alpha",
        r"^cam(\d+)_(M\d+_[A-Za-z_]+)_m17$",
        arm_group=2,
    )
    # (beta): cam{N}_g{NNN}_m17 where g = gain*100 (0.5 -> g050)
    beta_rows = collect_dir(
        "captures_hdr_beta",
        r"^cam(\d+)_(g\d+)_m17$",
        arm_group=2,
    )
    # (gamma): cam{N}_d{NN}_m17 where d = uniform-D
    gamma_rows = collect_dir(
        "captures_hdr_gamma",
        r"^cam(\d+)_(d\d+)_m17$",
        arm_group=2,
    )

    # Choose per-hypothesis baselines
    out = {}
    if alpha_rows:
        cmp_a = baseline_compare(alpha_rows, "M0_baseline", "alpha")
        print_section("(alpha) merge-mode HDR", alpha_rows, cmp_a)
        out["alpha"] = {"rows": [asdict(r) for r in alpha_rows], "compare": cmp_a}
    if beta_rows:
        # MB-ON baseline = g100 (gain=1.0 = engine default). LDR sweep was MB ON
        # across all gains; baseline is "physical" gain.
        cmp_b = baseline_compare(beta_rows, "g100", "beta")
        print_section("(beta) MB-gain HDR (baseline = g=1.0)", beta_rows, cmp_b)
        out["beta"] = {"rows": [asdict(r) for r in beta_rows], "compare": cmp_b}
    if gamma_rows:
        # Baseline = engine default scaled-D at the LOWEST uniform value.
        # LDR sweep used D=8 as the comparison anchor for D=16 leverage.
        cmp_g = baseline_compare(gamma_rows, "d08", "gamma")
        print_section("(gamma) angular-bin HDR (baseline = D=8 uniform)", gamma_rows, cmp_g)
        out["gamma"] = {"rows": [asdict(r) for r in gamma_rows], "compare": cmp_g}

    out_path = os.path.join(HERE, "hdr_relitigate_results.json")
    with open(out_path, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\n[wrote] {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
