"""MBRC v2.0-pre engine-default validation analyzer (doc/7/alpha_m4_deepdive_impl.md §8.2 blockers).

Reads mode-0 (full composite) PNG pairs from captures_engine_default/ and
computes per-pixel deltas between the current default (M0+MBoff) and the
proposed new default (M4+MBon g=1.0 D8).

Per scene+cam pair, reports:
  - mean L (Rec.709 luminance) for each config + abs/relative delta
  - per-channel mean delta (does color bleed appear in composite?)
  - % pixels brightened (delta > +5%), darkened (delta < -5%), unchanged
  - max delta + p95 delta magnitude (firefly indicator in composite)
  - bright-clip pct (pixels > 0.95) — does M4+MB overexpose direct-lit regions?

Usage:
  python tools/v20_pre_measurement/analyze_engine_default.py
"""
from __future__ import annotations
import os, sys, json, glob
from dataclasses import dataclass, asdict
import numpy as np

try:
    from PIL import Image
except ImportError:
    print("ERROR: pip install Pillow", file=sys.stderr); sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
CAPDIR = os.path.join(HERE, "captures_engine_default")


def load_png(path: str) -> np.ndarray:
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
    return img


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


@dataclass
class Pair:
    scene: str
    cam: int
    base_meanL: float
    rec_meanL: float
    abs_dL: float       # rec - base
    rel_dL_pct: float   # 100 * (rec - base) / max(base, eps)
    dR_mean: float
    dG_mean: float
    dB_mean: float
    pct_brightened: float
    pct_darkened: float
    pct_unchanged: float
    max_dL: float
    p95_abs_dL: float
    base_bright_clip_pct: float
    rec_bright_clip_pct: float


def analyze_pair(scene: str, cam: int) -> Pair | None:
    base_path = os.path.join(CAPDIR, f"{scene}_cam{cam}_baseline_m0.png")
    rec_path  = os.path.join(CAPDIR, f"{scene}_cam{cam}_recommend_m0.png")
    if not os.path.exists(base_path) or not os.path.exists(rec_path):
        print(f"[skip] {scene} cam{cam}: missing PNG"); return None
    base = load_png(base_path); rec = load_png(rec_path)
    if base.shape != rec.shape:
        print(f"[skip] {scene} cam{cam}: shape mismatch {base.shape} vs {rec.shape}"); return None
    Lb = lum(base); Lr = lum(rec)
    dL = Lr - Lb
    eps = 1e-3
    brightened = (dL > 0.05 * np.maximum(Lb, eps))
    darkened   = (dL < -0.05 * np.maximum(Lb, eps))
    unchanged  = ~(brightened | darkened)
    n = Lb.size
    return Pair(
        scene=scene, cam=cam,
        base_meanL=float(Lb.mean()), rec_meanL=float(Lr.mean()),
        abs_dL=float(dL.mean()),
        rel_dL_pct=100.0 * float(dL.mean()) / max(float(Lb.mean()), eps),
        dR_mean=float((rec[..., 0] - base[..., 0]).mean()),
        dG_mean=float((rec[..., 1] - base[..., 1]).mean()),
        dB_mean=float((rec[..., 2] - base[..., 2]).mean()),
        pct_brightened=100.0 * float(brightened.sum()) / n,
        pct_darkened  =100.0 * float(darkened.sum())   / n,
        pct_unchanged =100.0 * float(unchanged.sum())  / n,
        max_dL=float(dL.max()),
        p95_abs_dL=float(np.quantile(np.abs(dL), 0.95)),
        base_bright_clip_pct=100.0 * float((Lb > 0.95).sum()) / n,
        rec_bright_clip_pct =100.0 * float((Lr > 0.95).sum()) / n,
    )


def fmt_pair(p: Pair) -> str:
    return (
        f"  {p.scene:<8s} cam{p.cam}  meanL base={p.base_meanL:.4f} rec={p.rec_meanL:.4f}  "
        f"dL={p.abs_dL:+.4f} ({p.rel_dL_pct:+.1f}%)  "
        f"dR/dG/dB={p.dR_mean:+.3f}/{p.dG_mean:+.3f}/{p.dB_mean:+.3f}  "
        f"bright={p.pct_brightened:5.1f}% dark={p.pct_darkened:5.1f}% same={p.pct_unchanged:5.1f}%  "
        f"max_dL={p.max_dL:+.3f} |p95|={p.p95_abs_dL:.3f}  "
        f"clip base={p.base_bright_clip_pct:4.1f}%->rec={p.rec_bright_clip_pct:4.1f}%"
    )


PAIRS = [
    ("alcove", 0), ("alcove", 2),
    ("plain",  0), ("plain",  2),
    ("sponza", 0),
]


def main() -> int:
    rows: list[Pair] = []
    print("===== Engine-default validation: M0+MBoff vs M4+MBon g=1.0 D8 (mode 0) =====")
    for scene, cam in PAIRS:
        p = analyze_pair(scene, cam)
        if p is None: continue
        print(fmt_pair(p)); rows.append(p)
    if not rows:
        print("no pairs analyzed"); return 1

    # Aggregates
    print("\n===== Aggregates =====")
    rels = [r.rel_dL_pct for r in rows]
    print(f"  mean rel dL across {len(rows)} pairs: {np.mean(rels):+.1f}%  "
          f"(range {min(rels):+.1f}% .. {max(rels):+.1f}%)")
    print(f"  mean % brightened: {np.mean([r.pct_brightened for r in rows]):.1f}%  "
          f"mean % darkened: {np.mean([r.pct_darkened for r in rows]):.1f}%")
    print(f"  bright-clip increase: {np.mean([r.rec_bright_clip_pct - r.base_bright_clip_pct for r in rows]):+.2f}pp")

    # Ship/no-ship verdict gates
    print("\n===== Ship/no-ship verdict gates =====")
    # Gate G1: mode 0 composite must show >= +5% mean luminance lift on at least one cam per scene
    by_scene = {}
    for r in rows: by_scene.setdefault(r.scene, []).append(r)
    for scene, prs in by_scene.items():
        max_lift = max(p.rel_dL_pct for p in prs)
        verdict = "PASS" if max_lift >= 5.0 else ("MARGINAL" if max_lift >= 1.0 else "NO-LIFT")
        print(f"  G1 {scene}: max mode-0 lift = {max_lift:+.1f}% -> {verdict}")
    # Gate G2: clip increase < 5pp anywhere (no catastrophic over-exposure)
    max_clip = max(r.rec_bright_clip_pct - r.base_bright_clip_pct for r in rows)
    print(f"  G2 max bright-clip increase: {max_clip:+.2f}pp -> "
          f"{'PASS' if max_clip < 5.0 else 'FAIL'}")
    # Gate G3: no scene catastrophically darkened (mean rel dL > -2% everywhere)
    min_lift = min(r.rel_dL_pct for r in rows)
    print(f"  G3 min mode-0 lift across all pairs: {min_lift:+.1f}% -> "
          f"{'PASS' if min_lift > -2.0 else 'FAIL'}")

    # JSON dump
    out = os.path.join(HERE, "engine_default_results.json")
    with open(out, "w") as f:
        json.dump([asdict(r) for r in rows], f, indent=2)
    print(f"\n[wrote] {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
