# v4 ShaderToy Adoption — Final Closeout Report

**Date:** 2026-05-28  
**Program:** v4 ShaderToy Adoption (supersedes v3)  
**Duration:** ~3 sessions (audit, Phase 1-2 implementation, Phase 3 closeout)  
**Predecessor programs:** v2.x MBRC correction (31-commit FAILED), v3 M1 delta port (2×2 matrix DEAD)

---

## 1. Program Goal vs. Actual Outcome

### Original v3 goal (2026-05-26)

> "Fully adopt the in-tree ShaderToy 3D RC reference as the production radiance pipeline. Retire the current `radiance_3d.comp` bake chain and the hybrid per-pixel correction safety net."

### v4 actual outcome (2026-05-28)

**The ShaderToy reference was NOT fully adopted.** The volumetric topology cannot replicate the surface-attached reference's behavior for point-light enclosed geometry. Instead, the program:

1. **Found scene-specific fixes that work within the volumetric constraint**
2. **Documented the constraint as a topology limit, not a code bug**
3. **Shipped a per-scene MB-gain preset for Sponza that clears the retirement gate**
4. **Accepted hybrid correction as the Cornell fix (Path B deferred)**

The original goal of "retire hybrid" was NOT achieved. The hybrid correction remains ON for Cornell-class scenes because the volumetric cascade has a fundamental 2× under-emit for point lights in enclosed geometry.

---

## 2. What Failed

### v2.x MBRC Correction (31 commits, 2026-05-19 → 2026-05-26)

| Hypothesis | Mechanism | Verdict |
|-----------|-----------|---------|
| γ (gamma) | Angular under-sampling at C0 | REJECTED |
| β (beta) | MB-gain wrong fixed point | LEVERAGE_NOT_CURE |
| α (alpha) | Merge-time directional weighting | LEVERAGE_WRONG_DIR |
| δ (delta) | Spatial probe-density | REJECT |
| h.a..h.c | Merge variant × MB factorial | No fix emerged |
| v2.2 | aFactor reshape | KILLED at Step 0 |
| v2.3 | Leak attribution at C0 | MARGINAL |
| v2.4 | C0 dirRes 8→16 | DEAD |
| v2.4.b | Output luminance clamp K=2 | DEAD |
| v2.5 | Per-cascade isolation (C1→C2 merge) | CLEAR_ATTRIBUTION (no fix) |

**Key learning:** The bright tail is structural, not parametric. No knob the current architecture exposes can isolate it.

### v3 M1 Delta Port (2026-05-27)

| Condition | Cornell | Sponza | Combined |
|-----------|---------|--------|----------|
| delta3 (per-corner gated trilinear) | DEAD | MISSING | DEAD |
| delta6 (geometric cone widening) | DEAD | MARGINAL | DEAD |
| both | DEAD | DEAD | DEAD |

**Key learning:** Copying ShaderToy merge-formula fragments into the volumetric topology doesn't help. The two architectures differ at a deeper level (surface-attached vs volumetric probe placement) that individual code lines can't bridge.

---

## 3. What Works

### Sponza — Cascade-Only GI at MB Gain=0.10

| Metric | Default (gain=1.0) | Fixed (gain=0.10) | PT Reference | Retirement Gate |
|--------|-------------------|-------------------|--------------|-----------------|
| ratio_self | 4.71 | **1.04** | 1.00 | — |
| \|p95\| | 4.53 | **0.25** | 0 | ≤ 0.50 |
| bright% | 100% | 1.7% | 0% | ≤ 5% |
| dim% | 0% | 0% | 0% | ≤ 5% |
| mode-0 RMS | 0.206 | **0.020** | — | — |

Sponza at gain=0.10 clears the strict |p95|≤0.50 retirement gate by **50% margin**. Stage 10 mode-0 visual validation confirms 10× composite RMS reduction (95% of cascade-vs-hybrid gap closed). This is a real visual improvement, not a measurement artifact.

**Shipped as:** `--mb-gain-per-scene` CLI flag (Phase 1A). When enabled, Sponza-class scenes automatically use MB gain=0.10.

### Cornell — Directional Light Closes 87% of Gap

| Light type | cascade_gi / pt_gi |
|------------|---------------------|
| Point light (default) | 0.49 |
| Directional (0,-1,0) | **0.93** |
| Hybrid correction (point) | 0.83 |

Directional light produces uniform surface illumination → every probe ray sees ~same radiance → cascade samples correctly. This proves the cascade algorithm IS correct for uniformly-lit surfaces. The point-light deficit is a sampling limitation of the volumetric topology.

### Cornell — Hybrid Correction Covers Point-Light Case

Hybrid per-pixel correction (MC bounce-1) achieves ratio=0.83 on Cornell point-light, up from cascade-only 0.49. This is not ShaderToy-quality (0.93+) but is acceptable for production use.

---

## 4. What Was Shipped

### Phase 1A — Sponza Per-Scene MB-Gain Preset

| File | Lines |
|------|-------|
| `src/main3d.cpp` | +18 (global flag, CLI arg, post-load apply block) |
| `src/demo3d.h` | +7 (member + setter) |
| `src/demo3d.cpp` | +14 (ImGui gray-out, status indicator) |

**Behavior:** Opt-in via `--mb-gain-per-scene` CLI flag. Sponza → gain=0.10, others → gain=1.0. ImGui slider grayed out when active. No behavioral change when flag is OFF.

### Phase 2B — Remove Stale M1 Delta Flags

