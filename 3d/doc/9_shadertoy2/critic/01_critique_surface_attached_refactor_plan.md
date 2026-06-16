# Critique — Surface-Attached ShaderToy Refactor Plan (v5)

**Source:** [doc/9_shadertoy2/surface_attached_shadertoy_refactor_plan.md](../surface_attached_shadertoy_refactor_plan.md)
**Date:** 2026-05-28
**Reviewer scope:** Algorithmic faithfulness to `shader_toy/CubeA.glsl`, contract continuity with v3 pivot locks, measurement/process discipline carried from v2.x.

This is a non-implementation critique: do not change code based on this doc alone. Items here either (a) point at a concrete spec drift between the plan and the ShaderToy reference, (b) flag a contract conflict with locked memory, or (c) demand a numerical or analytical decision the plan currently hand-waves.

---

## 0. TL;DR

The plan is broadly correct in *direction* (surface attachment is the right fix, hardcoded Cornell first is the right scope) and impressively explicit about safety rules. But it has three load-bearing technical drifts from the ShaderToy reference and two contract conflicts with locked v3 memory. None are fatal; all should be resolved before Phase 2 work begins.

| # | Severity | Theme |
|---|---|---|
| C1 | **CRITICAL** | Atlas layout drift — `dirRes` ≠ `probeSize`; the proposed `width*dirRes × height*dirRes` outer-product layout is not the ShaderToy ring-packed cascade band |
| C2 | **CRITICAL** | Persistent self-feedback ambiguity — plan does not specify whether the surface atlas is sampled by itself (ShaderToy: yes; recursive bounce closure) or re-baked transiently each frame |
| C3 | **HIGH** | Point-light direct-source treatment unspecified — no chart contains the delta point light; needs NEE or explicit sample-at-probe pass |
| H1 | **HIGH** | Contract conflict — Sponza-first-class memory ([[project_v3_pivot_locked]]) vs plan's Cornell-only-through-Phase-9 scope |
| H2 | **HIGH** | Contract conflict — strict `\|p95\|≤0.50 on both scenes` retirement bar replaced by softer `ratio ≥ 0.90` Cornell-only target |
| M1 | MED | Interval scaling constant `uSurfaceIntervalBase` is hand-wavy; ShaderToy uses a derivable value bound to chart world extent |
| M2 | MED | Phase 4 normalization formula has BRDF/π and area-per-bin combined ambiguously |
| M3 | MED | WeightedSample 3D port under-specified — ShaderToy's "flatland" cone math does not generalize cleanly to hemispherical lookback |
| M4 | MED | Measurement harness undefined — no EXR-capture procedure, no analyzer pin, no baseline JSON declared (violates "EXR or it didn't happen") |
| M5 | MED | Multi-bounce closure scheduling — Phase 4 calls for "competitive with hybrid before adding cascade complexity," but Cornell hybrid parity itself requires multi-bounce closure that the plan never schedules |
| L1 | LOW | Cornell chart table dimensions in §6.1 not bound to actual cornell scene world units |
| L2 | LOW | No stop-loss declared per phase (e.g., if Phase 4 stalls at 0.55, do what?) |
| L3 | LOW | Phase numbering vs ShaderToy fidelity — Phase 6 "WeightedSample in correct topology" is the architectural fix and arguably belongs in Phase 5, not after |
| L4 | LOW | "Diagnostics not optional" (§10) is good but the diagnostics list mixes shader debug views with CPU readback metrics without a build/wiring estimate |

---

## 1. Critical Drift from the ShaderToy Reference

### C1 — Atlas layout: `dirRes` vs `probeSize` confusion

**Plan §6.2 + Phase 2:**
> `probeSize = 2^(cascade + 1)`
> `probePositions = gRes / probeSize`
> `surfaceC0Atlas width  = surfaceAtlasWidth  * dirRes`
> `surfaceC0Atlas height = surfaceAtlasHeight * dirRes`
> `Use dirRes = 8 first.`

