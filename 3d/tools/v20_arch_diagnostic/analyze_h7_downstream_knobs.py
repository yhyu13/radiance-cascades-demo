"""MBRC v2.0 (h.c)'' downstream-knobs final rule-out analyzer.

Reads 6 cells (4 NEW from captures_h7_downstream/, 2 REUSED from
captures_h4_smoothstep/ as DM=1 DB=1 ST=1 baseline) and computes
per-config cascade/PT ratio + per-config spread cam2/cam0.

Configs:
  dm1db1 — default (per-direction-bin + bilinear, baseline from h4)
  dm0    — isotropic fallback (bypasses per-direction sampling entirely)
  dm1db0 — per-direction-bin + nearest (no bilinear inside lookup)

Pre-committed verdict (per h7 capture script):
  delta = spread(cfg) - spread(dm1db1)
  Positive delta means the knob CLOSES the gap (= contributor).
  Negative/zero delta means the knob is INNOCENT.

If BOTH non-default configs land INNOCENT (|delta| < 0.03), the entire
downstream consumption path is ruled out and the asymmetry source is
locked-in to BAKE-SIDE per-direction-bin atlas content.
"""
from __future__ import annotations
import os, json, sys
from dataclasses import dataclass, asdict
import numpy as np

try:
    import OpenEXR, Imath
except ImportError:
    print("ERROR: pip install OpenEXR", file=sys.stderr); sys.exit(2)


HERE     = os.path.dirname(os.path.abspath(__file__))
CAP_H7   = os.path.join(HERE, "captures_h7_downstream")
CAP_BASE = os.path.join(HERE, "captures_h4_smoothstep")
OUT      = os.path.join(HERE, "h7_downstream_knobs_results.json")
EPS_PT   = 1e-3


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
    cfg: str
    cam: int
    energy_ratio: float
    integrated_pt: float
    integrated_cascade: float


def stem_for(cfg: str, cam: int) -> str:
    if cfg == "dm1db1":
        return os.path.join(CAP_BASE, f"alcove_cam{cam}_blend_smoothstep_M0_b2_mboff_m17")
    return os.path.join(CAP_H7, f"alcove_cam{cam}_{cfg}_M0_b2_mboff_m17")


def analyze(cfg: str, cam: int) -> Row | None:
    stem = stem_for(cfg, cam)
    paths = {"gi":  f"{stem}_cascade_gi.exr",
             "ptF": f"{stem}_pt_full.exr",
             "ptD": f"{stem}_pt_direct.exr"}
    for k, p in paths.items():
        if not os.path.exists(p):
            print(f"[skip] cfg={cfg} cam{cam}: missing {k} ({p})"); return None
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
    return Row(cfg=cfg, cam=cam,
               energy_ratio=(ic / ipt) if ipt > 0 else float("nan"),
               integrated_pt=ipt, integrated_cascade=ic)


def classify(delta: float) -> str:
    if delta >= 0.10: return "DOWNSTREAM_KNOB_PRIMARY_DRIVER"
    if delta >= 0.03: return "DOWNSTREAM_KNOB_PARTIAL_CONTRIBUTOR"
    return "DOWNSTREAM_KNOB_INNOCENT"


