# Unified Visibility Plan — Mode 4 First, Interval Merge Second

**Date:** 2026-05-12
**Supersedes:** [probe_visibility_acceleration_plan.md](probe_visibility_acceleration_plan.md), [visibility_acceleration_plan.md](visibility_acceleration_plan.md)
**Synthesizes:** Both plans + [visibility_plan_comparison.md](visibility_plan_comparison.md)
**Revised:** 2026-05-12 in response to [critic 04](critic/04_visibility_unified_plan_review.md) / [reply 04](critic/reply/reply_04_visibility_unified_plan_review.md). Phase 1 algorithm now matches the corrected signed-projection from [reply 03](critic/reply/reply_03_probe_visibility_acceleration_plan_review.md); Phase 2 α-merge now matches the RC paper's interval merge.

## TL;DR

Two phases, sequenceable, the second optional if the first is good enough:

1. **Phase 1 — Mode 4 (render-only, days).** For each direction bin, signed-project the surface→probe vector onto the bin's direction; visible if the projection is no farther than the probe's stored hit distance. Reuses already-baked `hit.a`. Restores acceptable quality at expected-small cost above mode 0 (verify in Step 5), no atlas format change, no bake change.
2. **Phase 2 — Interval atlas (architectural, 2–4 days).** Promote the directional atlas from RGB to RGBA where α is the per-bin transparency interval. Modify the bake-time cascade-inheritance merge to follow the RC paper's interval composition. Mark `probeVisibility()`, `uVisibilityMode`, and modes 0..4 as deprecated; delete in a cleanup commit after verification.

Phase 1 gets the user out of the "mode 3 is too expensive / mode 0 leaks" trap immediately. Phase 2 is the architecturally correct destination per the RC paper and is the only one of the two that fixes bake-time light leaks.

---

## Why this sequencing

From [visibility_plan_comparison.md](visibility_plan_comparison.md):

- **Same diagnosis, same key insight.** `hit.a` per bin is already in the atlas; both source plans want to use it. The disagreement is only about how aggressively to refactor.
- **Phase 1 (Mode 4) is the lowest-risk path** to fix the user's immediate complaint. It is render-side only — atlas format unchanged, bake unchanged, every existing mode preserved as A/B reference.
- **Phase 2 (interval atlas) addresses what Phase 1 cannot:** bake-time cross-wall leakage in `radiance_3d.comp`'s cascade inheritance. It also retires the entire `uVisibilityMode` switch as obsolete.
- **Phases are sequenceable but not informationally entangled.** Phase 2 deletes Mode 4 and uses a different formula. The honest framing: Phase 1 ships a quality fix immediately and validates that `hit.a` semantics work in production (the data is fresh after the temporal_blend patch below, the per-pixel test produces continuous output). Phase 2's correctness is justified independently of Phase 1's outcome.

---

## Phase 1 — Mode 4: depth-aware per-bin visibility (signed projection)

### Goal

Eliminate H6's dot-banding at near-mode-0 cost, without touching the atlas format or the bake.

### Hard prerequisite — temporal_blend.comp patch

Per [reply 03 F3](critic/reply/reply_03_probe_visibility_acceleration_plan_review.md), `temporal_blend.comp:82` currently EMA-blends the entire vec4, corrupting the alpha channel (which carries `hit.a` distance). Mode 4 reads that alpha; without this patch it reads garbage.

```glsl
// Before:
imageStore(oHistory, coord, mix(his, cur, uAlpha));

// After:
vec4 blended = mix(his, cur, uAlpha);
blended.a    = cur.a;   // hit distance: use fresh, not blended
imageStore(oHistory, coord, blended);
```

Same pattern that `radiance_3d.comp:428` already uses. Likewise the AABB clamp at `temporal_blend.comp:79` should clamp `.rgb` only:

```glsl
his.rgb = clamp(his.rgb, nMin.rgb, nMax.rgb);
// his.a left unchanged from imageLoad
```

This patch is mandatory before Mode 4 lands.

### Algorithm — signed projection (reply 03 corrected form)

For each direction bin `(dx, dy)` of each of the 8 trilinear corners, in `sampleProbeDir`:

