"""M1 Stage 8 — Source-energy A/B analyzer.

Reuses Stage 3 EXR helpers and Stage 7 valid/bad mask logic. Reports per-variant
metrics at the baseline Sponza dominant shader cells (7,5,4), (6,5,4), (6,4,4)
plus screen-level ratio_self / bad_pct / |p95|. See
doc/7/v3_m1_stage8_source_energy_ab_plan.md for the verdict bands.
"""

import importlib.util
import json
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_source_energy_ab")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
N = 2048
ATLAS_RES = 32
DIR_RES = 8
EPS = 1e-6

# Stage 7 dominant Sponza shader-side cells (probe_diag p000 keys).
FIXED_CELLS = [(7, 5, 4), (6, 5, 4), (6, 4, 4)]

# (scene, variant) pairs to analyze. Sponza baseline reuses Stage 7 capture.
SPONZA_VARIANTS = ["baseline", "mb_off", "mb_gain_half", "jitter_off", "delta3_on", "hybrid_on"]
CORNELL_SANITY_VARIANTS = ["baseline", "mb_off", "delta3_on"]


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def pct(values, q):
    return float(np.percentile(values, q)) if values.size else 0.0


def capture_paths(scene: str, variant: str) -> dict[str, Path]:
    if scene == "sponza" and variant == "baseline":
        # Reuse Stage 7 capture rather than recapturing the baseline.
        directory = Path("tools/v3_m1_shader_contrib/captures_sponza")
        stem = f"m1shadercontrib_{scene}_baseline_N{N:04d}_m17"
    else:
        directory = ROOT / f"captures_{scene}_{variant}"
        stem = f"m1stage8_{scene}_{variant}_N{N:04d}_m17"
    return {
        "cascade": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
        "gbuffer": directory / f"{stem}_gbuffer.exr",
        "diag": directory / f"{stem}_probe_diag.exr",
        "contrib": directory / f"{stem}_probe_contrib.exr",
        "bin": directory / f"{stem}_probe_bin.exr",
    }


def analyze_one(mod, scene: str, variant: str) -> dict:
    paths = capture_paths(scene, variant)
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    cascade = mod.read_exr(paths["cascade"])
    pt_full = mod.read_exr(paths["pt_full"])
    pt_direct = mod.read_exr(paths["pt_direct"])
    gbuffer = mod.read_exr(paths["gbuffer"], channels=("R", "G", "B", "A"))
    diag = mod.read_exr(paths["diag"], channels=("R", "G", "B", "A"))

    pt_gi = np.maximum(pt_full - pt_direct, 0.0)
    if cascade.shape[:2] != pt_gi.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_gi.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)
    if diag.shape[:2] != pt_gi.shape[:2]:
        diag = mod.downsample_2x2_center(diag)

    casc_l = mod.lum(cascade)
    pt_l = mod.lum(pt_gi)
    ratio = casc_l / np.maximum(pt_l, EPS)
    valid = (pt_l > 0.05) & (casc_l > 0.001) & (gbuffer[..., 3] > 0.0) & (diag[..., 3] > 0.0)

    if not np.any(valid):
        return {"status": "empty_valid"}

    # Screen-level metrics
    ratio_valid = ratio[valid]
    bad_pct = float(100.0 * np.mean(ratio_valid > 1.3))
    ratio_self = float(np.mean(ratio_valid))
    # |p95| of ratio-1 = scope §7 retirement criterion at sign-off N.
    p95_abs = float(np.percentile(np.abs(ratio_valid - 1.0), 95))

    # Map every valid pixel to a p000 shader cell.
    p000 = np.floor(np.clip(diag[..., :3] * ATLAS_RES, 0.0, ATLAS_RES - 1.0)).astype(np.int32)

    # Fixed-cell view at the Stage 7 dominant cells.
    cell_rows = []
    for cx, cy, cz in FIXED_CELLS:
        mask = valid & (p000[..., 0] == cx) & (p000[..., 1] == cy) & (p000[..., 2] == cz)
        n = int(np.sum(mask))
        if n == 0:
            cell_rows.append({
                "cell": f"({cx},{cy},{cz})",
                "count": 0,
                "cascade_luma_mean": 0.0,
                "pt_luma_mean": 0.0,
                "ratio_mean": 0.0,
            })
            continue
        cell_rows.append({
            "cell": f"({cx},{cy},{cz})",
            "count": n,
            "cascade_luma_mean": float(np.mean(casc_l[mask])),
            "pt_luma_mean": float(np.mean(pt_l[mask])),
            "ratio_mean": float(np.mean(ratio[mask])),
        })

    # SC1: also report this variant's own top-3 dominant cells over the bad mask.
    bad_mask = valid & (ratio > 1.3)
    own_top_cells = []
    if np.any(bad_mask):
        counter: Counter = Counter()
        ys, xs = np.nonzero(bad_mask)
        for y, x in zip(ys.tolist(), xs.tolist()):
            counter[tuple(int(v) for v in p000[y, x])] += 1
        for key, count in counter.most_common(3):
            mask = valid & (p000[..., 0] == key[0]) & (p000[..., 1] == key[1]) & (p000[..., 2] == key[2])
            own_top_cells.append({
                "cell": f"({key[0]},{key[1]},{key[2]})",
                "count": int(count),
                "cascade_luma_mean": float(np.mean(casc_l[mask])),
                "pt_luma_mean": float(np.mean(pt_l[mask])),
                "ratio_mean": float(np.mean(ratio[mask])),
            })

    return {
        "status": "ok",
        "screen": {
            "valid": int(np.sum(valid)),
            "ratio_self": ratio_self,
            "bad_pct": bad_pct,
            "p95_abs_ratio_minus_1": p95_abs,
        },
        "fixed_cells": cell_rows,
        "own_top_cells": own_top_cells,
    }