**Reference [shader_toy/CubeA.glsl:127-148]:**
```glsl
float probeCascade = floor(mod(UV.y, 1536.)/256.);     // 6 cascades stacked vertically per chart
float probeSize    = pow(2., probeCascade + 1.);       // C0=2, C1=4, C2=8, ..., C5=64
vec2  probePositions = gRes/probeSize;                  // C0: gRes/2 probes per chart axis
vec3  probePos = gPos + mod(modUV.x, probePositions.x)*probeSize/256.*gTan
                      + mod(modUV.y, probePositions.y)*probeSize/256.*gBit;
vec2  probeUV  = floor(modUV/probePositions) + 0.5;     // direction-bin coordinate within probe
vec2  probeRel = probeUV - probeSize*0.5;
```

In ShaderToy:
- Each cascade occupies the full chart UV rectangle (e.g. 256×256 for the floor) — **not** an outer product with a separate direction axis.
- Inside the rectangle, `(modUV.x, modUV.y)` is split into "which probe" via `floor(modUV/probePositions)` and "which direction bin within that probe" via `mod(modUV, probePositions)`. This is the inverse of what the plan describes.
- At C0, `probeSize = 2` → each probe owns a 2×2 = 4-bin square. There are `(gRes/2)²` probes per chart. There is no separate `dirRes=8` channel.
- "Direction resolution" grows with cascade index, not as a static dimension. ShaderToy's bin count per probe is `4 + 8·floor(probeThetai)` (line 146) — a **ring layout** on the square, not a uniform grid.

The plan's `width*dirRes × height*dirRes` formulation describes a 2D probe atlas with a per-probe directional sub-tile — a perfectly valid alternative, but it is **not** ShaderToy and the interpolation/merge math will diverge. If the goal is "follow ShaderToy until proven otherwise" (§12 commandment 1), this is the first place the plan must commit.

**Required decision before Phase 2:** Either (a) port the ShaderToy ring-packed cascade-band layout verbatim, with `probeSize=2` at C0 and ring direction enumeration, or (b) explicitly declare a divergence with rationale and prove the divergent layout still merges correctly. Mixing the two — taking ShaderToy's `probeSize` semantics but applying them to a `dirRes`-scaled outer-product atlas — will silently break the merge.

**Likely consequence if shipped as written:** the bilinear merge in Phase 5/6 will read texels that correspond to a different probe than intended. Numerical symptoms will look like "leaks across charts" or "upper cascade is darker than C0" — exactly the kind of bug that v2.x sweeps could not isolate because the merge index was wrong from the start.

### C2 — Persistent self-feedback (recursive bounce) is not scheduled

**Reference [shader_toy/CubeA.glsl:44-45, 165-180]:**
```glsl
vec4 Output = texture(iChannel3, rayDir);   // iChannel3 = this same buffer last frame
...
if (rayHit.c.x >= -1.5) {                   // Geo hit
    vec2 suv = clamp(rayHit.uv*128., ...) + rayHit.uvo;
    Output.xyz = TextureCube(suv, 0.).xyz +              // ← reads SELF (atlas of hit-chart)
                 TextureCube(suv + vec2(rayHit.res.x*0.5, 0.), 0.).xyz +
                 TextureCube(suv + vec2(0., rayHit.res.y*0.5), 0.).xyz +
                 TextureCube(suv + rayHit.res*0.5, 0.).xyz;
    // sunlight, color modulation, etc.
}
```

ShaderToy gets unlimited bounces "for free" because the atlas is a persistent buffer that samples itself each frame. The cascade levels above C0 also recursively read C0 via the merge in lines 207-216. Multi-bounce closure is structural, not a separate algorithmic step.

**Plan Phase 2 step 5-7:**
> 5. If hit surface: compute direct-lit outgoing radiance at hit.
> 6. If emissive/light hit: write emission/direct light.
> 7. If miss: write sky/env if enabled.