def main() -> int:
    cfgs = ["dm1db1", "dm0", "dm1db0"]
    rows: list[Row] = []
    for c in cfgs:
        for cam in (0, 2):
            r = analyze(c, cam)
            if r is not None: rows.append(r)
    if not rows:
        print("ERROR: no captures found", file=sys.stderr); return 1

    print()
    print(f"{'cfg':<8} {'cam':>3} {'integPT':>9} {'integCasc':>10} {'casc/PT':>9}")
    print("-" * 48)
    for r in rows:
        print(f"{r.cfg:<8} {r.cam:>3} {r.integrated_pt:>9.3f} "
              f"{r.integrated_cascade:>10.3f} {r.energy_ratio:>9.4f}")

    by = {(r.cfg, r.cam): r.energy_ratio for r in rows}

    print()
    print("=== Per-cam ratio delta vs dm1db1 baseline ===")
    print(f"{'cfg':<8} {'cam':>3} {'baseline':>9} {'config':>9} {'delta':>9} {'pct':>8}")
    print("-" * 56)
    per_cfg_per_cam: dict[str, dict[int, float]] = {}
    for c in cfgs:
        if c == "dm1db1": continue
        per_cfg_per_cam[c] = {}
        for cam in (0, 2):
            base = by.get(("dm1db1", cam))
            cur  = by.get((c, cam))
            if base is None or cur is None: continue
            d = cur - base
            pct = (d / base * 100.0) if base != 0 else float("nan")
            per_cfg_per_cam[c][cam] = d
            print(f"{c:<8} {cam:>3} {base:>9.4f} {cur:>9.4f} "
                  f"{d:>+9.4f} {pct:>+7.2f}%")

    print()
    print("=== Spread cam2/cam0 per config + delta vs baseline ===")
    print(f"{'cfg':<8} {'cam0':>9} {'cam2':>9} {'c2/c0':>9} {'delta':>9}")
    print("-" * 50)
    spreads = {}
    base_spread = None
    for c in cfgs:
        r0 = by.get((c, 0)); r2 = by.get((c, 2))
        if r0 is None or r2 is None or r0 < 1e-6: continue
        s = r2 / r0
        spreads[c] = s
        if c == "dm1db1":
            base_spread = s
            print(f"{c:<8} {r0:>9.4f} {r2:>9.4f} {s:>9.4f} {'(base)':>9}")
        else:
            d = s - base_spread if base_spread is not None else float("nan")
            print(f"{c:<8} {r0:>9.4f} {r2:>9.4f} {s:>9.4f} {d:>+9.4f}")

    print()
    print("=== Verdict per non-default config ===")
    verdicts = {}
    for c in cfgs:
        if c == "dm1db1": continue
        s = spreads.get(c)
        if s is None or base_spread is None: continue
        delta = s - base_spread
        v = classify(delta)
        verdicts[c] = {"delta_spread": delta, "verdict": v}
        print(f"  {c:<8} delta_spread={delta:+.4f} -> {v}")

    # Composite verdict: are both non-default configs INNOCENT?
    all_innocent = all(v["verdict"] == "DOWNSTREAM_KNOB_INNOCENT" for v in verdicts.values())
    print()
    if all_innocent and len(verdicts) > 0:
        print("COMPOSITE VERDICT: DOWNSTREAM_PATH_LOCKED_IN_INNOCENT")
        print("  Both remaining downstream knobs are innocent. Combined with")
        print("  (h.b) blend ruled out + (h.c)' spatial-trilinear is symmetrizer,")
        print("  the entire downstream consumption path is ruled out. Asymmetry")
        print("  source MUST be bake-side per-direction-bin atlas content.")
    else:
        contrib = [c for c, v in verdicts.items() if v["verdict"] != "DOWNSTREAM_KNOB_INNOCENT"]
        print(f"COMPOSITE VERDICT: DOWNSTREAM_KNOB_CONTRIBUTES: {contrib}")
        print("  At least one downstream knob meaningfully affects the cam0/cam2")
        print("  spread. Bake-side framing weakened; investigate the contributing")
        print("  knob before pivoting to per-direction-bin viz.")

    out = {
        "epsilon_pt_luminance": EPS_PT,
        "rows": [asdict(r) for r in rows],
        "ratios": {f"{c}|cam{cam}": v for (c, cam), v in by.items()},
        "spreads_cam2_over_cam0": spreads,
        "per_config_verdicts": verdicts,
        "composite_verdict": ("DOWNSTREAM_PATH_LOCKED_IN_INNOCENT"
                              if all_innocent else "DOWNSTREAM_KNOB_CONTRIBUTES"),
        "per_cfg_per_cam_delta": {c: {f"cam{k}": v for k, v in d.items()}
                                  for c, d in per_cfg_per_cam.items()},
    }
    with open(OUT, "w") as f: json.dump(out, f, indent=2)
    print(f"\n[wrote] {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
