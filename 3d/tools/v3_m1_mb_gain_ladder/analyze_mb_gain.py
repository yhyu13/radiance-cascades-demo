"""M1 Stage 9 — MB-gain Sponza ladder analyzer.

Reads per-scene per-gain captures (Stage 9 ladder + reused Stage 7/8 endpoints),
reports screen-level metrics, computes the linear-interp |p95|=0.50 crossing on
Sponza, and emits a fork recommendation per Stage 9 plan §4-§5.
"""

import importlib.util
import json
from pathlib import Path

import numpy as np


ROOT = Path("tools/v3_m1_mb_gain_ladder")
LOCAL_ANALYZER = Path("tools/v3_m1_local_sampling/analyze_local.py")
N = 2048
EPS = 1e-6
RETIREMENT_GATE = 0.50          # scope §7
CORNELL_P95_TOLERANCE = 0.10    # SC4: |p95| may rise by ≤10% above gain=1.0 baseline
CORNELL_RATIO_TOLERANCE = 0.10  # SC4: ratio_self may drop by ≤0.10 below gain=1.0 baseline


def load_local_module():
    spec = importlib.util.spec_from_file_location("local_sampling", LOCAL_ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def gain_tag(gain: float) -> str:
    return f"g{int(round(gain * 100)):03d}"


def capture_paths(scene: str, gain: float, source: str) -> dict[str, Path]:
    """Return EXR paths for a (scene, gain) point. `source` selects which capture
    directory to read from: stage9 (this stage), stage7 (Sponza baseline reuse),
    stage8 (Stage 8 captures: mb_off, mb_gain_half, hybrid_on, Cornell baseline)."""
    if source == "stage7" and scene == "sponza":
        directory = Path("tools/v3_m1_shader_contrib/captures_sponza")
        stem = f"m1shadercontrib_{scene}_baseline_N{N:04d}_m17"
    elif source == "stage8_mb_off":
        directory = Path(f"tools/v3_m1_source_energy_ab/captures_{scene}_mb_off")
        stem = f"m1stage8_{scene}_mb_off_N{N:04d}_m17"
    elif source == "stage8_mb_half":
        directory = Path(f"tools/v3_m1_source_energy_ab/captures_sponza_mb_gain_half")
        stem = f"m1stage8_sponza_mb_gain_half_N{N:04d}_m17"
    elif source == "stage8_cornell_base":
        directory = Path("tools/v3_m1_source_energy_ab/captures_cornell_baseline")
        stem = f"m1stage8_cornell_baseline_N{N:04d}_m17"
    else:  # "stage9"
        tag = gain_tag(gain)
        directory = ROOT / f"captures_{scene}_{tag}"
        stem = f"m1stage9_{scene}_{tag}_N{N:04d}_m17"
    return {
        "cascade": directory / f"{stem}_cascade_gi.exr",
        "pt_full": directory / f"{stem}_pt_full.exr",
        "pt_direct": directory / f"{stem}_pt_direct.exr",
        "gbuffer": directory / f"{stem}_gbuffer.exr",
        "diag": directory / f"{stem}_probe_diag.exr",
    }


def measure(mod, paths: dict[str, Path]) -> dict:
    missing = [str(p) for p in paths.values() if not p.exists()]
    if missing:
        return {"status": "missing", "missing": missing[:3]}

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

    ratio_valid = ratio[valid]
    return {
        "status": "ok",
        "valid": int(np.sum(valid)),
        "ratio_self": float(np.mean(ratio_valid)),
        "bad_pct": float(100.0 * np.mean(ratio_valid > 1.3)),
        "p95_abs": float(np.percentile(np.abs(ratio_valid - 1.0), 95)),
        # SC8: dump diag.rgb max so we can see if the Stage 8 anomaly is gain-dependent.
        "diag_rgb_max": float(diag[..., :3].max()),
    }


def measure_point(mod, scene: str, gain: float, sources: list[str]) -> dict:
    """Try each source in order; return the first successful measurement."""
    for src in sources:
        result = measure(mod, capture_paths(scene, gain, src))
        if result["status"] == "ok":
            result["source"] = src
            return result
    return {"status": "missing", "tried": sources}


def linear_interp_crossing(points: list[tuple[float, float]], target: float) -> float | None:
    """Given (gain, |p95|) ascending in gain, return gain at which |p95|=target.
    Returns None if no bracket crosses the target."""
    points = sorted(points, key=lambda p: p[0])
    for (g0, p0), (g1, p1) in zip(points, points[1:]):
        if (p0 - target) * (p1 - target) <= 0 and p1 != p0:
            t = (target - p0) / (p1 - p0)
            return g0 + t * (g1 - g0)
    return None


def main() -> int:
    mod = load_local_module()

    # Sponza gain points: 0.0 from Stage 9 (SC1 sanity) AND Stage 8 mb_off (cross-check);
    # 0.5 reused from Stage 8 mb_gain_half; 1.0 reused from Stage 7 baseline.
    sponza_gains = [
        (0.00, ["stage9", "stage8_mb_off"]),
        (0.10, ["stage9"]),
        (0.20, ["stage9"]),
        (0.30, ["stage9"]),
        (0.40, ["stage9"]),
        (0.50, ["stage8_mb_half", "stage9"]),
        (1.00, ["stage7", "stage9"]),
    ]
    cornell_gains = [
        (0.25, ["stage9"]),
        (0.50, ["stage9"]),
        (0.75, ["stage9"]),
        (1.00, ["stage8_cornell_base"]),
    ]

    sponza = {}
    for g, sources in sponza_gains:
        sponza[g] = measure_point(mod, "sponza", g, sources)
    cornell = {}
    for g, sources in cornell_gains:
        cornell[g] = measure_point(mod, "cornell", g, sources)

    # SC1 cross-check: stage9 gain=0.0 should match stage8 mb_off within 1% on |p95|.
    sc1 = {}
    s9 = measure(mod, capture_paths("sponza", 0.0, "stage9"))
    s8 = measure(mod, capture_paths("sponza", 0.0, "stage8_mb_off"))
    if s9.get("status") == "ok" and s8.get("status") == "ok":
        sc1 = {
            "stage9_gain0_p95": s9["p95_abs"],
            "stage8_mb_off_p95": s8["p95_abs"],
            "abs_diff": abs(s9["p95_abs"] - s8["p95_abs"]),
            "equivalent_within_1pct": abs(s9["p95_abs"] - s8["p95_abs"]) / max(s9["p95_abs"], EPS) < 0.01,
        }
    else:
        sc1 = {"status": "not_evaluable", "s9": s9.get("status"), "s8": s8.get("status")}

    # Crossing analysis.
    sponza_points = [
        (g, r["p95_abs"]) for g, r in sponza.items() if r.get("status") == "ok"
    ]
    crossing_gain = linear_interp_crossing(sponza_points, RETIREMENT_GATE)

    # Cornell tolerance band.
    cornell_baseline = cornell.get(1.00, {})
    tolerance_violations = []
    if cornell_baseline.get("status") == "ok":
        base_p95 = cornell_baseline["p95_abs"]
        base_ratio = cornell_baseline["ratio_self"]
        for g, r in cornell.items():
            if r.get("status") != "ok":
                continue
            p95_rise = r["p95_abs"] - base_p95
            ratio_drop = base_ratio - r["ratio_self"]
            violated = (p95_rise > CORNELL_P95_TOLERANCE) or (ratio_drop > CORNELL_RATIO_TOLERANCE)
            if violated:
                tolerance_violations.append({
                    "gain": g, "p95_rise": float(p95_rise), "ratio_drop": float(ratio_drop)
                })

    # Fork recommendation.
    if crossing_gain is None:
        fork = "NO_CROSSING"
        narrative = "Sponza |p95| never crosses 0.50 within the measured gain range."
    else:
        # Is Cornell at this gain still inside tolerance?
        # Find nearest measured Cornell point at or above the crossing gain.
        eligible = [(g, r) for g, r in cornell.items()
                    if r.get("status") == "ok" and g >= crossing_gain - 0.05]
        if not eligible:
            fork = "CORNELL_DATA_INSUFFICIENT"
            narrative = f"Crossing ≈ {crossing_gain:.3f} but no Cornell sample at or above this gain to evaluate trade."
        else:
            g_near, r_near = min(eligible, key=lambda gr: gr[0])
            base_p95 = cornell_baseline["p95_abs"] if cornell_baseline.get("status") == "ok" else None
            base_ratio = cornell_baseline["ratio_self"] if cornell_baseline.get("status") == "ok" else None
            if base_p95 is None:
                fork = "CORNELL_BASELINE_MISSING"
                narrative = "Cornell gain=1.0 baseline missing — can't evaluate trade."
            else:
                p95_rise = r_near["p95_abs"] - base_p95
                ratio_drop = base_ratio - r_near["ratio_self"]
                if p95_rise <= CORNELL_P95_TOLERANCE and ratio_drop <= CORNELL_RATIO_TOLERANCE:
                    fork = "FORK_GLOBAL_GAIN"
                    narrative = (f"Crossing ≈ {crossing_gain:.3f}; nearest Cornell sample at "
                                 f"gain={g_near} stays inside tolerance (|p95| rise={p95_rise:+.3f}, "
                                 f"ratio_self drop={ratio_drop:+.3f}). Global gain candidate.")
                else:
                    fork = "FORK_PER_SCENE_OR_PROBE"
                    narrative = (f"Crossing ≈ {crossing_gain:.3f}; Cornell at gain={g_near} violates "
                                 f"tolerance (|p95| rise={p95_rise:+.3f}, ratio_self drop={ratio_drop:+.3f}). "
                                 "No single global gain works; Stage 10 must pursue per-scene "
                                 "(Fork A) or probe-local (Fork B) gain.")

    output = {
        "sponza": {f"{g:.2f}": r for g, r in sponza.items()},
        "cornell": {f"{g:.2f}": r for g, r in cornell.items()},
        "sc1_gain0_vs_mb_off_equivalence": sc1,
        "crossing_estimate_p95_eq_050": crossing_gain,
        "cornell_tolerance_violations": tolerance_violations,
        "fork_recommendation": fork,
        "narrative": narrative,
    }
    out = ROOT / "mb_gain_ladder_results.json"
    out.write_text(json.dumps(output, indent=2), encoding="utf-8")

    # Concise stdout summary
    summary = {
        "sc1_equivalent": sc1.get("equivalent_within_1pct"),
        "sponza_p95_by_gain": {
            f"{g:.2f}": round(r.get("p95_abs", -1), 4)
            for g, r in sponza.items() if r.get("status") == "ok"
        },
        "cornell_p95_by_gain": {
            f"{g:.2f}": round(r.get("p95_abs", -1), 4)
            for g, r in cornell.items() if r.get("status") == "ok"
        },
        "crossing_gain_at_p95_050": (
            round(crossing_gain, 4) if crossing_gain is not None else None
        ),
        "fork": fork,
    }
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
