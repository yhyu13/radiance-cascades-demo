# v4 — ShaderToy Adoption: Clean Scope & Execution Plan

**Date:** 2026-05-28  
**Replaces:** `doc/7/v3_shadertoy_adoption_scope.md` (superseded by 11-stage M1 diagnostic chain)  
**Status:** v4 scope LOCKED. All predecessor closure docs preserved at `doc/7/`.

---

## 0. What Changed From v3

The v3 scope doc described M1 as 3 ShaderToy delta ports (#3 per-corner gating, #6 cone geometry, #4 MB formulation). What actually executed was an 11-stage diagnostic chain (Stages 0–11d) that:

1. Confirmed Deltas #3/#6 are DEAD (2×2 matrix: all combinations regress both scenes)
2. Narrowed the problem to multi-bounce feedback via per-cascade contribution analysis
3. Found Sponza's fix: MB gain=0.10 (|p95|=0.25, ratio=1.04, clears retirement gate)
4. Isolated Cornell's 2× under-emit to point-light surface sampling in the bake

The v3 scope doc is now documentation debt — it describes a plan that was abandoned after Stage 1. This v4 doc is the canonical reference.

**Key strategic shift:** The ShaderToy "adoption" is no longer about porting individual code lines. The v3 Deltas #3/#6 proved that copying ShaderToy merge formulas into the volumetric topology doesn't help. The real work is understanding the topological constraint (volumetric probes under-sample non-uniformly-lit surfaces) and shipping what works within that constraint.

---

## 1. Current State (May 28, 2026)

### Sponza — GREEN (ready to ship)

| Metric | Cascade (default) | Cascade (gain=0.10) | Hybrid | PT |
|--------|---------|---------|--------|-----|
| ratio_self | 4.71 | **1.04** | 0.83 | 1.00 |
| |p95| | 4.53 | **0.25** | 0.31 | — |
| bright% | 100% | 1.7% | 0% | — |
| dim% | 0% | 0% | 6.6% | — |
| mode-0 RMS vs PT | 0.206 | **0.020** | 0.010 | — |

Sponza at gain=0.10 clears the strict |p95|≤0.50 retirement gate by 50% margin. Stage 10 mode-0 visual validation confirms this is a real improvement (10× RMS reduction, 95% gap closure to hybrid), not a measurement artifact.

### Cornell — YELLOW (mechanism understood, fix limited by topology)

| Config | ratio_self | |p95| |
|--------|-----------|------|
| Point light (default) | **0.49** | 0.88 |
| Directional light | **0.93** | — |
| Hybrid (point) | 0.83 | — |

The 2× under-emit under point lights is NOT a coding bug. It's the volumetric topology's sampling limitation: probe rays are cast uniformly over S² from each probe position. When surface outgoing radiance is non-uniform (point light creates a bright center patch on the floor with dark edges), the cascade undersamples the bright region. Directional light uniformly illuminates surfaces → cascade samples correctly (ratio=0.93).

**Fix ceiling:** No merge-formula tweak, bin-resolution bump, or gain adjustment can close this gap within the volumetric topology. The v2.x program proved this (v2.4 dirRes DEAD, v2.2 merge-formula DEAD, v2.4.b clamp DEAD). The fix that works is hybrid per-pixel correction — or Path B (surface-attached probes).

### Stale Code (M1 Delta flags)

| Flag | Default | GUI? | CLI? | Status |
|------|---------|------|------|--------|
| `m1Delta3GatedTrilinear` | false | No | No | DEAD — regresses both scenes |
| `m1Delta6GeometricCone` | false | No | No | DEAD — regresses Cornell, partial Sponza |

These are dead code. The shader uniforms exist but nothing can toggle them. Phase 2 cleanup will remove them.

---

## 2. The Volumetric Constraint (Why We Can't Close Cornell Without Topology Change)

The fundamental diagram:

```
Point light at (0, 0.8, 0):
  ┌─────────────────┐
  │  ┌───────────┐  │  Ceiling (directly lit)
  │  │  ● light  │  │
  │  └───────────┘  │
  │                 │
  │  ░░░░░░░░░░░░░░░│  Floor: bright center (directly lit near light)
  │  ░░██████████░░░│         dark edges (cosine falloff far from light)
  │  ░░██████████░░░│         very dark corners
  │  ░░██████████░░░│
  └─────────────────┘

Probe rays from 3D grid:
  ✓ Ray hits bright floor center → captures full energy
  ✗ Ray hits dark floor edge   → captures ~0.3× energy
  ✗ Ray hits dark floor corner → captures ~0.1× energy
  ✗ Ray hits ceiling           → captures ceiling energy (not floor)
  
Average = less than uniform-illumination average → 2× under-emit

Directional light:
  ┌─────────────────┐
  │  ↓ ↓ ↓ ↓ ↓ ↓ ↓ │  All surfaces uniformly lit
  │  ↓ ↓ ↓ ↓ ↓ ↓ ↓ │
  │  ↓ ↓ ↓ ↓ ↓ ↓ ↓ │  Probe rays hit ANYWHERE → ~same radiance
  │  ↓ ↓ ↓ ↓ ↓ ↓ ↓ │  → captures correct energy
  └─────────────────┘
```

Path B (surface-attached) fixes this because probes sit ON surfaces and sample the hemisphere above their normal — every probe ray originates from the surface position, so the surface's outgoing radiance is captured exactly at the probe location, not by stochastic rays from nearby probes.

In the ShaderToy reference, probes are placed on each wall surface. A probe on the floor center (brightly lit) captures the floor's outgoing radiance perfectly into its atlas bins. A probe on the floor corner (dimly lit) captures dim radiance. The cascade hierarchy merges them correctly across spatial scales. In our volumetric grid, a probe floating 1-2 cells away from the floor has a ~3% chance of any given ray hitting the bright center patch — most rays hit dimmer regions or the ceiling.

---

## 3. Milestones

### Phase 1 — Ship What Works (today, ~2 h)

**Goal:** Land the verified Sponza fix, close Cornell investigation, update documentation.

#### Phase 1A — Sponza per-scene MB-gain preset (~30 min)

- Add `--mb-gain-per-scene=1` CLI flag (default OFF)
- When ON: set `multiBounceGain = 0.10` for Sponza-class scenes, `1.0` for others
- Scene class detection: on `loadOBJMesh`, if `isSponza → gain=0.10`, else `gain=1.0`
- ImGui: show active gain in status bar; gray out manual gain slider when per-scene is ON
- Re-capture Sponza baseline at gain=0.10 to lock in `baseline_lock.json`
- ~15 lines of C++, no shader changes

**Acceptance:** Sponza mode-17 cascade captures produce |p95|≤0.30 at N=2048 with per-scene gain enabled. Per-scene gain is opt-in (does not change current default behavior).

#### Phase 1B — Cornell confirm and close (~90 min)

1. **Cornell directional capture confirmation** — re-run Stage 11c directional capture after build to verify ratio=0.93 reproduces
2. **Document Cornell constraint** — write `doc/8_shadertoy/cornell_point_light_constraint.md` with the volumetric topology explanation and the fix ceiling
3. **No code changes for Cornell** — the fix is topological (Path B) or correction-layer (hybrid ON). Do not implement a half-fix

**Acceptance:** Cornell documentation on disk. Decision recorded: hybrid stays ON for Cornell-class enclosed-geometry point-light scenes.

### Phase 2 — Cleanup & Closeout (~90 min)

#### 2A — Update scope doc and project documentation

- Mark this v4 doc as the canonical scope
- Update `CLAUDE.md` / project memory with the topology constraint finding
- Archive v3 scope doc with a "SUPERSEDED BY v4" header

#### 2B — Remove stale M1 delta flags

- Remove `m1Delta3GatedTrilinear`, `m1Delta6GeometricCone` from `demo3d.h/cpp`
- Remove `uM1Delta3GatedTrilinear`, `uM1Delta6GeometricCone` from `radiance_3d.comp`
- Remove the `setM1Delta3GatedTrilinear`, `setM1Delta6GeometricCone` accessors
- Remove the hardcoded cone computation (kept behind `m1Delta6GeometricCone`)
- Rebuild and re-run Cornell/ Sponza baselines to confirm bit-identical output
- ~30 lines removed, no behavioral change (flags were always OFF)

#### 2C — Baseline lock update

- Update `baseline_lock.json` with the clean-build baselines
- Add Sponza-gain=0.10 entry

### Phase 3 — Path B Decision Gate (deferred)

**Trigger:** user decision after Phase 2 closeout. **Do not auto-proceed.**

Path B is a 3–6 session surface-attached topology rewrite. It addresses the Cornell point-light constraint. Decision factors:

| Factor | For Path B | Against Path B |
|--------|-----------|----------------|
| Cornell quality | Fixes the 2× under-emit at root | Sponza already works; Cornell works with hybrid |
| Scene generality | Works for all lighting types | Volumetric works for all but point-light enclosed |
| Implementation cost | 3–6 sessions | Hybrid is already implemented |
| Maintenance | One code path (cascade only) | Two code paths (cascade + hybrid) |
| ShaderToy fidelity | Exact match | Close but not exact |

**Default recommendation:** Do not proceed to Path B unless a new scene requirement makes the Cornell point-light constraint a blocking issue. The hybrid correction already closes the Cornell gap (ratio 0.83) at acceptable quality. The combination of cascade (Sponza, open scenes, directional) + hybrid (Cornell, enclosed point-light) covers all tested configurations.

---

## 4. Pre-Committed Gates

| Phase | Gate | Pass condition | Fail action |
|-------|------|----------------|-------------|
| 1A | Sponza gain=0.10 baseline | |p95| ≤ 0.30 at N=2048 | Debug the gain application; do not proceed to 1B |
| 1A | Default regression | Default (per-scene OFF) produces same metrics as M0 baseline within ±5% | Fix the feature gate; per-scene must be strictly opt-in |
| 1B | Cornell directional | ratio_self ≥ 0.85 after rebuild | Investigate build regression; do not document constraint until confirmed |
| 2B | Cleanup regression | Bit-identical EXR output before/after M1 flag removal | Revert removal; flag removal must be a true no-op |
| 2C | Lock integrity | All capture entries have SHA256 matches with on-disk files | Re-capture missing files |

---

## 5. Rollback Criteria

- **Phase 1A reverted** if Sponza gain=0.10 produces |p95| > 0.50 (gate miss). Investigate configuration drift.
- **Phase 2B reverted** if EXR checksum differs. M1 flag removal must be strictly dead-code removal.
- **Any phase reverted** if build fails or new warnings appear.

---

## 6. What We Learned (Cerebrum Carries Forward)

### The volumetric constraint is real and terminal

3D-grid probes cannot capture non-uniform surface outgoing radiance at full fidelity when the lighting is localized (point/spot). The D² uniformly-distributed probe rays average over the visible hemisphere, and if the lit patch is small, the average is biased low. This is NOT fixable by:
- More probes (v2.4 dirRes DEAD)
- Different merge formulas (v2.2 DEAD)
- Output clamps (v2.4.b DEAD)
- ShaderToy delta ports (v3 M1 Stage 1 DEAD)

The fix IS:
- Surface-attached probes (Path B)
- OR per-pixel correction (hybrid)
- OR directional/uniform lighting

### The ShaderToy surface-attached topology is the "real" fix

Every constraint we hit (Cornell 2× under-emit, C1→C2 bright-tail leak, Sponza over-bright at default gain) traces back to the volumetric probe placement. The ShaderToy reference avoids ALL of these by placing probes ON surfaces with hemisphere-restricted sampling. Our v2.x program spent 31 commits failing to fix these with parameter tuning. The v3 M1 delta port failed for the same reason: the deltas assume surface-attached semantics (WeightedSample's cone is probe-apparent-extent, not bin-width; merge is surface-bilinear, not volume-trilinear).

### But the volumetric approach works well enough

Sponza at gain=0.10 is measurably excellent. Cornell at directional is 93% of PT. The cascade pipeline IS correct for scenes where lighting is broad (directional, large area lights, open-atrium diffuse bounce). The hybrid correction covers the remaining case (point-light enclosed). Shipping with both is the honest engineering answer.

---

## 7. Cross-References

- v3 scope (superseded): `doc/7/v3_shadertoy_adoption_scope.md`
- v3 M1 Stage 1 (DEAD): `doc/7/v3_m1_stage1_delta36_matrix_impl.md`
- v3 M1 Stage 9 (gain ladder): `doc/7/v3_m1_stage9_mb_gain_ladder_impl.md`
- v3 M1 Stage 10 (mode-0 validation): `doc/7/v3_m1_stage10_mode0_visual_ab_impl.md`
- v3 M1 Stage 11b (consumer audit): `doc/7/v3_m1_stage11b_cornell_consumer_audit_impl.md`
- v3 M1 Stage 11c (light type): `doc/7/v3_m1_stage11c_light_type_discriminator_impl.md`
- v3 M1 Stage 11d (light distance): `doc/7/v3_m1_stage11d_light_distance_ladder_impl.md`
- v2.x failure-learnings: `doc/7/v25_z_mbrc_correction_failure_learnings.md`
- ShaderToy reference: `shader_toy/CubeA.glsl`
- Current bake shader: `res/shaders/radiance_3d.comp`
- Audit: `doc/8_shadertoy/01_audit_v3_status_and_gaps.md`
- Correction: `doc/8_shadertoy/02_correction_m1_flags_dead_code.md`
- Reference quickref: `doc/8_shadertoy/03_shadertoy_reference_quickref.md`