1. Fetch the bin's atlas value as `vec4` (was `vec3` — bandwidth ~unchanged; many drivers pad GL_RGB8 to 32-bit anyway).
2. Compute `wcos = max(0, dot(bdir, normal))` as today.
3. Compute the signed projection of the surface→probe offset onto the bin's direction:
   ```glsl
   float t = dot(surfacePos - probeCenter, bdir);
   ```
4. Compute visibility weight `wvis`:
   ```glsl
   float voxelSize = (uVolumeMax.x - uVolumeMin.x) / float(uVolumeSize.x);
   float missEps   = 0.5 * voxelSize;
   float hitDist   = a.a;

   if (hitDist < 0.0) {
       wvis = 1.0;          // sky sentinel (-1) → fully visible
   } else if (hitDist < missEps) {
       wvis = 1.0;          // miss / negligible-distance hit → fully visible
   } else {
       wvis = (t <= hitDist + missEps) ? 1.0 : 0.0;
   }
   ```
   Three geometric cases captured: `t < 0` (surface on opposite side from bdir → visible, falls through `t ≤ hitDist`), `0 ≤ t ≤ hitDist` (surface between probe and probe's hit → visible), `t > hitDist` (surface past the geometry the probe hit → occluded).
5. Accumulate `irrad += a.rgb * wcos * wvis; wsum += wcos * wvis`.

**Why this is geometrically correct in 3D** (where ShaderToy's 2D `cos(π/2 - θ)` formula isn't directly useful): `bdir` is the direction the probe's ray traveled to find geometry. The "wall plane" perpendicular to `bdir` at distance `hitDist` from the probe is a reasonable proxy for the geometry the probe saw. Surface points on the FAR side of that plane (`t > hitDist`) are geometrically occluded from receiving the same radiance the probe collected for that bin. Surfaces on the NEAR side or behind the probe receive it.

**Quality mechanism** (corrected — the "per-bin granularity" claim was loose). Adjacent corners can produce different binary `wvis` values, but `t = dot(surfacePos − probeCenter, bdir)` varies **continuously** with `surfacePos`. The per-pixel transition through the threshold `t = hitDist` is therefore gradual, and the trilinear blend across 8 corners has no sharp discontinuity. The actual mechanism eliminating banding is **per-pixel threshold continuity**, not per-bin granularity per se.

**Cone correction**: the projection assumes geometry is a flat wall perpendicular to `bdir`. Real geometry is curved/oriented arbitrarily; a more rigorous test would compare lateral distance against `tan(bin_half_angle) × hitDist`. **Filed as Phase 1.5 work** if Step 5 verification reveals under-occlusion in practice. Not pre-committed.

### Implementation surface

- **`temporal_blend.comp`**: prerequisite patch above (preserve `cur.a`, clamp `.rgb` only).
- **`raymarch.frag`**: add `sampleProbeDirDepthAware(pc, normal, D, surfacePos)`; add `uVisibilityMode == 4` branch in `sampleDirectionalGI`.
- **`demo3d.h` / `demo3d.cpp`**: extend the `visibilityMode` doc comment; add mode 4 to the ImGui combo; add CLI flag `--visibility-mode=4`. **Do not** flip the default yet.
- **No bake changes.** `radiance_3d.comp:428` already writes `vec4(rgb, hit.a)` into the atlas.

### Cost (predicted; verify in Step 5)

| Mode | Per-pixel work | vs mode 0 |
|---|---|---|
| 0 OFF | 8 corners × D² bins × vec3 fetch | baseline |
| 1 binary corner gate | + 8 SDF traces | small |
| 3 per-bin shadow trace | + 8 × D² SDF traces | large |
| **4 depth-aware (signed projection)** | 8 × D² × vec4 fetch (bandwidth ~unchanged) + 1 dot + 1 compare per bin | **expected small; verified in Step 5** |

The original "~5 ALU + length per bin" cost claim from the probe plan is superseded — signed projection needs only 1 dot product and 1 compare per bin (no `length()` per corner).

### Verification

- **Step 0 (baseline regen).** Regenerate mode-0 and mode-3 captures against current main to establish baselines. Do not compare Mode 4 against pre-Phase-1 captures — recent OBJ-load and camera changes may have shifted the visual reference.
- **Step 1.** Build clean.
- **Step 2.** Capture sequence at the cam.md viewpoint with `--visibility-mode={0, 3, 4}`. Sponza interior + Cornell closed-box.
- **Step 3.** Visual A/B: mode 4 should match mode 3 (no dot banding, no over-darkening) and be visibly better than mode 0 (no light leak through walls).
- **Step 4.** Compute decision-gate metrics (see below).
- **Step 5.** RenderDoc timing: capture mode 0 and mode 4 raymarch pass cost. Record absolute ms and relative delta. No pre-committed threshold — establish the actual ratio.
- **Step 6 (per-cascade verification, addresses critic C8).** For each cascade C0..C4, render `wvis` as the output color (instead of weighted radiance) and confirm: (a) `wvis` is not always 1 (test is doing something), (b) `wvis` is not always 0 (test isn't pathological), (c) the spatial distribution of `wvis=0` correlates with where geometry occludes probes at that cascade's spacing.
- **Step 7.** Document results in [sponza_gi_root_cause_hypothesis_test_impl.md](sponza_gi_root_cause_hypothesis_test_impl.md).

### Decision gate at end of Phase 1

**Pre-committed metric** (replaces "≈" / "looks ok"):

- **Primary**: FLIP score < 0.05 between Mode 4 capture and Mode 3 reference at cam.md viewpoint, Sponza scene.
- **Secondary**: per-region RMSE on three crops (lit floor, shadowed alcove, vertical wall column) < 0.02.
- **Failure threshold**: FLIP > 0.10 or any region RMSE > 0.05 → Mode 4 quality unacceptable.

| Outcome | Next step |
|---|---|
| Pass primary + secondary, RenderDoc cost increase < 20% | Default = mode 4. Schedule Phase 2 as the long-term correctness fix. |
| Pass primary + secondary, but bake-time leaks visible (light bleeds through walls in static scenes regardless of render mode) | Proceed to Phase 2 — only the α-gated bake merge can fix this. |
| Fail primary or secondary, before cone refinement | Investigate Phase 1.5 cone correction (`tan(half_angle) × hitDist` lateral test). One commit; rerun verification. |
| Fail primary or secondary, after cone refinement | Skip default flip; proceed to Phase 2 immediately. Signed-projection approximation is insufficient for this scene's geometry. |

(Mode 5 = Mode 4 + 1 confirmation shadow ray was removed from this gate per critic C6 — it was a deferral disguised as a plan branch. If real implementation produces a case for it, file as a separate plan.)

---

## Phase 2 — Interval atlas: α as transparency, gated bake merge

### Goal

Eliminate the entire `probeVisibility()` / `uVisibilityMode` system by making the atlas store **radiance intervals** (RGB + α-as-transparency) per the RC paper. Visibility becomes a property of the data, not a render-time test.

### Why this is architecturally correct

The RC paper (Section 1, quoted in the visibility plan): classic radiance probes need disocclusion handling *because* they encode full radiance instead of intervals. Intervals (radiance + transparency) are linearly interpolatable; full radiance is not. Our `probeVisibility()` is solving a problem the original algorithm doesn't have.

### Pre-flight task

Run `Grep uDirectionalAtlas` and produce a fetch-site enumeration table (file:line + current `.rgb` / `.rgba` usage). Phase 2's actual implementation commits split into two sub-commits (see "Recommended commit shape" below); the pre-flight table feeds both.

### Atlas format change

- `GL_RGB8` → `GL_RGBA8` for the directional atlas. **Memory cost: measure on target GPU**, do not assume "+33%". Many drivers pad GL_RGB8 to 4-byte alignment, so on-GPU footprint is often unchanged. Use `glGetTexLevelParameter` (or RenderDoc memory stats) before/after.
- Optional later: `GL_RGBA16F` for soft α (0..1) instead of binary. Not in scope for initial Phase 2.

### Bake changes (`radiance_3d.comp`)

- Per-direction loop already classifies hit/miss/sky:
  - Surface hit (`hit.a > 0.0`) → store `α = 0.0` (occluded interval).
  - In-volume miss (`hit.a == 0.0`) → store `α = 1.0` (transparent, inherits from upper).
  - Sky sentinel (`hit.a < 0.0`) → store `α = 1.0` (transparent, sky fill).
- Modify the cascade-inheritance merge at the far boundary to follow the RC paper's interval composition:

  Today:
  ```glsl
  rad = hit.rgb * l + upperDir * (1.0 - l);
  ```

  Phase 2 (paper's interval merge: `L_{a,c} = L_{a,b} + β_{a,b} * L_{b,c}`, `β_{a,c} = β_{a,b} * β_{b,c}`):
  ```glsl
  // thisRad, thisAlpha = this cascade's near interval
  // upperDir.rgb, upperDir.a = upper cascade's far interval
  rad   = thisRad + thisAlpha * upperDir.rgb;
  alpha = thisAlpha * upperDir.a;
  ```

  The current `l` smoothstep is a separate concern (it softens the cascade-handoff seam). Phase 2 sub-task: decide whether to keep `l` applied to radiance only (gating cascade-blend visibility separately) or replace it with an explicit interval-boundary handoff. Do not merge a half-decided combination — pick one before the semantic-change sub-commit lands.

### EMA + α interaction

α is **fresh-only — never EMA-blended.** Two reasons:

1. The `temporal_blend.comp` patch from Phase 1 already establishes "preserve `cur.a`" as the convention. Phase 2 just inherits it.
2. EMA on α would silently turn binary α into soft α (0..1) at hit/miss flicker boundaries. We are not opting into soft α in initial Phase 2.

(Soft α is a future refinement requiring an explicit decision; it is not in scope here.)

### Render changes (`raymarch.frag`)

- `sampleProbeDir` reads RGBA. The α channel becomes the visibility weight per bin:
  ```glsl
  vec4 a = texelFetch(uDirectionalAtlas, ...);
  float w = wcos * a.a;     // α gates the bin
  irrad += a.rgb * w;
  wsum  += w;
  ```
- `sampleDirectionalGI` blends 8 corners as before, but the trilinear blend is now safe by construction (interpolating intervals, not full radiance).
- **Mark as deprecated** (do not delete in this commit): `probeVisibility()`, `uVisibilityMode`, modes 0..4, `sampleProbeDirDepthAware`. Keep through verification so A/B comparisons remain available. Delete in a separate cleanup commit after Phase 2 verification passes.

### Verification

1. Build clean.
2. Capture at cam.md viewpoint:
   - Sponza (interior corridors — was the worst light-leak source).
   - Cornell (closed-box reference — should be unchanged).
3. Compare against Phase 1 mode 4: should match or exceed quality on FLIP/RMSE metrics from Phase 1's decision gate.
4. **Concrete bake-leak test (operationalizes critic S6):**
   - Build a closed-room test scene (Cornell with all walls opaque, lit only from the open front).
   - Capture the directional atlas at a probe deep inside an occluded region (e.g., behind the back wall) using RenderDoc's texture viewer or a debug `glReadPixels` of `uDirectionalAtlas`.
   - Inspect bins facing toward the light source. Pre-Phase-2: those bins carry nonzero RGB (cross-wall leak in the bake). Post-Phase-2: those bins should be either α=0 (occluded interval) or RGB=0 (no light propagated through occluded interval merge).
   - Quantify: sum of `bin.rgb * bin.a` across all bins of all probes in the occluded region. Pre-Phase-2 baseline > 0; post-Phase-2 ~0.
5. Atlas memory: confirm measured allocation against the perf-tooling Step 12 budget in [perf_tooling_step12_impl.md](../../5/claude_plan/perf_tooling_step12_impl.md).
6. Update [sponza_gi_root_cause_hypothesis_test_impl.md](sponza_gi_root_cause_hypothesis_test_impl.md).

### Scope estimate (revised from "~1 day")

**2–4 days**, broken down:

- Atlas format change + pre-flight grep audit: 0.5 day
- `radiance_3d.comp` α storage + interval merge (including the `l` smoothstep decision): 0.5 day
- `raymarch.frag` RGBA fetch refactor: 0.5 day
- ImGui + CLI deprecation work: 0.5 day
- Verification captures + bake-leak instrumentation: 1 day
- Buffer for surprises (atlas allocation interaction with cascade resize, EMA-related bugs, cone-correction need): 0.5–1 day

### What survives Phase 1 → Phase 2

| Phase 1 artifact | Phase 2 fate |
|---|---|
| `temporal_blend.comp` `cur.a` preservation | **Kept** (still required). |
| `sampleProbeDirDepthAware` | **Deprecated**, deleted in cleanup commit (α-gated merge subsumes it). |
| Mode 4 ImGui entry | **Deprecated**, deleted in cleanup commit. |
| `--visibility-mode=N` CLI | **Kept as no-op with deprecation warning** for one release post-Phase-2. Remove in the release after that. |
| Bake-side cascade inheritance | **Modified** — paper's multiplicative interval merge. |
| Atlas format | **Changed** — RGB → RGBA. |
| Modes 0–4 in ImGui | **Deprecated**, deleted in cleanup commit. |

---

## Recommended commit shape

- **Phase 1, commit A (prerequisite):** `[Claude] Phase 6 Step N: temporal_blend preserve hit.a, clamp rgb only` — fixes EMA/AABB α corruption. Standalone; no behavior change for current modes.
- **Phase 1, commit B:** `[Claude] Phase 6 Step N+1: depth-aware per-bin visibility (mode 4) via signed projection` — adds Mode 4. Touches `raymarch.frag`, `demo3d.h`, `demo3d.cpp`, ImGui combo, CLI parser. Adds A/B captures.
- **Phase 2, commit A (format only, functionally inert):** `[Claude] Phase 6 Step N+2: directional atlas RGB→RGBA (no semantic change)` — atlas allocation change + every fetch site audited and updated to read `.rgb` from RGBA. Should be a no-op.
- **Phase 2, commit B (semantic):** `[Claude] Phase 6 Step N+3: store α as transparency interval, paper-correct merge` — bake-side α storage + interval merge formula + render-side α-gated sampling. Marks legacy modes deprecated.
- **Phase 2, cleanup commit (after verification passes):** `[Claude] Phase 6 Step N+4: remove deprecated probeVisibility / uVisibilityMode / modes 0..4` — actual deletion. CLI flag becomes no-op stub with deprecation warning.

Sub-committing Phase 2 as A+B makes the format change rollback-able via `git revert` of commit B alone, without re-baking.

---

## Out of scope (deferred)

| Strategy | Status after this plan |
|---|---|
| Strategy 3 — Coarse SDF for visibility | Obsolete after Phase 2 (no `probeVisibility()` to accelerate). |
| Strategy 4 — Cached visibility bitmask | Obsolete after Phase 2. |
| Strategy 5 — Hierarchical skip | Obsolete after Phase 2. |
| Strategy 6 — Half-resolution visibility | **Deferred** — may matter if D grows or cascade count rises; revisit if Phase 2's per-bin α-fetch becomes bandwidth-bound. |
| Strategy 7 — Temporal amortization | Out of scope (introduces lag; not suitable for moving-camera use cases). |
| Mode 5 (mode 4 + 1 confirmation shadow ray) | Removed from decision gate. File as separate plan if real implementation produces a case for it. |
| Soft α (0..1) in Phase 2 | Future refinement requiring explicit decision; not in initial scope. |
| Phase 1.5 cone correction (`tan(half_angle) × hitDist` lateral test) | Filed; only triggered if Phase 1 verification fails the primary/secondary metric. |

---

## Open questions / risks

- **Bake-time leak materiality.** If Phase 1 looks great but the user still reports cross-wall light bleed in static scenes, Phase 2 is mandatory. Phase 1 verification protocol covers this branch in the decision gate.
- **Signed-projection adequacy in 3D.** The flat-wall-perpendicular-to-`bdir` proxy may under-occlude near curved geometry or grazing-angle walls. If Phase 1 verification fails, Phase 1.5 cone correction is the first remediation; Phase 2 escalation is the second.
- **Atlas RGBA memory at 1080p.** Logical +33%; on-GPU footprint may be unchanged. Measure on target GPU per the instructions in Phase 2 atlas-format-change section.
- **Smoothstep `l` interaction with α-gated merge.** Phase 2 sub-task; pin the answer before the semantic-change sub-commit lands.
- **Soft α temptation during Phase 2.** If the bake naturally produces soft α (e.g., partial occlusion at sub-voxel resolution), resist EMA-blending it without an explicit decision. The "α is fresh-only" rule above is the discipline gate.
