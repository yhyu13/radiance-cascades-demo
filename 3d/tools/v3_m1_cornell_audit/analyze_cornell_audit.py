"""M1 Stage 11b — Cornell consumer-side under-brightness audit.

Reuses Stage 8 cornell_g100 cam0 capture + Stage 11b new cam1/cam2 captures.
Computes:
  - per-pixel ratio histogram (cornell, per camera)
  - per-region ratio split by gbuffer albedo bucket
  - per-cell ratio statistics
  - Sponza g=0.10 comparison (consistency check from Stage 10 SC11)
Emits a bake-vs-consumer verdict per plan §5.
"""

import importlib.util
import json
from collections import defaultdict
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_cornell_audit")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
N = 2048
EPS = 1e-6
ATLAS_RES = 32  # C0 atlas resolution for per-cell binning

# Cornell at gain=1.0 per camera. cam0 reused from Stage 8.
CORNELL_CAPTURES = {
    "cam0": {"dir": "tools/v3_m1_source_energy_ab/captures_cornell_baseline",
             "stem": "m1stage8_cornell_baseline_N2048_m17"},
    "cam1": {"dir": "tools/v3_m1_cornell_audit/captures_cornell_cam1",
             "stem": "m1stage11b_cornell_cam1_g100_N2048_m17"},
    "cam2": {"dir": "tools/v3_m1_cornell_audit/captures_cornell_cam2",
             "stem": "m1stage11b_cornell_cam2_g100_N2048_m17"},
}

# Sponza at best gain g=0.10 for consistency check.
SPONZA_REF = {"dir": "tools/v3_m1_mb_gain_ladder/captures_sponza_g010",
              "stem": "m1stage9_sponza_g010_N2048_m17"}


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec); assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def load_capture(mod, cap: dict) -> dict | None:
    d = Path(cap["dir"]); stem = cap["stem"]
    paths = {
        "cascade":   d / f"{stem}_cascade_gi.exr",
        "pt_full":   d / f"{stem}_pt_full.exr",
        "pt_direct": d / f"{stem}_pt_direct.exr",
        "gbuffer":   d / f"{stem}_gbuffer.exr",
        "diag":      d / f"{stem}_probe_diag.exr",
        "contrib":   d / f"{stem}_probe_contrib.exr",
        "bin":       d / f"{stem}_probe_bin.exr",
    }
    for p in paths.values():
        if not p.exists():
            return None
    cascade = mod.read_exr(paths["cascade"])
    pt_full = mod.read_exr(paths["pt_full"])
    pt_direct = mod.read_exr(paths["pt_direct"])
    gbuffer = mod.read_exr(paths["gbuffer"], channels=("R","G","B","A"))
    diag = mod.read_exr(paths["diag"], channels=("R","G","B","A"))
    contrib = mod.read_exr(paths["contrib"], channels=("R","G","B","A"))
    bin_img = mod.read_exr(paths["bin"], channels=("R","G","B","A"))
    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)
    if diag.shape[:2] != pt_gi.shape[:2]:
        diag = mod.downsample_2x2_center(diag)
    if contrib.shape[:2] != pt_gi.shape[:2]:
        contrib = mod.downsample_2x2_center(contrib)
    if bin_img.shape[:2] != pt_gi.shape[:2]:
        bin_img = mod.downsample_2x2_center(bin_img)
    return dict(cascade=cascade, pt_gi=pt_gi, pt_full=pt_full, pt_direct=pt_direct,
                gbuffer=gbuffer, diag=diag, contrib=contrib, bin_img=bin_img)


