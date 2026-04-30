# Phase 5 Self-Critique

**Date:** 2026-04-29
**Branch:** 3d
**Scope:** All Phase 5 sub-phases (5a–5e)

---

## Verdict

Phase 5 is structurally sound: the octahedral encoding is correct, the atlas layout is well-designed, and the cascade chain logic is coherent. But the phase as a whole has a critical maturity gap — **not a single sub-phase has been visually validated at runtime**. Five sub-phases of code were stacked on top of each other with compile as the only gate. One bug (D=2 degenerate case in 5e) was caught only because the app was finally run. There may be others.

The secondary concern is three features that are either inert (Phase 5d visibility check), unquantified in benefit (Phase 5c directional merge on final image), or only partially tested in the specific A/B configuration they were designed for (Phase 5e).

---

## Findings

### 1. High — All of Phase 5 is compile-verified only; no visual A/B has been run

**Evidence:**
- `doc/cluade_plan/phase5bc_impl_learnings.md:187` — "Runtime visual validation pending"
- `doc/cluade_plan/phase5d_impl_learnings.md:5` — "Runtime GI A/B pending"
- `doc/cluade_plan/phase5e_impl_learnings.md` — all runtime rows "Pending"
- `doc/cluade_plan/PLAN.md:148` — "Visual A/B (directional vs isotropic merge toggle) has not yet been run."

The stated goal of Phase 5 was "GI banding at cascade interval boundaries is visibly reduced with per-direction merge active vs Phase 4 baseline." That measurement has never been taken. Sub-phases 5d and 5e were designed and implemented before 5c was even visually confirmed to work. This is the most significant quality gap.

---

### 2. High — Phase 5e shipped with a degenerate D=2 formula that caused visible wall-color bleed

**Evidence:**
- `src/demo3d.cpp`: original formula `1 << (i+1)` = [D2, D4, D8, D16]
- `doc/cluade_plan/phase5e_impl_learnings.md` — "D=2 Degenerate Case" proof

All 4 D=2 bin centers land exactly on the octahedral equatorial fold (z=0 plane). C0's D=2 rays are all horizontal. When C0 reads C1's atlas (D=4) via `dirToBin(horizontal_ray, 4)`, it hits C1's corner bins (1,1)/(1,3)/(3,1)/(3,3), which represent mostly-vertical directions (z ≈ ±0.816). C0 was reading ceiling/floor data for its horizontal rays — causing green wall radiance to appear on the red wall.

The fix changes the formula to `min(16, dirRes << i)` = [D4, D8, D16, D16], which keeps C0 at D=4. But this changes the A/B semantics: **C0 and C1 now have identical D in both scaled and fixed modes**. Only C2 and C3 change. The A/B for Phase 5e is therefore a test of whether scaling C2/C3 from D=4 to D=16 has any visible effect — a much weaker test than the intended full-cascade comparison.

---

### 3. Medium — Phase 5d visibility check is dead code that runs every frame

**Evidence:**
- `res/shaders/radiance_3d.comp:224-243` — the visibility block
- `doc/cluade_plan/phase5d_impl_learnings.md` — structural no-op proof: `distToUpper ≈ 0.108m < tMin_upper = 0.125m` for all cascade pairs

In non-co-located mode, the GPU evaluates `wUpper`, `toCurrentFromUpper`, `distToUpper`, `dirToBin`, `texelFetch` (visibility atlas read), and the `visHit < distToUpper * 0.9` condition — and always fails the condition. For C0 running D=4 (16 bins per probe) across 32³ probes, this is **32768 × 16 = 524,288 wasted visibility evaluations** per bake pass.

The root cause is structural: the 4x-interval / 2x-halving scheme guarantees `tMin_upper > distToUpper_max` for all cascade pairs. The check cannot fire without changing either the interval ratio or the halving ratio.

---

### 4. Medium — `upperProbePos` is computed inside the per-direction inner loop

**Evidence:**
- `res/shaders/radiance_3d.comp:206-208` — inside `for(dy) { for(dx) { ... } }`

```glsl
ivec3 upperProbePos = (uUpperToCurrentScale > 0)
    ? (probePos / uUpperToCurrentScale)
    : ivec3(0);
```

`upperProbePos` depends only on `probePos` (constant per probe invocation) and `uUpperToCurrentScale` (constant per dispatch). With Phase 5e D=16 for C2/C3, this runs 256 times per probe instead of once. It should be hoisted above the loop.

Similarly, `upperBin` (`dirToBin(rayDir, uUpperDirRes)`) is correctly inside the loop (it depends on `rayDir`), but the Phase 5d visibility block is also fully inside the loop, recomputing `wUpper` and `toCurrentFromUpper` for every direction bin — despite the fact that visibility is a probe-to-probe spatial check independent of direction. That block should be hoisted outside the loop entirely (one visibility check per probe pair, not D² checks).

---

### 5. Medium — The benefit of Phase 5c directional merge on the final image is unquantified and may be modest

