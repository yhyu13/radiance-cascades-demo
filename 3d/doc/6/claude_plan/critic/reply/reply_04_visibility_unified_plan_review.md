## Reply: Unified Visibility Plan Critic 04 — `04_visibility_unified_plan_review.md`

**Date:** 2026-05-12
**Status:** All 18 findings accepted. **Plus a meta-finding the critic missed: the unified plan replicated the buggy Mode 4 algorithm from the original probe plan without applying the corrections from [reply_03](reply_03_probe_visibility_acceleration_plan_review.md).** That is the single most important fix and propagates through C1–C3 and C7. Plan updated in-place — see "Doc updates applied" at the end.

---

### Meta-finding (M1) — Unified plan ignored reply_03's corrections

The unified plan's Phase 1 algorithm copied the original probe plan's `cosCone` formulation (`distSP < a.a*cosCone + ε`) verbatim. Reply 03 had already replaced that with a **signed-projection** algorithm (`t = dot(surfacePos − probeCenter, bdir); wvis = (t ≤ hitDist + missEps) ? 1 : 0`) and explicitly retired the cone-angle scalar in the basic form. The unified plan also missed the temporal_blend.comp EMA patch that reply 03 documents as a Mode-4 prerequisite.

**Action:** rewrite Phase 1's algorithm section and verification protocol to match reply 03. This subsumes critic findings C3 (cone derivation) and a chunk of C7 (cost claim) — there is no `cosCone` to derive, and the per-bin ALU drops from "~5 ops + length()" to "1 dot + 1 compare".

---

### C1 (HIGH) — Phase 2 α-merge formula is wrong

Accepted. The plan's `mix(thisAlpha, upperDir.a, 1-l)` is a linear blend; the paper's `β_{a,c} = β_{a,b} * β_{b,c}` is multiplicative. With `near=0`, `far=1`, the formulas disagree at the boundary that matters most (occluded near interval blocking transparent far interval).

**Plan revision:** replace the snippet with the paper's interval merge:

```glsl
// Paper interval merge: L_{a,c} = L_{a,b} + β_{a,b} * L_{b,c};  β_{a,c} = β_{a,b} * β_{b,c}
//   β = transparency (1 = fully transmitting, 0 = fully blocking)
//   thisAlpha = α this cascade's bake measured for this bin (1 if miss/sky, 0 if hit)
//   upperDir.a = α inherited from upper cascade for this bin
rad   = thisRad + thisAlpha * upperDir.rgb;
alpha = thisAlpha * upperDir.a;
```

The current `l` smoothstep is a separate concern (a hack for cascade-handoff seam softening). Phase 2 needs to either keep the smoothstep applied **only to radiance** (gating cascade-blend visibility separately) or replace it with an explicit interval-boundary handoff. Filed as a Phase 2 sub-task; updated plan calls it out.

---

### C2 (MEDIUM) — "Eliminates banding by construction" mechanism stated wrongly

Accepted. The correct mechanism is **per-pixel threshold continuity**: `t = dot(surfacePos − probeCenter, bdir)` varies continuously with `surfacePos`, so even when adjacent corners produce different binary `wvis` values, the per-pixel transition through the threshold is gradual.

**Plan revision:** restate Mode 4's quality claim as: "Per-bin visibility test with a per-pixel-continuous threshold variable. Adjacent corners can produce different binary visibility decisions, but the per-pixel `t` transitions smoothly across cell boundaries, so the trilinear blend has no sharp discontinuity. (This is the actual mechanism — 'per-bin granularity' is the data structure, not the reason banding disappears.)"

---

### C3 (MEDIUM) — `cosCone` not derived

Superseded by M1. Reply 03 dropped `cosCone` from the basic algorithm. The "future quality refinement" path (cone correction via `tan(half_angle) × hitDist` lateral comparison) is filed as Phase 1.5 work if the basic projection test under-occludes.

---