def classify_albedo(rgb: np.ndarray) -> int:
    """0=unknown/black, 1=red wall, 2=green wall, 3=white wall/floor/ceiling, 4=other."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    is_red   = (r > 0.3) & (g < 0.3) & (b < 0.3)
    is_green = (g > 0.3) & (r < 0.3) & (b < 0.3)
    is_white = (r > 0.4) & (g > 0.4) & (b > 0.4) & (abs(r - g) < 0.2) & (abs(g - b) < 0.2)
    out = np.zeros(rgb.shape[:2], dtype=np.int32)
    out[is_white] = 3
    out[is_red]   = 1
    out[is_green] = 2
    out[(out == 0) & ((r + g + b) > 0.1)] = 4
    return out


def per_pixel_ratio_stats(d: dict) -> dict:
    mod = MOD
    casc_l = mod.lum(d["cascade"])
    pt_l = mod.lum(d["pt_gi"])
    ratio = casc_l / np.maximum(pt_l, EPS)
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (d["gbuffer"][..., 3] > 0.0)
    if not np.any(valid):
        return {"status": "empty_valid"}
    r = ratio[valid]
    hist_edges = np.linspace(0.0, 3.0, 31)  # 30 bins, 0..3
    hist, _ = np.histogram(r, bins=hist_edges)

    # Per-region split.
    albedo_class = classify_albedo(d["gbuffer"][..., :3])
    region_stats = {}
    for cls, label in [(1, "red"), (2, "green"), (3, "white"), (4, "other")]:
        m = valid & (albedo_class == cls)
        if np.sum(m) > 50:
            rr = ratio[m]
            region_stats[label] = {
                "count": int(np.sum(m)),
                "ratio_mean":   float(np.mean(rr)),
                "ratio_median": float(np.median(rr)),
                "ratio_p25":    float(np.percentile(rr, 25)),
                "ratio_p75":    float(np.percentile(rr, 75)),
            }

    return {
        "status": "ok",
        "valid": int(np.sum(valid)),
        "ratio_mean":   float(np.mean(r)),
        "ratio_median": float(np.median(r)),
        "ratio_std":    float(np.std(r)),
        "ratio_p10":    float(np.percentile(r, 10)),
        "ratio_p90":    float(np.percentile(r, 90)),
        "histogram_edges": hist_edges.tolist(),
        "histogram_counts": hist.tolist(),
        "per_region": region_stats,
        # Bimodality estimate: ratio of histogram bin between 0.4-0.6 to between 0.9-1.1
        "bimodality_05_vs_10": (
            float(hist[int(0.4 / 3.0 * 30):int(0.6 / 3.0 * 30) + 1].sum() /
                  max(hist[int(0.9 / 3.0 * 30):int(1.1 / 3.0 * 30) + 1].sum(), 1))
        ),
    }


def per_cell_ratio_stats(d: dict, label: str) -> dict:
    """SC1: per-cell ratio binned by probe_diag p000 (subject to the Stage 8/9 diag-rgb leak;
    treated as advisory). Aggregate cascade_gi luma and pt_gi luma per cell, then ratio."""
    mod = MOD
    casc_l = mod.lum(d["cascade"])
    pt_l = mod.lum(d["pt_gi"])
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (d["gbuffer"][..., 3] > 0.0) & (d["diag"][..., 3] > 0.0)
    if not np.any(valid):
        return {"status": "empty_valid"}
    p000 = np.floor(np.clip(d["diag"][..., :3] * ATLAS_RES, 0.0, ATLAS_RES - 1.0)).astype(np.int32)

    # Aggregate per cell.
    accum_casc = defaultdict(list)
    accum_pt = defaultdict(list)
    ys, xs = np.nonzero(valid)
    for y, x in zip(ys.tolist(), xs.tolist()):
        key = (int(p000[y, x, 0]), int(p000[y, x, 1]), int(p000[y, x, 2]))
        accum_casc[key].append(float(casc_l[y, x]))
        accum_pt[key].append(float(pt_l[y, x]))

    per_cell = []
    for key in accum_casc.keys():
        c_mean = float(np.mean(accum_casc[key]))
        p_mean = float(np.mean(accum_pt[key]))
        if p_mean > EPS:
            per_cell.append({
                "cell": str(key), "count": len(accum_casc[key]),
                "casc_luma": c_mean, "pt_luma": p_mean,
                "ratio": c_mean / p_mean,
            })

    if not per_cell:
        return {"status": "no_cells"}

    ratios = np.array([r["ratio"] for r in per_cell])
    counts = np.array([r["count"] for r in per_cell])
    weighted_ratio = float(np.sum(ratios * counts) / np.sum(counts))

    return {
        "status": "ok",
        "label": label,
        "n_cells": len(per_cell),
        "cell_ratio_mean": float(np.mean(ratios)),
        "cell_ratio_median": float(np.median(ratios)),
        "cell_ratio_weighted_by_pixels": weighted_ratio,
        "cell_ratio_p10": float(np.percentile(ratios, 10)),
        "cell_ratio_p90": float(np.percentile(ratios, 90)),
        "cell_ratio_std": float(np.std(ratios)),
    }


def top_bin_histogram(d: dict) -> dict:
    """For Cornell under-bright pixels, histogram of top-bin index (D=8 octahedral).
    Plan §3 — direction-by-direction check."""
    mod = MOD
    casc_l = mod.lum(d["cascade"])
    pt_l = mod.lum(d["pt_gi"])
    ratio = casc_l / np.maximum(pt_l, EPS)
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (d["gbuffer"][..., 3] > 0.0)
    under = valid & (ratio < 0.7)  # under-bright pixels
    if not np.any(under):
        return {"status": "no_under_bright"}
    D = 8
    top_bin = np.floor(np.clip(d["bin_img"][..., :2] * D, 0.0, D - 1.0)).astype(np.int32)
    hist = np.zeros((D, D), dtype=np.int64)
    ys, xs = np.nonzero(under)
    for y, x in zip(ys.tolist(), xs.tolist()):
        hist[top_bin[y, x, 1], top_bin[y, x, 0]] += 1
    total = int(np.sum(hist))
    flat = hist.flatten()
    flat_idx_sorted = np.argsort(-flat)
    top5 = []
    for i in flat_idx_sorted[:5]:
        if flat[i] > 0:
            top5.append({"bin_xy": [int(i % D), int(i // D)], "count": int(flat[i]),
                         "fraction": float(flat[i] / total)})
    return {"status": "ok", "n_under_bright_pixels": total,
            "top5_bins": top5, "histogram_2d": hist.tolist()}


MOD = load_local_module()


def derive_verdict(cornell_per_cell_ratio: float, cornell_per_pixel_ratio: float,
                   cornell_per_region: dict, cornell_bimodality: float) -> dict:
    """Plan §5 decision rule. Returns dict with verdict + reasoning."""
    notes = []

    # Region asymmetry: max region ratio - min region ratio
    region_ratios = [v["ratio_median"] for v in cornell_per_region.values()]
    region_spread = float(max(region_ratios) - min(region_ratios)) if region_ratios else 0.0
    notes.append(f"region_spread={region_spread:.3f}")
    notes.append(f"bimodality_05_vs_10={cornell_bimodality:.2f}")

    # Per-cell vs per-pixel.
    per_cell_close_to_1 = abs(cornell_per_cell_ratio - 1.0) < 0.10
    per_pixel_close_to_05 = abs(cornell_per_pixel_ratio - 0.5) < 0.10

    if per_cell_close_to_1 and per_pixel_close_to_05:
        verdict = "CONSUMER_UNDER_INTEGRATES"
        notes.append("per-cell ratio ~1.0 AND per-pixel ratio ~0.5 → consumer drops energy on per-pixel integration")
    elif abs(cornell_per_cell_ratio - 0.5) < 0.10:
        verdict = "BAKE_UNDER_EMITS"
        notes.append("per-cell ratio ~0.5 → bake doesn't accumulate enough at the probe level")
    elif region_spread > 0.30 or cornell_bimodality > 3.0:
        verdict = "MIXED"
        notes.append("strong region asymmetry or bimodal distribution → multiple causes")
    else:
        verdict = "INCONCLUSIVE"
        notes.append("per-cell and per-pixel don't fit a clean pattern; need additional capture or shader instrumentation")

    return {"verdict": verdict, "notes": notes,
            "cornell_per_cell_ratio": cornell_per_cell_ratio,
            "cornell_per_pixel_ratio_mean": cornell_per_pixel_ratio,
            "region_ratio_spread": region_spread}


def main() -> int:
    output = {"cornell": {}, "sponza": {}}

    cornell_per_cell_ratios = []
    cornell_per_pixel_means = []
    cornell_region_aggregate = None
    cornell_bimodality_avg = 0.0
    cornell_evaluated = 0

    for cam, cap in CORNELL_CAPTURES.items():
        d = load_capture(MOD, cap)
        if d is None:
            output["cornell"][cam] = {"status": "missing"}
            continue
        pix = per_pixel_ratio_stats(d)
        cell = per_cell_ratio_stats(d, f"cornell_{cam}")
        bins = top_bin_histogram(d)
        output["cornell"][cam] = {"per_pixel": pix, "per_cell": cell, "top_bin_under_bright": bins}
        if pix.get("status") == "ok":
            cornell_per_pixel_means.append(pix["ratio_mean"])
            cornell_bimodality_avg += pix["bimodality_05_vs_10"]
            cornell_evaluated += 1
            if cornell_region_aggregate is None:
                cornell_region_aggregate = pix["per_region"]
        if cell.get("status") == "ok":
            cornell_per_cell_ratios.append(cell["cell_ratio_weighted_by_pixels"])

    sponza = load_capture(MOD, SPONZA_REF)
    if sponza is not None:
        output["sponza"]["g010"] = {
            "per_pixel": per_pixel_ratio_stats(sponza),
            "per_cell": per_cell_ratio_stats(sponza, "sponza_g010"),
        }

    if cornell_evaluated:
        cornell_per_cell_avg = float(np.mean(cornell_per_cell_ratios)) if cornell_per_cell_ratios else 0.0
        cornell_per_pixel_avg = float(np.mean(cornell_per_pixel_means))
        bimod = cornell_bimodality_avg / cornell_evaluated
        output["verdict"] = derive_verdict(cornell_per_cell_avg, cornell_per_pixel_avg,
                                           cornell_region_aggregate or {}, bimod)
    else:
        output["verdict"] = {"verdict": "MISSING_DATA"}

    out = ROOT / "cornell_audit_results.json"
    out.write_text(json.dumps(output, indent=2), encoding="utf-8")

    summary = {
        "verdict": output["verdict"].get("verdict"),
        "cornell_per_pixel_mean_by_cam": {
            cam: round(output["cornell"][cam].get("per_pixel", {}).get("ratio_mean", -1), 4)
            for cam in CORNELL_CAPTURES.keys() if output["cornell"].get(cam, {}).get("per_pixel", {}).get("status") == "ok"
        },
        "cornell_per_pixel_median_by_cam": {
            cam: round(output["cornell"][cam].get("per_pixel", {}).get("ratio_median", -1), 4)
            for cam in CORNELL_CAPTURES.keys() if output["cornell"].get(cam, {}).get("per_pixel", {}).get("status") == "ok"
        },
        "cornell_per_cell_weighted_by_cam": {
            cam: round(output["cornell"][cam].get("per_cell", {}).get("cell_ratio_weighted_by_pixels", -1), 4)
            for cam in CORNELL_CAPTURES.keys() if output["cornell"].get(cam, {}).get("per_cell", {}).get("status") == "ok"
        },
        "cornell_cam0_per_region_median": {
            label: round(v["ratio_median"], 4) for label, v in (cornell_region_aggregate or {}).items()
        } if cornell_region_aggregate else {},
        "sponza_g010_per_pixel_mean": (
            round(output["sponza"].get("g010", {}).get("per_pixel", {}).get("ratio_mean", -1), 4)
            if output["sponza"].get("g010", {}).get("per_pixel", {}).get("status") == "ok" else None
        ),
        "out": str(out),
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
