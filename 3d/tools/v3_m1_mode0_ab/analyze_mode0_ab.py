"""M1 Stage 10 — Mode-0 final-composite A/B analyzer.

Synthesizes cascade_full = pt_direct + cascade_gi (per plan §2), compares
against pt_full ground truth, reports per-variant {composite_rms,
composite_mae, mean_rel_err, direct_share, excess_rms_over_hybrid} and
emits side-by-side PNGs to tools/v3_m1_mode0_ab/visuals/.
"""

import importlib.util
import json
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_mode0_ab")
VISUALS = ROOT / "visuals"
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
N = 2048
EPS = 1e-6
RMS_TOLERANCE_FRAC = 0.05  # "competitive with hybrid" if excess RMS ≤ 5% of hybrid RMS

# Variants and where to read their EXRs from. Stage 8/9 captures reused; stage10
# new captures for cornell_g010 and cornell_hybrid.
VARIANTS = {
    "sponza_g010":    {"dir": "tools/v3_m1_mb_gain_ladder/captures_sponza_g010",   "stem": "m1stage9_sponza_g010_N2048_m17",  "scene": "sponza",  "gain": 0.10},
    "sponza_g050":    {"dir": "tools/v3_m1_source_energy_ab/captures_sponza_mb_gain_half", "stem": "m1stage8_sponza_mb_gain_half_N2048_m17", "scene": "sponza", "gain": 0.50},
    "sponza_g100":    {"dir": "tools/v3_m1_shader_contrib/captures_sponza",        "stem": "m1shadercontrib_sponza_baseline_N2048_m17", "scene": "sponza", "gain": 1.00},
    "sponza_hybrid":  {"dir": "tools/v3_m1_source_energy_ab/captures_sponza_hybrid_on", "stem": "m1stage8_sponza_hybrid_on_N2048_m17", "scene": "sponza", "gain": 1.00, "hybrid": True},
    "cornell_g010":   {"dir": "tools/v3_m1_mode0_ab/captures_cornell_g010",        "stem": "m1stage10_cornell_g010_N2048_m17", "scene": "cornell", "gain": 0.10},
    "cornell_g050":   {"dir": "tools/v3_m1_mb_gain_ladder/captures_cornell_g050",  "stem": "m1stage9_cornell_g050_N2048_m17", "scene": "cornell", "gain": 0.50},
    "cornell_g100":   {"dir": "tools/v3_m1_source_energy_ab/captures_cornell_baseline", "stem": "m1stage8_cornell_baseline_N2048_m17", "scene": "cornell", "gain": 1.00},
    "cornell_hybrid": {"dir": "tools/v3_m1_mode0_ab/captures_cornell_hybrid",      "stem": "m1stage10_cornell_hybrid_N2048_m17", "scene": "cornell", "gain": 1.00, "hybrid": True},
}


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec); assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def variant_paths(v: dict) -> dict[str, Path]:
    d = Path(v["dir"]); stem = v["stem"]
    return {
        "cascade":   d / f"{stem}_cascade_gi.exr",
        "pt_full":   d / f"{stem}_pt_full.exr",
        "pt_direct": d / f"{stem}_pt_direct.exr",
        "gbuffer":   d / f"{stem}_gbuffer.exr",
        "png":       d / f"{stem}.png",
    }


def srgb_encode(linear: np.ndarray) -> np.ndarray:
    """sRGB-like gamma for visual display (2.2 simplified)."""
    return np.clip(linear, 0.0, 1.0) ** (1.0 / 2.2)


