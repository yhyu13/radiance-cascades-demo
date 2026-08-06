import importlib.util
import json
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_shader_contrib")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
SCENE = "sponza"
N = 2048
ATLAS_RES = 32
DIR_RES = 8
EPS = 1e-6


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def pct(values, q):
    return float(np.percentile(values, q)) if values.size else 0.0


def top_rows(counter, limit=12):
    return [{"key": str(k), "count": int(v)} for k, v in counter.most_common(limit)]


def analyze_scene() -> dict:
    mod = load_local_module()
    directory = ROOT / f"captures_{SCENE}"
    stem = f"m1shadercontrib_{SCENE}_baseline_N{N:04d}_m17"
    paths = {
        "cascade": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
        "gbuffer": directory / f"{stem}_gbuffer.exr",
        "diag": directory / f"{stem}_probe_diag.exr",
        "contrib": directory / f"{stem}_probe_contrib.exr",
        "bin": directory / f"{stem}_probe_bin.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    cascade = mod.read_exr(paths["cascade"])
    pt_full = mod.read_exr(paths["pt_full"])
    pt_direct = mod.read_exr(paths["pt_direct"])
    gbuffer = mod.read_exr(paths["gbuffer"], channels=("R", "G", "B", "A"))
    diag = mod.read_exr(paths["diag"], channels=("R", "G", "B", "A"))
    contrib = mod.read_exr(paths["contrib"], channels=("R", "G", "B", "A"))
    bin_img = mod.read_exr(paths["bin"], channels=("R", "G", "B", "A"))

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

    casc_l = mod.lum(cascade)
    pt_l = mod.lum(pt_gi)
    ratio = casc_l / np.maximum(pt_l, EPS)
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (gbuffer[..., 3] > 0.0) & (diag[..., 3] > 0.0)
    bad = valid & (ratio > 1.3)
    if not np.any(bad):
        return {"status": "empty_bad_mask", "valid": int(np.sum(valid))}

    p000 = np.floor(np.clip(diag[..., :3] * ATLAS_RES, 0.0, ATLAS_RES - 1.0)).astype(np.int32)
    top_probe = np.floor(np.clip(contrib[..., :3] * ATLAS_RES, 0.0, ATLAS_RES - 1.0)).astype(np.int32)
    top_bin = np.floor(np.clip(bin_img[..., :2] * DIR_RES, 0.0, DIR_RES - 1.0)).astype(np.int32)
    probe_share = np.clip(contrib[..., 3], 0.0, 1.0)
    bin_share = np.clip(bin_img[..., 2], 0.0, 1.0)
    contrib_luma = bin_img[..., 3]
    luma_ratio = contrib_luma / np.maximum(diag[..., 3], EPS)

    cell_groups = defaultdict(lambda: {
        "count": 0, "ratio": [], "probe_share": [], "bin_share": [], "luma_ratio": [],
        "top_probe": Counter(), "top_bin": Counter()
    })
    ys, xs = np.nonzero(bad)
    for y, x in zip(ys.tolist(), xs.tolist()):
        key = tuple(int(v) for v in p000[y, x])
        g = cell_groups[key]
        g["count"] += 1
        g["ratio"].append(float(ratio[y, x]))
        g["probe_share"].append(float(probe_share[y, x]))
        g["bin_share"].append(float(bin_share[y, x]))
        g["luma_ratio"].append(float(luma_ratio[y, x]))
        g["top_probe"][tuple(int(v) for v in top_probe[y, x])] += 1
        g["top_bin"][tuple(int(v) for v in top_bin[y, x])] += 1

    rows = []
    for key, g in cell_groups.items():
        ps = np.asarray(g["probe_share"], dtype=np.float32)
        bs = np.asarray(g["bin_share"], dtype=np.float32)
        lr = np.asarray(g["luma_ratio"], dtype=np.float32)
        rr = np.asarray(g["ratio"], dtype=np.float32)
        top_probe_count = g["top_probe"].most_common(1)[0][1]
        top_bin_count = g["top_bin"].most_common(1)[0][1]
        rows.append({
            "cell": str(key),
            "count": g["count"],
            "mean_ratio": float(np.mean(rr)),
            "luma_reconstruction_ratio_mean": float(np.mean(lr)),
            "luma_reconstruction_ratio_p05": pct(lr, 5),
            "luma_reconstruction_ratio_p95": pct(lr, 95),
            "probe_share_mean": float(np.mean(ps)),
            "probe_share_p95": pct(ps, 95),
            "bin_share_mean": float(np.mean(bs)),
            "bin_share_p95": pct(bs, 95),
            "top_probe_mode_share": float(top_probe_count / g["count"]),
            "top_bin_mode_share": float(top_bin_count / g["count"]),
            "top_probes": top_rows(g["top_probe"]),
            "top_bins": top_rows(g["top_bin"]),
        })
    rows.sort(key=lambda r: r["count"], reverse=True)

    lr_bad = luma_ratio[bad]
    reconstruction_ok = float(np.mean((lr_bad > 0.95) & (lr_bad < 1.05))) > 0.95
    if not reconstruction_ok:
        verdict = "SHADER_CONTRIB_DIAG_MISMATCH"
    else:
        dominant = rows[0]
        if dominant["top_bin_mode_share"] > 0.6 and dominant["bin_share_mean"] > 0.35:
            verdict = "HOT_BIN"
        elif dominant["top_probe_mode_share"] > 0.6 and dominant["probe_share_mean"] > 0.55:
            verdict = "HOT_PROBE"
        else:
            verdict = "BROAD_LOCAL_ENERGY"

    return {
        "status": "ok",
        "valid": int(np.sum(valid)),
        "bad": int(np.sum(bad)),
        "screen": {
            "ratio_self": float(np.mean(ratio[valid])),
            "bad_pct": float(100.0 * np.mean(ratio[valid] > 1.3)),
            "luma_reconstruction_ratio_mean_bad": float(np.mean(lr_bad)),
            "luma_reconstruction_ratio_p05_bad": pct(lr_bad, 5),
            "luma_reconstruction_ratio_p95_bad": pct(lr_bad, 95),
        },
        "cells": rows,
        "verdict": verdict,
    }


def main() -> int:
    scene = analyze_scene()
    results = {"scene": SCENE, "N": N, "result": scene, "verdict": scene.get("verdict", "NO_VERDICT")}
    out = ROOT / "shader_contrib_results.json"
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    summary = {
        "out": str(out),
        "verdict": results["verdict"],
        "screen": scene.get("screen"),
        "top_cells": scene.get("cells", [])[:5],
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