| File | Lines removed |
|------|--------------|
| `src/main3d.cpp` | -13 (2 CLI args) |
| `src/demo3d.h` | -24 (2 setters + 2 members) |
| `src/demo3d.cpp` | -7 (2 uniform calls + cone branch) |
| `res/shaders/radiance_3d.comp` | -10 (2 uniforms + ternary + aFactor guard) |

**Net source delta: -10 lines.** Build verified. EXR output bit-identical to pre-Phase-2B binary (flags were always `false`).

### Documentation

| Doc | Purpose |
|-----|---------|
| `v4_shadertoy_adoption_scope.md` | Canonical v4 plan + constraints |
| `cornell_point_light_constraint.md` | Volumetric topology limitation documented |
| `v4_phase1_impl.md` | Phase 1 implementation summary |
| `v4_phase2_plan.md`, `v4_phase2_impl.md` | Phase 2 plan + implementation |
| `v4_phase3_plan.md` | Phase 3 verification plan |
| `baseline_lock.json` | Updated with v4 build SHA256 + Sponza pscene entry |
| `doc/7/v3_shadertoy_adoption_scope.md` | Marked SUPERSEDED with v4 cross-link |
| `doc/8_shadertoy/04_reply_to_audit.md` | Phase 2B removal note appended |

---

## 5. What Remains Open

| Issue | Severity | Resolution |
|-------|----------|------------|
| Cornell point-light under-emit (2×) | HIGH | Documented topological constraint. Hybrid covers (0.83). Path B is the algorithmic fix. |
| Path B decision (surface-attached topology) | DEFERRED | 3-6 session rewrite. User decides. |
| Sponza valid mask (693 pixels) | MEDIUM | Uniform measurement floor across all stages. Adaptive threshold queued. |
| Analyzer threshold (pt_lum > 0.05) | MEDIUM | Too high for Sponza at N=2048. Shrinks with convergence. |
| Mode-0 visual confirm for `--mb-gain-per-scene` | LOW | Stage 10 validated gain=0.10 but not the per-scene flag specifically. |
| Sponza pscene capture confirmation | LOW | Lock has `expected_metrics` from Stage 9; pending_capture for this build. |

---

## 6. Path B Decision Tree

**Path B** = Surface-attached topology rewrite (3-6 sessions). Probes on surfaces with hemisphere-restricted sampling, like the ShaderToy reference.

| Factor | Go | No-Go |
|--------|-----|-------|
| Cornell quality | Fixes 2× under-emit at root (ratio → ~1.0) | Hybrid already covers (0.83) |
| Scene generality | Works for all lighting types | Volumetric works for open/directional |
| Code maintenance | Single path (cascade only) | Two paths (cascade + hybrid) |
| Implementation cost | 3-6 sessions | One session already spent on v4 |
| ShaderToy fidelity | Exact match | Close match (Sponza 1.04, Cornell-dir 0.93) |

**Recommendation:** Do NOT proceed to Path B unless a new scene requirement makes the Cornell point-light constraint a blocking issue. The current solution (cascade for open/directional + hybrid for enclosed/point) covers all tested configurations at acceptable quality.

---

## 7. The Volumetric Constraint (Final Formulation)

The cascade pipeline is correct for scenes where lighting is broad (directional, large area lights, open-atrium diffuse bounce). It is structurally biased for scenes where lighting is localized (point/spot in enclosed geometry).

The constraint is NOT fixable by:
- More probes or higher angular resolution (v2.4 DEAD)
- Different merge formulas (v2.2 DEAD)
- Output-side symptom clamps (v2.4.b DEAD)
- ShaderToy merge formula ports (v3 M1 Stage 1 DEAD)
- MB gain tuning (v3 M1 Stage 9 — ratio stays 0.49 at every gain)

The constraint IS fixable by:
- Surface-attached probes (Path B — probes on surfaces, hemisphere-restricted sampling)
- Per-pixel MC correction (hybrid — already implemented)
- Directional/uniform lighting (CLI flag — already implemented)

This is NOT a code bug. It's a sampling limitation of uniform-S² probe rays applied to non-uniform surface outgoing radiance. The ShaderToy reference avoids this because probes are on surfaces and sample the hemisphere above their normal — every probe ray originates from the surface position, so the surface's radiance is captured exactly.

---

## 8. Cross-References

**Canonical docs (this program):**
- `doc/8_shadertoy/v4_shadertoy_adoption_scope.md` — v4 scope
- `doc/8_shadertoy/cornell_point_light_constraint.md` — Cornell constraint
- `doc/8_shadertoy/v4_phase1_impl.md` — Phase 1 implementation
- `doc/8_shadertoy/v4_phase2_impl.md` — Phase 2 implementation

**Predecessor programs (for reference):**
- `doc/7/v3_shadertoy_adoption_scope.md` — v3 scope (SUPERSEDED)
- `doc/7/v25_z_mbrc_correction_failure_learnings.md` — v2.x closeout
- `doc/7/v3_m1_stage1_delta36_matrix_impl.md` — Delta #3/#6 DEAD verdict
- `doc/7/v3_m1_stage9_mb_gain_ladder_impl.md` — Sponza gain=0.10 discovery
- `doc/7/v3_m1_stage10_mode0_visual_ab_impl.md` — Mode-0 visual validation
- `doc/7/v3_m1_stage11b_cornell_consumer_audit_impl.md` — BAKE_UNDER_EMITS
- `doc/7/v3_m1_stage11c_light_type_discriminator_impl.md` — LIGHT_TYPE_DOMINANT
- `doc/7/v3_m1_stage11d_light_distance_ladder_impl.md` — Multi-bounce under-emit

**Reference implementation:**
- `shader_toy/CubeA.glsl` — ShaderToy surface-attached cascade