This is **direct-only C0**. The plan never explicitly schedules the "sample previous-frame surface atlas at the hit-point chart" step. Phase 4 ("hybrid parity") describes diagnostic-driven tuning of the direct term, not the addition of recursive feedback. Phase 5 adds upper-cascade merge but only between cascades of the current bake (the plan's prose suggests within-frame coarse-to-fine, not frame-to-frame).

This is the same axis that bit volumetric: memory [[project_v3_m1_stage11d_multi_bounce_under_emit]] says "cascade under-counts MULTI-BOUNCE energy under enclosed geometry; ratio jumps discretely at box-boundary." Cornell is multi-bounce-dominated; without the recursive feedback Cornell point-light surface RC will likely cap somewhere around 0.65-0.75 (direct + one explicit hemisphere bounce per probe), not the 0.83 hybrid bar and certainly not the 0.90 target.

**Required decision before Phase 2:**
1. Is the C0 atlas a **persistent** GPU texture sampled by itself each frame (ShaderToy parity), or a **transient** atlas freshly baked each frame?
2. If persistent: when does the recursive `TextureCube(suv, 0.).xyz`-equivalent get added? Phase 2 stage 5, or a new sub-phase 2.5 "self-feedback closure"?
3. If transient: how is multi-bounce achieved at all? An N-pass bake within a frame is computationally similar to the persistent case but loses convergence stability.

A persistent atlas changes the semantics of "first metric gate" too — single-frame ratio at Phase 3 is meaningless if the atlas needs N frames to converge. The plan needs a "let it converge then sample" rule analogous to MBRC's `N=2048` convention.

### C3 — Point light has no chart; direct-source sampling unspecified

ShaderToy's only emitter is a directional sun (`sunDir`) sampled by an explicit dot+shadow-trace at every probe-ray hit ([CubeA.glsl:172-177]). The Cornell point light is a delta-position source that lives **inside** the scene but is not part of any chart.

**Plan Phase 2 step 6:** "If emissive/light hit: write emission/direct light."

This works only if the point light is modeled as a small emissive sphere on the ceiling chart and the bake relies on probe rays *probabilistically* hitting that emissive region. But that's the same "probability of hitting the bright source" failure mode that doomed the volumetric path (see [doc/8_shadertoy/cornell_point_light_constraint.md] §2). Surface attachment fixes the *receiver* side; it does **not** automatically fix the source side.

For the bright-floor-patch under-emit specifically: surface attachment *does* help because the floor probe is **on** the bright patch and its bin towards the ceiling will sample the direct light at the ceiling material — but only if the bake explicitly evaluates point-light direct at the hit point (next-event estimation), not relying on the bin happening to land on a small emissive sphere.

**Required spec addition (Phase 2):**
- At each probe-ray surface hit, evaluate the point light directly: `Li_direct = pointLightColor * shadowTrace(hitPos -> lightPos) / d²`, multiplied by `max(0, dot(hitNormal, lightDir))`. Add this to the bin's outgoing-radiance write before any color/material modulation.
- Equivalent of CubeA.glsl:172-177 generalized from `sunDir` to `pointLightDir = normalize(lightPos - sPos)`.

Without this step, surface RC's first-bounce term will see only what's already baked into the atlas (zero on frame 0), and the recursive closure (C2 above) will take many frames to build up — or stall if the atlas is transient.

---

## 2. Contract Conflicts with Locked v3 Memory

### H1 — Sponza first-class vs Cornell-only

Memory [[project_v3_pivot_locked]] (locked 2026-05-26):
> "Sponza scope: First-class from M0. Every milestone (M0 baseline, M1 per-delta gates, M1 cumulative gate, M2, M3) evaluates **both** cornell/cam0 AND sponza/atrium. Sponza visual regression vetoes any delta regardless of cornell numbers."

Plan §4 rule 4 + Phase 9:
> "Cornell-only is acceptable and preferred at first."
> "Generalization After Cornell Success."

These are direct conflicts. Two possible resolutions:

1. **The v3 lock applies only to volumetric-path Delta work.** Surface RC is a new code path; the volumetric path remains unchanged and Sponza continues to use it. Under this reading, Cornell-only surface RC does not regress Sponza, so the lock is satisfied trivially.
2. **The v3 lock applies to all hybrid-retirement claims.** Then "Phase 8 — Replace Cornell Default Path" (with explicit hybrid retirement language at Phase 9's outcome) cannot ship without Sponza coverage.

The plan's §15 "Final Desired Outcome" says "Hybrid correction becomes optional/fallback instead of required," which is hybrid retirement. Under reading 2, that conflicts with the lock until Phase 9 generalizes.

**Required clarification before Phase 0:** Pick a reading. If reading 1, add an explicit note to §4 ("v3 Sponza-first-class lock does not apply because volumetric path stays default for Sponza until Phase 9"). If reading 2, push Phase 8 hybrid-retirement gating until Sponza coverage exists.

### H2 — Retirement bar drift

Memory [[project_v3_pivot_locked]]:
> "Hybrid retirement criterion: Strict — retire only if pivot achieves **\|p95\| ≤ 0.50 on both scenes**. No wider-band fallback. ... strict retirement protects against the same trap that produced v2.4.b — using a relaxed verdict band to declare success without architectural justification."

Plan Phase 8 + §9:
> "Cornell point-light surface RC ratio >= 0.90 target or clearly better than hybrid with acceptable visuals."

Different metric (ratio vs |p95|), different magnitude (0.90 ≥ unspecified), different scope (Cornell-only vs both scenes), and explicit escape hatch ("clearly better than hybrid with acceptable visuals" — exactly the kind of soft band the lock forbids).

**Required clarification before Phase 8 gate definition:**
- Restate the locked |p95| bar and confirm whether it's still in force.
- If the surface-RC pivot is allowed to declare success on ratio alone, document why the bake-side topology change justifies suspending the |p95| lock that the volumetric program was held to.
- "Acceptable visuals" is the language v2.4.b used. Either define it numerically (max RMS delta vs PT in mode-0 composite?) or strike it.

---

## 3. Medium-Severity Issues

### M1 — `uSurfaceIntervalBase` is hand-wavy

Plan §6.4:
```glsl
float tInterval = uSurfaceIntervalBase * probeSize;
```
> "Start with a tuned constant, but keep it explicit."

ShaderToy [CubeA.glsl:151]:
```glsl
float tInterval = (1./64.)*probeSize*2.;       // probeSize/32 world units
if (probeCascade > 4.5) tInterval = 10000.;
```

ShaderToy's gRes=256 with unit-cube charts gives world spacing per chart pixel of `1/256`. The interval at C0 is then `1/16` world units (≈ 4× per-probe spacing of `2/256 = 1/128`). The ratio `interval / probe_spacing = 8` is the cone-overlap rule that makes the merge tile, not a free parameter.

**Action:** derive `uSurfaceIntervalBase` from Cornell chart resolution and world bounds before tuning. Otherwise this is the v2.x "knob without theory" trap.

Per [[feedback_analytical_doc_qa]] direction-sign check: compute the planned `tInterval` at C0 numerically against ShaderToy's `1/16` and confirm magnitudes match (or document the rationale for divergence).

### M2 — Hemisphere normalization formula ambiguity

Plan Phase 4:
> `Lo ≈ albedo * Σ Li(dir) * cos(theta) * Δω / π`
> Or ShaderToy-equivalent:
> `Output *= hemisphere_area_per_bin`
> `Output *= cos(theta)`
> `Output *= albedo / π or matched convention`

ShaderToy [CubeA.glsl:190-192]:
```glsl
Output.xyz *= (cos(probeTheta - 3.141592653/probeSize) -
               cos(probeTheta + 3.141592653/probeSize))/(4. + 8.*floor(probeThetai));
Output.xyz *= cos(probeTheta);   // Diffuse
```

The first factor is **per-bin solid angle on the hemisphere ring** (a finite-difference of cos), divided by the **number of bins in that ring**. The cos(theta) is the Lambertian cosine. Notably, **there is no `/π` and no `albedo` factor at this point** — the albedo multiplication happens earlier at line 180 inside the geo-hit branch (`Output.xyz *= rayHit.c`), and the `/π` is folded into the consumer (the final render reads the C0 atlas as already-normalized).

Plan's two formulations are not equivalent (one has `/π`, the other doesn't). Either consumer-side or bake-side must absorb the `/π`, never both, never neither. Pick one and document.

**Action:** specify exact normalization with reference to where each factor lives (bake vs consume), matched to ShaderToy line numbers.

### M3 — WeightedSample 3D port under-specified

Plan Phase 6:
```text
upperProbePos = chartToWorld(upper chart, upper UV)
currentProbePos = chartToWorld(current chart, current UV)
relVec = currentProbePos - upperProbePos
lookBackDir = direction from upper probe toward current probe
lookBackBin = hemisphere/bin coordinate of lookBackDir in upper chart TBN
```

ShaderToy WeightedSample [CubeA.glsl:21-42] is explicitly 2D in chart space — `phi = atan(-dot(relVec, gTan), -dot(relVec, gBit))`, then a 1D ring index. This works in ShaderToy because both probes live on the **same chart** (`gTan, gBit` shared) and `relVec` is co-planar with the chart.

The plan's port crosses charts (current and upper "lookback" could be on different charts) and uses TBN for the upper chart only. Three problems:

1. If both probes are on the same chart, the 3D path reduces to the 2D ShaderToy path — fine.
2. If they're on different charts, `lookBackDir` is genuinely 3D. The upper chart's bin atlas is hemispherical and parameterized by 2D ring coordinates within its own tangent frame; the projection of `lookBackDir` into that frame may have a non-hemispherical (downward) component, which has **no defined bin**.
3. The "cone" math `theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*pi*0.5` is also chart-local; in 3D it should be defined against the upper chart's normal, but the relative geometry between two charts is not constant.

In ShaderToy, the merge never crosses charts because each chart's cascade band is self-contained — the "upper cascade UV" computation [CubeA.glsl:202] uses `floor(UV/gRes)*gRes + vec2(0., gRes.y)`, which stays in the same chart-column. The plan's Phase 6 description implies cross-chart merging that ShaderToy does not actually do.

**Action:** clarify whether Phase 5 merge is chart-local (matches ShaderToy) or cross-chart (a divergence requiring its own derivation). If chart-local, Phase 6 simplifies to a near-verbatim ShaderToy port and most of the 3D-vector verbiage drops out.

### M4 — Measurement harness undefined

Plan §10 lists diagnostics but no measurement protocol. Memory contract:
- [[project_mbrc_correction_failed_pivot_shadertoy]] "No more LDR-only verdicts — EXR or it didn't happen."
- [[feedback_measurement_before_features]] "measurement report ships as standalone signed-off deliverable before feature code."
- [[project_v3_pivot_locked]] specifies `tools/v3_baseline/baseline_lock.json` with cornell + sponza baselines.

The plan never specifies:
- Which tool captures the EXR (the existing `tools/v20_convergence/` chain, or new?).
- Which N-sample PT reference is the comparison target.
- Which JSON file pins the metric for `surface_c0_gi / pt_gi` claims.
- Whether the "ratio" computed in Phase 3-6 gates is the same ratio (mean ratio) computed by `tools/analysis/` for v4.

Without this, Phase 3 cannot ship a defensible "0.60 gate passed" verdict.

**Action:** add §10.1 "Measurement protocol":
- Reuse `tools/v20_convergence/` or commit a fork; name the script.
- Pin the PT reference capture (path + SHA256) for each gate.
- Pin the EXR sample count (N=2048 by convention).
- Add a "baseline_lock_surface_rc.json" sibling to v3/v4 baselines.

### M5 — Multi-bounce closure scheduling

Phase 4 demands "Cornell point-light surface/PT ratio ≥ 0.80," equal to hybrid. Hybrid achieves 0.83 via per-pixel MC bounce-1 (one explicit Monte-Carlo bounce). Surface RC C0 with only direct lighting at hit-point cannot reach 0.83 — it has direct + (cosine-weighted-hemisphere-sample of) direct, i.e. effectively bounce-1, only with deterministic hemisphere sampling instead of stochastic.

That's marginally close enough to bounce-1 hybrid that maybe Phase 4 can match it — but only if (C3) NEE for the point light is in place. Without NEE, the C0 hit-point direct term will be zero (no atlas history to read), so the bake produces no light at all on frame 1.

**Action:** make the Phase 2 → 4 ordering explicit:
- Phase 2: probe rays, hit-point material query, **NEE for point light at hit**, hemisphere normalization. Produces single-frame direct + bounce-1 = ~hybrid quality.
- Phase 3: consume in raymarch.frag, prove integration works.
- Phase 4: tune the same axes that hybrid tunes (shadow bias, fall-off, material albedo match).
- Phase 5+: cascade hierarchy and merge unlock multi-bounce closure (via the recursive C0 sample, which is the missing C2 piece).

Without restating this, Phase 4's "≥ 0.80" gate is unreachable and the plan will stall there.

---

## 4. Low-Severity / Process

### L1 — Cornell chart dimensions in §6.1 are not numerically bound

Chart table lists "256×256," "128×256" without referencing actual Cornell scene units (the in-repo cornell.obj has specific extents). Verify before writing surface_cornell_debug.comp. Otherwise the analytic chart-classification in Phase 3 (`near floor plane`, etc.) will offset the UV by an unknown amount.

### L2 — No stop-loss per phase

Each phase has an "Acceptance" and "Fail action" but no time/iteration limit. v2.x sank 31 commits across hypotheses that all failed; the v4 closeout explicitly cited "opportunity cost > marginal win ceiling" as a learning. Add per-phase "if not converged within N implementation attempts, escalate" rules. Suggestion: hard cap of 2 implementation attempts + 1 diagnostic round per phase before escalation to plan revision.

### L3 — Phase 5 vs Phase 6 ordering

"Phase 5 — Add Surface Cascade Levels" with "simple surface bilinear upper merge" first, then "Phase 6 — Port ShaderToy WeightedSample in Correct Topology." If ShaderToy fidelity is the goal (commandment 1), Phase 5's "simple bilinear" detour is a divergence the plan otherwise forbids. Consider collapsing to a single phase: Phase 5b = ShaderToy WeightedSample, with simple bilinear as a temporary debug fallback only.

### L4 — Diagnostics list mixes shader and CPU work without effort estimates

§10 lists 14 diagnostic outputs. Roughly half are shader debug views (cheap, can reuse existing debug-mode infrastructure) and half are CPU readbacks (expensive, need GL buffer roundtrip + analyzer hookup). Group them and estimate each. The lesson from [[feedback_measurement_before_features]] is that diagnostics work is the bulk of the actual schedule.

---

## 5. Process Risks Beyond the Plan

These are not faults of the plan, but it should acknowledge them:

1. **The Stage 11d mechanism is multi-bounce, not just bright-patch-miss.** The plan frames §1 entirely around the "probabilistic sampling miss" mechanism. That's *a* mechanism; memory [[project_v3_m1_stage11d_multi_bounce_under_emit]] identifies multi-bounce under-counting as a separate axis with discrete ratio jumps at box boundary. Surface attachment fixes the receiver-side miss; only the recursive atlas (C2 above) fixes the multi-bounce under-count. If only the first is implemented, Cornell ratio will stop short of 0.90.

2. **"Match ShaderToy first; generalize later" presumes ShaderToy is correct for Cornell.** ShaderToy was designed for an open-ish cube with a sky+sun directional setup, not a closed Cornell box with a point light. Some of its parameters (interval scaling, `1./64.` constant, ring bin layout sized for 6 cascades) may need calibration even in the "verbatim port" phase. Phase 2 acceptance "no NaN/Inf" is not enough — add "ratios per chart match expected analytic for a known geometry" before declaring chart bake sane.

3. **"Volumetric path remains as fallback" creates two-path maintenance burden the v4 closeout already complained about.** Phase 8 keeps both paths "selected at runtime" — that's three paths total counting hybrid. Schedule the volumetric-path deletion explicitly post-Phase-9, or set a sunset date.

---

## 6. Pre-Phase-0 Required Actions

Before Phase 0 implementation begins, address:

| ID | Action | Where |
|---|---|---|
| C1 | Commit to ring-packed cascade-band layout OR document divergence | New section "§5.x Atlas Layout — Detailed" |
| C2 | Declare persistent vs transient atlas; if persistent, schedule recursive-feedback step | New Phase 2.5 or amend Phase 2 |
| C3 | Specify NEE for point light at probe-ray hit | Amend Phase 2 step 5-6 |
| H1 | Resolve Sponza-first-class conflict | Amend §4 safety rules |
| H2 | Restate or relax \|p95\| ≤ 0.50 retirement bar with rationale | Amend Phase 8 + §9 |
| M4 | Specify measurement harness (script, baseline JSON, PT reference SHA) | New §10.1 |

The remaining items (M1, M2, M3, M5, L1-L4) can be resolved as the corresponding phase is implemented, provided they're acknowledged as open before that phase starts.

---

## 7. What's Good

To balance: the plan does several things right that earlier programs got wrong:

- **Per-phase metric gates** (Phase 3 ratio>0.60, Phase 4 ≥0.80, etc.) — directly addresses [[feedback_measurement_before_features]].
- **No deletion before replacement passes** (§4 rule 7) — addresses the v2.x impulse to "improve" the working path.
- **Diagnostics declared non-optional** (§10 closing line) — addresses [[project_v3_m1_stage11b_bake_under_emits]] tuning-blind risk.
- **"Match ShaderToy first; generalize later"** (§12 commandment 1) — directly applies the [[feedback_theoretical_fix_over_measurement]] learning that algorithmic diff against the reference is the winning move.
- **Phase 0 has zero behavior change** — the v2.x default-flip incidents argue for exactly this kind of safe flag plumbing first.
- **§11 "Most Likely Failure Sources"** with prioritized list — operationalizes the failure-mode triage that v2.x lacked.

The structural shape of the plan is sound. The drift items above are all in the technical detail layer, not the strategic layer.

---

## 8. Cross-References

**Plan under critique:**
- [doc/9_shadertoy2/surface_attached_shadertoy_refactor_plan.md](../surface_attached_shadertoy_refactor_plan.md)

**Reference algorithm:**
- [shader_toy/CubeA.glsl](../../../shader_toy/CubeA.glsl) — primary spec
- [shader_toy/Common.glsl](../../../shader_toy/Common.glsl) — helpers

**Predecessor program closeout:**
- [doc/8_shadertoy/v4_closeout_report.md](../../8_shadertoy/v4_closeout_report.md)
- [doc/8_shadertoy/cornell_point_light_constraint.md](../../8_shadertoy/cornell_point_light_constraint.md)

**Locked memory (treat as binding):**
- `project_v3_pivot_locked` — strict |p95|≤0.50 both scenes, Sponza first-class
- `project_mbrc_correction_failed_pivot_shadertoy` — v2.x DNRs, EXR-or-it-didn't-happen
- `project_v3_m1_stage11d_multi_bounce_under_emit` — multi-bounce mechanism
- `feedback_measurement_before_features` — measurement deliverable
- `feedback_theoretical_fix_over_measurement` — diff-against-reference primacy
- `feedback_analytical_doc_qa` — direction-sign/derivative-sign checks
- `feedback_cascade_merge_is_bake_time` — merge changes go in bake shaders, not consumer