### C4 (HIGH) — EMA + α interaction in Phase 2

Accepted. Two separate patches needed:

1. **Already documented in reply 03 for Phase 1**: `temporal_blend.comp` must preserve `cur.a` instead of EMA-blending. Without this, Mode 4 reads garbage hit distances regardless of phase. Plan-revision note added: this patch is a hard prerequisite for Phase 1, not just Phase 2.

2. **New for Phase 2**: when α becomes the transparency channel (instead of just hit distance carried along), the same "fresh α, blended RGB" pattern applies. `radiance_3d.comp:428` already does this. The temporal_blend patch from (1) covers it. **Decision: keep α binary by writing fresh α each frame (no EMA on α).** Soft α (a future refinement) would require an explicit decision to EMA-blend α, which we are not making in Phase 2.

Plan revision: explicit "α is fresh-only; never EMA-blended" line added to Phase 2.

---

### C5 (LOW) — "+33% memory" misleading

Accepted. Replaced with: "Atlas changes from `GL_RGB8` to `GL_RGBA8`. Many drivers pad GL_RGB8 to 4-byte alignment, so on-GPU footprint is often unchanged. Measure with `glGetTexLevelParameter(GL_TEXTURE_3D, 0, GL_TEXTURE_INTERNAL_FORMAT)` and `glGetTexLevelParameter(..., GL_TEXTURE_COMPRESSED_IMAGE_SIZE)` (or equivalent) before and after on the target GPU; do not budget against a synthetic 33%."

---

### C6 (MEDIUM) — Mode 5 fallback is a deferral

Accepted. **Removed Mode 5 from the decision gate.** New gate outcome for "Mode 4 mostly works but specific surfaces misbehave": "Proceed to Phase 2 — α-gated interval merge handles cases the projection approximation misses, no need for an interim Mode 5."

