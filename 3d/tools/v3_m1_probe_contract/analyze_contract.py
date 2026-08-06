import json
from pathlib import Path

import Imath
import numpy as np
import OpenEXR


SCENES = ["cornell", "sponza"]
N = 2048
ROOT = Path("tools/v3_m1_probe_contract")
EPS = 1e-6


def read_exr(path: Path) -> np.ndarray:
    f = OpenEXR.InputFile(str(path))
    dw = f.header()["dataWindow"]
    w = dw.max.x - dw.min.x + 1
    h = dw.max.y - dw.min.y + 1
    pt = Imath.PixelType(Imath.PixelType.FLOAT)
    r = np.frombuffer(f.channel("R", pt), dtype=np.float32).reshape(h, w)
    g = np.frombuffer(f.channel("G", pt), dtype=np.float32).reshape(h, w)
    b = np.frombuffer(f.channel("B", pt), dtype=np.float32).reshape(h, w)
    return np.stack([r, g, b], axis=-1)


def lum(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def downsample_2x2(img: np.ndarray) -> np.ndarray:
    h, w = img.shape[:2]
    h2, w2 = h // 2, w // 2
    return img[: h2 * 2, : w2 * 2].reshape(h2, 2, w2, 2, -1).mean(axis=(1, 3))


def analyze_screen(directory: Path, stem: str) -> dict:
    paths = {
        "cascade_gi": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
    }
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing}

    casc = read_exr(paths["cascade_gi"])
    pt_full = read_exr(paths["pt_full"])
    pt_direct = read_exr(paths["pt_direct"])
    pt_indirect = np.maximum(pt_full - pt_direct, 0.0)
    if casc.shape[:2] != pt_indirect.shape[:2]:
        casc = downsample_2x2(casc)

    casc_lum = lum(casc)
    pt_lum = lum(pt_indirect)
    mask = (pt_lum > 0.05) & (casc_lum > 0.001)
    if not np.any(mask):
        return {"status": "empty_mask", "valid": 0}

    ratio = casc_lum[mask] / np.maximum(pt_lum[mask], EPS)
    rel = ratio - 1.0
    return {
        "status": "ok",
        "valid": int(mask.sum()),
        "cascade_mean": float(np.mean(casc_lum[mask])),
        "pt_indirect_mean": float(np.mean(pt_lum[mask])),
        "ratio_self": float(np.mean(ratio)),
        "p05_rel": float(np.percentile(rel, 5)),
        "p50_rel": float(np.percentile(rel, 50)),
        "p95_rel": float(np.percentile(rel, 95)),
        "abs_p95": float(np.percentile(np.abs(rel), 95)),
        "dim_pct": float(100.0 * np.mean(ratio < 0.7)),
        "bright_pct": float(100.0 * np.mean(ratio > 1.3)),
    }


def analyze_probe_chain(stats: dict) -> dict:
    cascades = stats.get("cascades", [])
    means = [float(c.get("meanLum", 0.0)) for c in cascades]
    ratios = []
    for i in range(len(means) - 1):
        ratios.append(float(means[i] / max(means[i + 1], EPS)))

    coverage = []
    for c in cascades:
        coverage.append({
            "index": int(c.get("index", len(coverage))),
            "resolution": int(c.get("resolution", 0)),
            "dirRes": int(c.get("dirRes", 0)),
            "meanLum": float(c.get("meanLum", 0.0)),
            "maxLum": float(c.get("maxLum", 0.0)),
            "anyPct": float(c.get("anyPct", 0.0)),
            "surfPct": float(c.get("surfPct", 0.0)),
            "skyPct": float(c.get("skyPct", 0.0)),
        })

    if len(means) >= 2 and means[0] > max(means[1:]) * 1.5:
        direction = "c0_energy_spike_or_final_sampling"
    elif len(means) >= 2 and max(means[1:]) > means[0] * 1.5:
        direction = "upper_cascade_or_merge_energy"
    elif coverage and max(c["skyPct"] for c in coverage) > 90.0:
        direction = "sky_or_volume_boundary_contract"
    else:
        direction = "mixed_probe_contract"

    return {
        "mean_lum_chain": means,
        "adjacent_mean_ratios": ratios,
        "coverage": coverage,
        "direction_hint": direction,
    }


def main() -> int:
    results = {"N": N, "scenes": {}}
    for scene in SCENES:
        directory = ROOT / f"captures_{scene}"
        stem = f"m1contract_{scene}_baseline_N{N:04d}_m17"
        stats_path = directory / f"{stem}_probe_stats.json"
        if not stats_path.exists():
            results["scenes"][scene] = {"status": "missing_stats", "path": str(stats_path)}
            continue
        stats = json.loads(stats_path.read_text(encoding="utf-8-sig"))
        screen = analyze_screen(directory, stem)
        probe = analyze_probe_chain(stats)
        results["scenes"][scene] = {
            "status": "ok" if screen.get("status") == "ok" else screen.get("status", "bad"),
            "screen": screen,
            "probe_contract": probe,
        }

    ok = {
        scene: data for scene, data in results["scenes"].items()
        if data.get("status") == "ok"
    }
    if len(ok) == 2:
        cornell = ok["cornell"]
        sponza = ok["sponza"]
        c0_cornell = cornell["probe_contract"]["mean_lum_chain"][0]
        c0_sponza = sponza["probe_contract"]["mean_lum_chain"][0]
        ratio_c0 = c0_sponza / max(c0_cornell, EPS)
        ratio_screen_error = sponza["screen"]["ratio_self"] / max(cornell["screen"]["ratio_self"], EPS)
        ratio_screen_cascade_mean = sponza["screen"]["cascade_mean"] / max(cornell["screen"]["cascade_mean"], EPS)
        if ratio_screen_error > 4.0 and ratio_c0 < 2.0:
            direction = "not_global_probe_chain_energy; prioritize final_sampling_or_reference_local_contract"
        elif ratio_c0 > 2.0:
            direction = "probe_chain_energy_differs_by_scene; prioritize merge_energy_or_probe_placement"
        else:
            direction = "ambiguous_contract; add localized probe/world-position audit"
        results["cross_scene_summary"] = {
            "sponza_over_cornell_c0_mean_ratio": float(ratio_c0),
            "sponza_over_cornell_screen_cascade_mean_ratio": float(ratio_screen_cascade_mean),
            "sponza_over_cornell_ratio_self_ratio": float(ratio_screen_error),
            "direction": direction,
        }

    out = ROOT / "probe_contract_results.json"
    out.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