def save_png(rgb_linear: np.ndarray, out: Path) -> None:
    """Write 8-bit gamma-encoded PNG using PIL."""
    from PIL import Image
    arr = (np.clip(srgb_encode(rgb_linear), 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(arr).save(str(out))


def analyze_variant(mod, key: str, v: dict) -> dict:
    p = variant_paths(v)
    missing = [str(x) for x in p.values() if not x.exists() and x.suffix == ".exr"]
    if missing:
        return {"status": "missing", "missing": missing[:3]}

    cascade   = mod.read_exr(p["cascade"])           # GI-only, shape (h_c, w_c, 3)
    pt_full   = mod.read_exr(p["pt_full"])           # full composite, shape (h_p, w_p, 3)
    pt_direct = mod.read_exr(p["pt_direct"])         # direct only
    gbuffer   = mod.read_exr(p["gbuffer"], channels=("R","G","B","A"))

    # All sidecars are at different resolutions; PT is half-res. Downsample
    # cascade+gbuffer to PT-res so per-pixel comparison is well-defined.
    if cascade.shape[:2] != pt_full.shape[:2]:
        cascade = mod.downsample_2x2_mean(cascade)
    if gbuffer.shape[:2] != pt_full.shape[:2]:
        gbuffer = mod.downsample_2x2_center(gbuffer)

    cascade_full = pt_direct + cascade               # plan §2 synthesis
    valid_mask   = (mod.lum(pt_full) > 0.05) & (gbuffer[..., 3] > 0.0)
    if not np.any(valid_mask):
        return {"status": "empty_valid"}

    diff = cascade_full - pt_full
    diff_luma = mod.lum(diff[valid_mask])
    pt_luma_full = mod.lum(pt_full)
    pt_luma_dir  = mod.lum(pt_direct)

    composite_rms = float(np.sqrt(np.mean(diff_luma ** 2)))
    composite_mae = float(np.mean(np.abs(diff_luma)))
    rel_err = np.abs(diff_luma) / np.maximum(pt_luma_full[valid_mask], EPS)
    composite_mean_relative_error = float(np.mean(rel_err))

    # SC4: clamped variant (cap at 99th percentile so highlights don't dominate).
    p99 = float(np.percentile(pt_luma_full[valid_mask], 99))
    if p99 > EPS:
        c_clamp = np.clip(cascade_full, 0.0, p99)
        p_clamp = np.clip(pt_full,      0.0, p99)
        cd = (c_clamp - p_clamp)
        composite_rms_clamped = float(np.sqrt(np.mean(mod.lum(cd[valid_mask]) ** 2)))
    else:
        composite_rms_clamped = composite_rms

    direct_share = float(np.mean(pt_luma_dir[valid_mask]) / max(np.mean(pt_luma_full[valid_mask]), EPS))

    # SC1: synthesis sanity — compare synthesized composite mean luma vs mode-17 PNG-derived
    # luma. PNG is tone-mapped, so this is a coarse check; just flag large deviation.
    sanity_ratio = None
    try:
        from PIL import Image as _Img
        if p["png"].exists():
            png = np.asarray(_Img.open(str(p["png"])).convert("RGB"), dtype=np.float32) / 255.0
            # Inverse sRGB to approximate linear, then luma.
            png_linear = np.clip(png, 1e-6, 1.0) ** 2.2
            if png_linear.shape[:2] != cascade_full.shape[:2]:
                png_linear = png_linear[::2, ::2]  # rough match to PT-res
            png_lum = 0.2126*png_linear[...,0] + 0.7152*png_linear[...,1] + 0.0722*png_linear[...,2]
            sanity_ratio = float(np.mean(mod.lum(cascade_full)[valid_mask]) /
                                 max(np.mean(png_lum[valid_mask[:png_lum.shape[0], :png_lum.shape[1]]]), EPS))
    except Exception:
        sanity_ratio = None

    # Save side-by-side visual.
    out_png = VISUALS / f"{key}_synth_vs_pt.png"
    side = np.concatenate([cascade_full, pt_full], axis=1)
    save_png(side, out_png)

    return {
        "status": "ok",
        "valid": int(np.sum(valid_mask)),
        "composite_rms": composite_rms,
        "composite_rms_clamped": composite_rms_clamped,
        "composite_mae": composite_mae,
        "composite_mean_relative_error": composite_mean_relative_error,
        "direct_share": direct_share,
        "synth_sanity_ratio_vs_png": sanity_ratio,
        "side_by_side_png": str(out_png),
    }


def verdict_for_scene(scene_variants: dict[str, dict], scene: str) -> dict:
    # scene_variants is the subset of VARIANTS for one scene already measured.
    if not all(v.get("status") == "ok" for v in scene_variants.values()):
        return {"verdict": "MISSING_DATA"}
    hybrid_key = f"{scene}_hybrid"
    if hybrid_key not in scene_variants:
        return {"verdict": "NO_HYBRID_REFERENCE"}

    h_rms = scene_variants[hybrid_key]["composite_rms"]
    gain_results = {k: v for k, v in scene_variants.items() if not k.endswith("_hybrid")}

    excess = {k: v["composite_rms"] - h_rms for k, v in gain_results.items()}
    competitive = [k for k, e in excess.items() if e <= RMS_TOLERANCE_FRAC * h_rms]

    best_gain_key = min(gain_results.keys(), key=lambda k: gain_results[k]["composite_rms"])

    mode17_best_gain = {"sponza": "sponza_g010", "cornell": "cornell_g100"}.get(scene)

    if mode17_best_gain == best_gain_key:
        verdict = "STAGE8_9_VINDICATED"
    elif len(competitive) == len(gain_results):
        verdict = "FLAT_GAIN_ROOM"
    else:
        verdict = "MODE17_ARTIFACT"

    return {
        "verdict": verdict,
        "hybrid_rms": h_rms,
        "excess_rms_over_hybrid": excess,
        "competitive_with_hybrid": competitive,
        "mode0_best_gain_key": best_gain_key,
        "mode0_best_rms": gain_results[best_gain_key]["composite_rms"],
        "mode17_winning_gain_key": mode17_best_gain,
    }


def main() -> int:
    mod = load_local_module()
    VISUALS.mkdir(parents=True, exist_ok=True)

    measurements = {}
    for key, v in VARIANTS.items():
        measurements[key] = analyze_variant(mod, key, v)

    by_scene = {"sponza": {}, "cornell": {}}
    for key, v in VARIANTS.items():
        by_scene[v["scene"]][key] = measurements[key]
    verdicts = {scene: verdict_for_scene(svs, scene) for scene, svs in by_scene.items()}

    output = {"variants": measurements, "verdicts": verdicts, "tolerance_frac": RMS_TOLERANCE_FRAC}
    out = ROOT / "mode0_ab_results.json"
    out.write_text(json.dumps(output, indent=2), encoding="utf-8")

    summary = {"out": str(out), "rms_by_variant": {}, "verdicts": {}, "direct_share": {}}
    for k, m in measurements.items():
        if m.get("status") == "ok":
            summary["rms_by_variant"][k] = round(m["composite_rms"], 4)
            summary["direct_share"][k] = round(m["direct_share"], 4)
        else:
            summary["rms_by_variant"][k] = m.get("status", "?")
    for s, v in verdicts.items():
        summary["verdicts"][s] = {
            "verdict": v.get("verdict"),
            "mode0_best": v.get("mode0_best_gain_key"),
            "mode17_best": v.get("mode17_winning_gain_key"),
            "hybrid_rms": round(v.get("hybrid_rms", -1), 4) if v.get("hybrid_rms") is not None else None,
        }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