If real implementation reveals a need for the cone-correction refinement (C3's "future quality refinement"), that becomes a Phase 1.5 commit before Phase 2. But it is not pre-committed in the plan.

---

### C7 (MEDIUM) — "~1.05×" cost claim is a guess

Accepted. **Cost table revised:**

| Mode | Per-pixel work | vs mode 0 |
|---|---|---|
| 0 OFF | 8 corners × D² bins × vec3 fetch | baseline |
| 1 binary corner gate | + 8 SDF traces | small |
| 3 per-bin shadow trace | + 8 × D² SDF traces | large |
| **4 depth-aware (corrected)** | 8 × D² × vec4 fetch (bandwidth ~unchanged) + 1 dot + 1 compare per bin | **expected small; verify in Step 5** |

The "1 dot + 1 compare" per bin (revised from "~5 ALU + length") is a real reduction — reply 03's signed-projection algorithm is leaner than the original cone formulation.

---

### C8 (MEDIUM) — Cross-cascade behavior unanalyzed

Accepted. The signed-projection algorithm is actually more cascade-robust than the cone formulation it replaced — `t < hitDist` scales naturally with cascade probe spacing because both `t` (surface-to-probe projection) and `hitDist` (probe ray length) scale together. But the claim deserves verification.

**Plan revision (new Phase 1 verification step):** "Step 6: capture per-cascade visibility heatmaps. For each cascade C0..C4, render `wvis` as the output color (instead of weighted radiance) and confirm: (a) `wvis` is not always 1 (test is doing something), (b) `wvis` is not always 0 (test isn't pathological), (c) the spatial distribution of `wvis=0` correlates with where geometry occludes probes at that cascade's spacing."

---

### S1 (LOW) — "~1 day" Phase 2 estimate

Accepted. Revised estimate: **2–4 days**, broken down as:

- Atlas format change + pre-flight grep audit: 0.5 day
- `radiance_3d.comp` α storage + interval merge: 0.5 day
- `raymarch.frag` RGBA fetch refactor: 0.5 day
- ImGui + CLI cleanup with deprecation grace (S4): 0.5 day
- Verification captures + per-cascade heatmaps + atlas-content instrumentation (S6): 1 day
- Buffer for surprises (atlas allocation interaction with cascade resize, EMA-related bugs, cone-correction need): 0.5–1 day

---

### S2 (LOW) — Delete-modes-when ambiguity

Accepted. **Pick: keep modes 1/2/3 through validation, delete in a separate cleanup commit after Phase 2 verification passes.** Internal inconsistency removed by editing the "Render changes" section to say "**Mark as deprecated**: probeVisibility(), uVisibilityMode, modes 0..4. Delete in cleanup commit after Phase 2 verification passes."

---

### S3 (LOW) — Atlas format change rollback

Accepted. **Plan revision adds a pre-flight task:** "Run `Grep uDirectionalAtlas` and produce a fetch-site enumeration table (file:line + current `.rgb`/`.rgba` usage). Phase 2 commit splits into two sub-commits: (a) format change only (RGB→RGBA, all sites read `.rgb` from RGBA — should be a no-op functionally), (b) semantic change (use `.a` as transparency in merge + sample paths). Rollback is now `git revert` of (b) without re-baking, since (a) is functionally inert."

---

### S4 (LOW) — CLI/scripting breaking change

Accepted. **Plan revision:** "Keep `--visibility-mode=N` flag for one release post-Phase-2 as a no-op with deprecation warning printed at startup. Remove in the release after that."

---

### S5 (LOW) — No baseline-regen plan

Accepted. **Added Phase 1 verification Step 0**: "Regenerate mode-0 and mode-3 captures against current main to establish baselines. Do not compare Mode 4 against pre-Phase-1 captures — recent OBJ-load and camera changes may have shifted the visual reference."

---

### S6 (MEDIUM) — Bake-leak verification not operationalizable

Accepted. **Concrete instrumentation proposal added to Phase 2 verification:**

1. Build a closed-room test scene (Cornell with all walls opaque, lit only from the open front).
2. Capture the directional atlas at a probe deep inside an occluded region (e.g., behind the back wall) using RenderDoc's texture viewer or a debug `glReadPixels` of `uDirectionalAtlas`.
3. Inspect bins facing toward the light source. Pre-Phase-2: those bins carry nonzero RGB (cross-wall leak in the bake). Post-Phase-2: those bins should be either α=0 (occluded interval) or RGB=0 (no light propagated through occluded interval merge).
4. Quantify: sum of `bin.rgb * bin.a` across all bins of all probes in the occluded region. Pre-Phase-2 baseline should be > 0; post-Phase-2 should be ~0.

This is a proper operational test instead of the vague "zero out probeVisibility-equivalent paths" sentence.

---

### S7 (LOW) — "Phase 1 informs Phase 2" claim weakly supported

Accepted. The two phases are sequenceable but not informationally connected — Phase 2 deletes Mode 4 and uses a different formula. **Reframed in plan as:** "Phase 1 ships a quality fix immediately and validates that `hit.a` semantics work in production (the data is fresh after the temporal_blend patch, the per-pixel test produces continuous output). Phase 2's correctness is justified independently of Phase 1's outcome."

---

### E1 (LOW) — Plan asserts what it should verify

Accepted. Hedged "by construction", "guaranteed", "matches mode 3" → "expected to ___; verified in Step N".

---

### E2 (LOW) — "Out of scope" too aggressive

Accepted partially. **Half-resolution visibility (Strategy 6)** moved from "obsolete" to "deferred — may matter if D grows or cascade count rises". The other strategies (3, 4, 5, 7) remain genuinely obsolete after Phase 2 because they accelerate `probeVisibility()` which Phase 2 deletes.

---

### E3 (MEDIUM) — Decision gate has no quality metric

Accepted. **Pre-committed metric added to plan:**

- **Primary**: FLIP score < 0.05 between Mode 4 capture and Mode 3 reference at cam.md viewpoint, Sponza scene.
- **Secondary**: per-region RMSE on three crops (lit floor, shadowed alcove, vertical wall column) < 0.02.
- **Failure threshold**: FLIP > 0.10 or any region RMSE > 0.05 → Mode 4 quality unacceptable, skip default flip, escalate to Phase 2.

Captures stored alongside [sponza_gi_root_cause_hypothesis_test_impl.md](../../sponza_gi_root_cause_hypothesis_test_impl.md).

---

## Doc updates applied

Updates landing in `visibility_unified_plan.md` in the same commit as this reply:

- **Phase 1 algorithm**: replaced `cosCone` formulation with reply_03's signed-projection algorithm (`t = dot(surfacePos − probeCenter, bdir); wvis = (t ≤ hitDist + missEps) ? 1 : 0`). Removed `cosCone` derivation discussion.
- **Phase 1 prerequisite**: added explicit "temporal_blend.comp must preserve `cur.a`" prerequisite, citing reply_03 F3.
- **Phase 1 verification**: added Step 0 (baseline regen), Step 6 (per-cascade heatmaps), pinned FLIP/RMSE metric for the decision gate.
- **Phase 1 quality claim**: restated mechanism as "per-pixel threshold continuity" instead of "per-bin granularity".
- **Phase 1 cost table**: revised to "expected small; verify in Step 5", dropped "~1.05×" specific number, updated per-bin ALU to "1 dot + 1 compare".
- **Phase 1 decision gate**: removed Mode 5 fallback; "specific surfaces misbehave" branch now points to Phase 2 directly.
- **Phase 2 α-merge formula**: replaced linear `mix` with paper's multiplicative `α_a * α_b` and additive-with-β-weighting radiance.
- **Phase 2 EMA**: explicit "α is fresh-only; never EMA-blended" line.
- **Phase 2 atlas memory**: replaced "+33%" claim with "measure on target GPU" instruction.
- **Phase 2 verification**: added concrete bake-leak test (closed-room scene, atlas inspection, quantitative pre/post comparison).
- **Phase 2 scope**: re-estimated to 2–4 days with breakdown.
- **Phase 2 commit shape**: split into format-change-only sub-commit + semantic-change sub-commit for granular rollback.
- **Phase 2 mode deletion**: deferred to a separate cleanup commit after verification passes (resolves S2 ambiguity).
- **CLI deprecation**: `--visibility-mode=N` kept as a no-op with deprecation warning for one release post-Phase-2.
- **"Phase 1 informs Phase 2" framing**: replaced with the more honest "Phase 1 ships immediately; Phase 2's correctness is independent" framing.
- **Out of scope**: moved Strategy 6 (half-resolution) from "obsolete" to "deferred"; updated rationale.
- **Hedged language** throughout: "by construction" → "expected", "matches mode 3" → "should match mode 3 per Step 5 verification".

Items NOT updated in the plan (filed as future work or rejected):

- C3's cone-correction refinement: filed as Phase 1.5 if signed-projection under-occludes; not pre-committed.
- E2's strategies 3/4/5/7: kept as "obsolete after Phase 2" — they accelerate code Phase 2 deletes.

---

## Summary

The critic's biggest contribution was forcing a re-derivation that I should have done myself before writing the plan: the signed-projection algorithm from reply_03 is the corrected basis, not the cone formulation. Once that fix propagates, several smaller findings (C3, half of C7) collapse out automatically. The remaining findings are mostly editorial discipline (hedge claims, pin metrics, plan rollback) plus two genuine bugs (C1's α-merge formula, C4's EMA-on-α corruption) that would have shipped silent correctness errors.

Net change to the plan: **stronger Phase 1 algorithm, correct Phase 2 merge formula, verifiable decision gate, honest scope estimate, granular rollback path.** Phasing is unchanged — Mode 4 first as a render-only quick win, interval atlas second as the architectural endpoint.