**Evidence:**
- `res/shaders/reduction_3d.comp:28-34` — simple unweighted average of D² bins
- `res/shaders/raymarch.frag` — reads `texture(uRadiance, uvw)` from the isotropic `probeGridTexture`

The display path always reads the isotropic `probeGridTexture`, which is the unweighted average of all D² bins. The directional merge in Phase 5c improves what gets written into each bin (miss rays get the upper cascade's directional value, not its average). This improvement survives the reduction: a C0 probe near a red wall gets red for leftward bins and green for rightward bins, averaged to a physically-accurate isotropic estimate.

However, the reduction uses **uniform weighting** (each bin counts equally). In a D=4 grid, the 4 "corner" bins of the octahedral map cover disproportionately small solid angles compared to "equatorial" bins. The unweighted average introduces a systematic angular-sampling bias. Whether this bias is visible at D=4 is unknown — it was never compared against Phase 4 baseline.

---

### 6. Low — Redundant condition in the Phase 5d visibility block

**Evidence:**
- `res/shaders/radiance_3d.comp:224` — `if (uUpperToCurrentScale == 2 && uHasUpperCascade != 0)`

`uUpperToCurrentScale == 2` is only set when `hasUpper5d == true`, which requires the upper cascade to be active and have a valid atlas — the same condition that sets `uHasUpperCascade = 1`. The second check is always true when the first is true. Minor dead evaluation, but misleads readers into thinking these could differ.

---

### 7. Low — Eight toggle combinations exist; only the default has been verified

**Evidence:**
- Three binary toggles: `useDirectionalMerge`, `useColocatedCascades`, `useScaledDirRes`
- 2³ = 8 combinations

The zero-regression invariant (all OFF → identical to Phase 4) has been reasoned about but not measured. Non-co-located + D-scaled (the combination closest to the ShaderToy reference) is the highest-risk path and has never been run. Atlas dimension changes combine with per-cascade D changes in this mode; a single off-by-one in the atlas indexing would be silent (reads return stale data, no GL error).

---

### 8. Low — Phase 5e A/B only stresses C2/C3; C0/C1 boundary unchanged

After the D=2 fix, the "fixed" and "scaled" modes differ only at C2 and C3 (D=4 → D=16). Cascade boundary banding at C0/C1 — the closest and most visually prominent boundary — is identical in both modes. If the visible banding concern is at the C0/C1 edge (most likely given scale), Phase 5e cannot address it. A future Phase 5f test might need C0=D4 vs C0=D8 as the comparison axis.

---

### 9. Low — `PLAN.md` status table does not reflect Phase 5d or 5e

**Evidence:**
- `doc/cluade_plan/PLAN.md:139-148` — Phase 5 table stops at 5c + debug; 5d and 5e are absent
- The Phase 5 "done when" criterion (`doc/cluade_plan/PLAN.md:157`) has not been checked

The plan file is the canonical project status record. A reader of `PLAN.md` would not know Phase 5d (non-co-located toggle) or Phase 5e (D scaling) exist.

---

## Where Phase 5 Is Strong

- **Octahedral encoding**: `dirToBin(binToDir(b, D), D) == b` holds for all valid bins at D≥4. Full sphere coverage confirmed analytically. No pole artifacts from this parameterization.
- **Atlas layout**: `(probePos.x * D + dx, probePos.y * D + dy, probePos.z)` bounds are safe by construction for all valid probe positions and bin indices.
- **`uBaseInterval` / `uProbeCellSize` split**: correctly maintains tMin/tMax from C0's cellSize while allowing per-cascade probe-to-world mapping. This was the load-bearing change for Phase 5d and required precise reasoning.
- **Reduction pass**: simple, correct, dispatch-aligned with the atlas bake pass. Correctly handles per-cascade resolution.
- **Destroy+rebuild pattern**: all atlas-dimension toggles (5d, 5e) correctly call `destroyCascades()` + `initCascades()` + `cascadeReady = false`. No texture dimension mismatch bugs.
- **Backward-compatibility invariant**: when all three toggles are at default, `uUpperDirRes == uDirRes`, `dirToBin(binToDir(b,D),D) == b`, and `upperAtlasTxl == atlasTxl` for co-located mode. Phase 5c ON is behaviorally equivalent to Phase 4 except for the directional bin lookup — the only difference is precision.

---

## Priority Actions

| Priority | Action |
|---|---|
| P0 | Run the app: Phase 5c ON vs OFF visual A/B at the C0/C1 boundary |
| P0 | Run the app: Phase 5e D-scaled vs fixed ON, check C2/C3 banding |
| P1 | Hoist `upperProbePos` above the per-direction loop |
| P1 | Hoist the Phase 5d visibility block above the per-direction loop (one check per probe pair) |
| P1 | Update `PLAN.md` Phase 5 table with 5d and 5e status |
| P2 | Consider removing or disabling the Phase 5d visibility block pending a structural fix (distAlongRay alternative) |
| P2 | Add solid-angle-weighted averaging to the reduction pass (replace uniform weights with per-bin solid angle) |