def _band(score: float) -> str:
    """Map ratio-improvement score to verdict band (SC2 — primary discriminator)."""
    if score >= 0.40:
        return "STRONG_COLLAPSE"
    if score >= 0.15:
        return "PARTIAL_COLLAPSE"
    if score > -0.15:
        return "NEUTRAL"
    return "WORSE"


def verdict_for_variant(baseline: dict, variant: dict) -> dict:
    """Per SC2: primary discriminator is ratio drop toward 1.0, not raw cascade
    luma. Fixed-cell is preferred; if the diag p000 shifted off the baseline
    cells (mb_off, delta3_on broke cascade), fall back to screen-level ratio.
    """
    if baseline.get("status") != "ok":
        return {"verdict": "BASELINE_MISSING"}
    if variant.get("status") != "ok":
        return {"verdict": "VARIANT_BROKEN", "variant_status": variant.get("status")}

    bcells = {c["cell"]: c for c in baseline["fixed_cells"]}
    vcells = {c["cell"]: c for c in variant["fixed_cells"]}
    luma_drops = []
    ratio_moves_toward_one = []
    fixed_cell_pairs = 0
    for key in [f"({a},{b},{c})" for a, b, c in FIXED_CELLS]:
        b = bcells.get(key)
        v = vcells.get(key)
        if not b or not v or b["count"] == 0 or v["count"] == 0:
            continue
        fixed_cell_pairs += 1
        luma_drops.append(1.0 - (v["cascade_luma_mean"] / max(b["cascade_luma_mean"], EPS)))
        b_excess = abs(b["ratio_mean"] - 1.0)
        v_excess = abs(v["ratio_mean"] - 1.0)
        ratio_moves_toward_one.append((b_excess - v_excess) / max(b_excess, EPS))

    # Screen-level fallback (always computed for cross-check).
    b_excess_s = abs(baseline["screen"]["ratio_self"] - 1.0)
    v_excess_s = abs(variant["screen"]["ratio_self"] - 1.0)
    screen_ratio_improvement = (b_excess_s - v_excess_s) / max(b_excess_s, EPS)
    screen_bad_delta = variant["screen"]["bad_pct"] - baseline["screen"]["bad_pct"]
    screen_p95_delta = variant["screen"]["p95_abs_ratio_minus_1"] - baseline["screen"]["p95_abs_ratio_minus_1"]

    result = {
        "fixed_cell_pairs_used": fixed_cell_pairs,
        "screen_ratio_improvement": float(screen_ratio_improvement),
        "screen_bad_pct_delta": float(screen_bad_delta),
        "screen_p95_delta": float(screen_p95_delta),
    }

    if luma_drops:
        result["mean_luma_drop"] = float(np.mean(luma_drops))
        result["mean_ratio_improvement_fixed_cell"] = float(np.mean(ratio_moves_toward_one))
        result["verdict"] = _band(float(np.mean(ratio_moves_toward_one)))
        result["verdict_source"] = "fixed_cell"
    else:
        result["verdict"] = _band(float(screen_ratio_improvement))
        result["verdict_source"] = "screen_fallback"
        result["note"] = "fixed-cell p000 mapping shifted off baseline cells; screen-level used"

    # SC2 tie-break / sanity check: if cascade collapses to ~0 (broken), flag.
    if variant["screen"]["ratio_self"] < 0.1 and variant["screen"]["valid"] < 0.5 * baseline["screen"]["valid"]:
        result["verdict"] = "CASCADE_BROKEN"
        result["note"] = "cascade essentially zero — variant breaks the pipeline, not a fix"

    return result


def analyze_scene_variants(mod, scene: str, variants: list[str]) -> dict:
    results = {v: analyze_one(mod, scene, v) for v in variants}
    if results.get("baseline", {}).get("status") != "ok":
        return {"variants": results, "verdicts": {}, "note": "baseline missing — no verdicts computed"}
    verdicts = {
        v: verdict_for_variant(results["baseline"], results[v])
        for v in variants if v != "baseline"
    }
    return {"variants": results, "verdicts": verdicts}


def main() -> int:
    mod = load_local_module()
    output = {
        "sponza": analyze_scene_variants(mod, "sponza", SPONZA_VARIANTS),
        "cornell_sanity": analyze_scene_variants(mod, "cornell", CORNELL_SANITY_VARIANTS),
        "fixed_cells": [f"({a},{b},{c})" for a, b, c in FIXED_CELLS],
    }
    out = ROOT / "source_energy_results.json"
    out.write_text(json.dumps(output, indent=2), encoding="utf-8")

    # Print a concise summary
    summary = {"out": str(out), "sponza_verdicts": {}, "sponza_screen": {}}
    for v, verd in output["sponza"]["verdicts"].items():
        summary["sponza_verdicts"][v] = verd.get("verdict", "?")
    for v, res in output["sponza"]["variants"].items():
        if res.get("status") == "ok":
            summary["sponza_screen"][v] = {
                "ratio_self": round(res["screen"]["ratio_self"], 4),
                "bad_pct": round(res["screen"]["bad_pct"], 2),
                "p95_abs": round(res["screen"]["p95_abs_ratio_minus_1"], 4),
            }
        else:
            summary["sponza_screen"][v] = res.get("status", "?")
    if "cornell_sanity" in output:
        summary["cornell_verdicts"] = {
            v: verd.get("verdict", "?") for v, verd in output["cornell_sanity"]["verdicts"].items()
        }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
