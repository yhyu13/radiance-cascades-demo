# Plan: Phase 1.5 (Cone Correction) + Phase 2 (Interval Atlas)

**Date:** 2026-05-14
**Predecessor:** [visibility_unified_plan_phase1_decision_gate.md](visibility_unified_plan_phase1_decision_gate.md) — Mode 4 lands but fails secondary RMSE in lit_floor (0.030 > 0.02) and fails the implicit ~10% raymarch-cost target (+50%). User asked: dump both proposed paths into a single plan.

**Critic chain:** [critic 07](critic/07_visibility_phase1.5_and_phase2_plan_review.md) (19 findings) → revision 1; [critic 08](critic/08_visibility_phase1.5_and_phase2_plan_rev1_review.md) (8 new findings + 2 structural concerns) → revision 2; **[Phase 2 pre-flight audit](visibility_phase2_preflight_audit.md) → revision 3 (this version)** — found that the atlas was always `GL_RGBA16F`, not `GL_RGB8`. Phase 2A is empty; 2B breaks Mode 4 default. All folded.

**Important context update:** the [decision-gate doc](visibility_unified_plan_phase1_decision_gate.md) was updated in parallel to use a **relative secondary criterion** (`m4-vs-m3 ≤ m0-vs-m3 × 1.3`) instead of the original 0.02 absolute threshold. Under the corrected criterion, **the secondary RMSE gate now PASSES for the existing Mode 4 today.** The Phase 1 verdict shifted from "secondary fails + cost fails" to **"cost-only blocker."** This re-scopes Path A — see §2.1.

**TL;DR (rev 2):** Two paths, but **Path A is no longer the default first step.**

- **Path A (Phase 1.5):** add a per-bin **cone correction** to Mode 4. ~Half-day shader work. **Originally scoped to fix the secondary RMSE failure; that failure no longer exists under the corrected criterion.** Path A's only remaining purpose is to push Mode 4 quality further if a downstream user/test requires aggregate RMSE below the current 0.019, OR to reduce subjective grazing-angle artifacts. **Cost: ~+30% total frame vs Mode 0** (recalibrated up from +22% per critic 08 N1 — Path A's per-bin op count is 10, not 6). **Recommendation: opt-in only.**
- **Path B (Phase 2):** **store α as transparency in the existing RGBA16F atlas** (the RGB→RGBA migration the plan originally specified isn't needed — atlas was always 4-channel half-float; pre-flight audit found this), modify the bake-time cascade-inheritance merge per the RC paper. **2–3.5 day refactor** (down from 2.5–4.5 — 2A is empty; semantic change + cleanup are the only real work + bake-leak test scene authoring). Retires `uVisibilityMode` and Modes 0–4. Architecturally correct; addresses bake-time leaks Mode 4 cannot reach. **Cost prediction: matches Mode 0 ± single-digit %** (predicted, not measured — verified in Step 5). **2B breaks the just-landed Mode 4 default** — sequencing decision needed (see §3.8).

**Recommended sequencing (rev 2):** **bake-leak empirical test first** (§4.0, ~30 min manual A/B). Then per §4.1:
- If leaks observed → Path B mandatory.
- If clean and user accepts +10% frame cost (current Mode 4) → ship Mode 4 today as default; schedule Path B as architectural cleanup.
- If clean and user accepts only Mode-0 cost → Path B; Mode 4 stays opt-in.
- Path A appears only in opt-in / niche scenarios per §2.1.

---

## 1. Context

### What we know after Phase 1

- Mode 4 (signed-projection per-bin visibility) lands and matches Mode 3 quality at aggregate RMSE 0.019 (Sponza) and 0.007 (Cornell). **Primary decision-gate criterion passes.**
- Per-region RMSE shows Mode 4 is **+13–27% worse** than Mode 0 at matching the Mode 3 reference in the high-frequency lit_floor crop (0.030 vs 0.024 baseline). **Secondary decision-gate criterion fails on this crop.** Other two crops pass cleanly.
- Raymarch GPU cost is **+50% over Mode 0** (10.2 ms → 15.4 ms, single-frame RenderDoc capture). Total frame is +10.5%. The probe plan's "near Mode 0 cost" claim was overoptimistic.
- **Measured GPU effective throughput in this shader:** ~0.47 TFLOP (derived from Mode 4's ~2.4B added ops over ~5.1 ms). This is the calibration baseline for Path A's cost prediction below — **not** the headline 1 TFLOP figure I'd used in the original plan draft (critic 07 M1).
- Bake is **identical** across modes (radiance_3d ±0.4%) — Mode 4 is render-only, as designed.
- Bake-time leak materiality is **untested** at the cam.md viewpoint. The unified plan flagged this as a key Path B-or-not question.
- Mode 4's algorithm is a **flat-wall-perpendicular-to-bdir approximation** (assume the geometry the probe hit is a plane perpendicular to the bin direction). Real geometry can deviate.

### What this plan must answer

1. Is there a cheap quality-only fix that makes Mode 4 default-flippable (Path A)?
2. What's the precise spec for the architectural endpoint (Path B), refined by what we learned in Phase 1?
3. When do we run A vs B vs both?

---

## 2. Path A — Phase 1.5: Cone Correction

### 2.1 Goal (revised per critic 08 structural concern 1)

**What Path A is no longer for.** Under the [decision-gate doc](visibility_unified_plan_phase1_decision_gate.md)'s corrected secondary criterion (`m4-vs-m3 ≤ m0-vs-m3 × 1.3`), **the existing Mode 4 already passes secondary**. Path A was originally scoped to fix that failure; it doesn't need to anymore.

**What Path A might still be for:**
- (a) Tighter Mode-3 match if a downstream user / formal verification requires aggregate RMSE below the current 0.019.
- (b) Subjective improvement at high-detail viewpoints if a manual A/B reveals visible Mode 4 vs Mode 3 differences.
- (c) Reduced over-occlusion at grazing angles if Path B is delayed indefinitely and Mode 4 ships in production for an extended period.

**Cost penalty:** ~+30% total frame vs Mode 0 (vs Mode 4's +10.5%). Path A makes cost worse, not better. See §2.5 for the recalibrated math.

**Recommendation: do NOT run Path A unless one of (a/b/c) is actively required.** The default forward path (assuming §4.0 finds no leaks) is now "ship Mode 4 today + schedule Path B as cleanup." Path A is the "if extra quality is demanded later" branch.

The remaining sections below specify Path A in case it's needed; they do not change the recommendation against running it by default.

### 2.2 Algorithm (revised per critic 07 H1)

The current Mode 4 test (in `sampleProbeDirDepthAware`) treats the probe's hit as a flat wall perpendicular to `bdir` at distance `hitDist`. A surface point is occluded iff its signed projection along `bdir` is past the wall plane:

```glsl
// Current Mode 4 (flat-wall approximation):
float t = dot(surfacePos - probeCenter, bdir);
wvis = (t <= hitDist + missEps) ? 1.0 : 0.0;
```

This over-occludes when the surface is past the wall plane in the `bdir` direction but **lateral** to the bin's solid-angle cone — e.g., a floor surface viewed by a probe whose `bdir` ray hit a column's edge to the side. The wall the probe hit doesn't extend to where the surface is, so it shouldn't occlude.

**Cone correction:** the bin's solid angle defines a cone from the probe through the bin direction. The cone radius at any axial distance `s ≥ 0` is `s × tan(θ_half)`. A surface is geometrically inside the bin's cone iff `lateral ≤ s × tan(θ_half)` where `s = max(t, hitDist)` — see critic 07 H1 for why we use `max`, not just `hitDist`. Cone widens with distance; the cone-at-wall-plane is a lower bound, but a surface past the wall is at axial distance `t > hitDist` and the cone there is wider.

```glsl
// Path A — Mode 4 with cone correction (revised per critic 07 H1):
vec3  delta   = surfacePos - probeCenter;
float t       = dot(delta, bdir);
vec3  axial   = t * bdir;
vec3  lat_vec = delta - axial;
float lateral = length(lat_vec);

// Cone radius at the surface's axial position OR at the wall plane,
// whichever is farther from the probe. Past-wall surfaces use t (cone widens);
// near-probe surfaces use hitDist (defensive — past_plane=false anyway).
float cone_r = max(t, hitDist) * uConeTan;

bool past_plane  = (t > hitDist + missEps);
bool in_bin_cone = (lateral <= cone_r);
wvis = (past_plane && in_bin_cone) ? 0.0 : 1.0;
```

`uConeTan` is a uniform float so it can be tuned per-cascade or per-experiment without rebuilding shaders. Computed CPU-side once per cascade (no shader trig).

### 2.3 `θ_half` derivation (revised per critic 07 H2 + L1)

The bin covers solid angle ≈ 4π/D² steradians **on average** under octahedral mapping. The half-angle of the equivalent cone is approximately:

    θ_half = acos(1 − 2/D²)
    tan(θ_half) = sqrt(1 − (1 − 2/D²)²) / (1 − 2/D²)    (no shader trig needed)

For D=8: `tan(θ_half) ≈ 0.256`.

**Caveat (critic 07 H2; reference added per critic 08 N8):** octahedral mapping is **not** uniform-area. The mapping's Jacobian gives bins centered near the equatorial fold (z=0 in the projection) ~1× the average area while bins centered near the octahedron's vertices (e.g. ±x, ±y, ±z axes) get up to ~2× — a direct consequence of the projection (see e.g. Praun & Hoppe 2003, "Spherical parametrization and remeshing", or any modern octahedral-mapping reference). A single global `θ_half` will over-occlude in some directions and under-occlude in others.

Two paths to handle this:

- **(default) Conservative single value via `uConeTan` slider.** Start with the average-area derivation above. Verification step uses the slider to empirically tune; if the empirical optimum diverges >20% from the formula, accept the empirical value as-is.
- **(fallback) Per-bin `θ_half` LUT.** If the slider can't find a single value that passes the verification quality criterion, build a CPU-side D²-entry LUT from the actual octahedral Jacobian. ~64 floats per cascade (D=8) — trivial uniform array. Adds 1 fetch per bin in the shader.

The plan starts with single-value, falls back to per-bin LUT only if quality demands it.

### 2.4 Implementation

- **`raymarch.frag`**: extend `sampleProbeDirDepthAware` with the revised cone test (~7 lines). Add a `uniform float uConeTan;`.
- **`demo3d.cpp`**: bind `uConeTan` from a public setter `setConeTan(float)` (defaulted to the average-area derivation per current `dirRes`). Add an ImGui slider in the cascade panel's "Sampling" tab for live tuning during verification. Auto-recompute default on `dirRes` change.
- **`main3d.cpp`**: optional `--cone-tan=X` CLI flag for headless A/B captures.
- **No bake changes.**

### 2.5 Cost analysis (re-recalibrated per critic 08 N1+N3)

Per bin, Path A adds (above existing Mode 4's `dot(delta, bdir) + compare`):

| Op | Count |
|---|---:|
| `axial = t * bdir` (vec3 mul) | 1 |
| `lat_vec = delta - axial` (vec3 sub) | 1 |
| `lateral = length(lat_vec)` (dp3 + sqrt) | 2 |
| `cone_r = max(t, hitDist) * uConeTan` (max + mul) | 2 |
| `past_plane = (t > hitDist + missEps)` (add + cmp) | 2 |
| `in_bin_cone = (lateral <= cone_r)` (cmp) | 1 |
| `wvis = past && cone ? 0 : 1` (and + select) | 1 |
| **Total added/bin** | **10** |

Note: `delta = surfacePos - probeCenter` is hoisted out of the bin loop in the existing Mode 4 code (per-corner, not per-bin), so it's not in the added-per-bin tally.

Total per pixel: 8 corners × D²=64 bins × 10 ops = **5120 added ops/pixel**. At 921k pixels = ~4.7B added ops/frame. At Phase 1's measured 0.47 TFLOP-effective, that's **~10 ms added on top of Mode 4's 15.4 ms = ~25 ms raymarch**.

**Honest cost expectation: raymarch ~+145% over Mode 0** (vs Mode 4's +50%, vs Mode 0's 10.2 ms baseline → ~25 ms). Frame total: ~67 ms (~+30% over Mode 0's 51.8 ms baseline / ~15 FPS vs Mode 0's ~19 FPS).

Caveat: linear extrapolation past Mode 4's measured baseline is increasingly speculative — register pressure and warp occupancy may shift in ways that compound. The +25 ms predicted raymarch is itself an under-estimate if Path A pushes the kernel into a worse occupancy class. RenderDoc verification (§2.6 step 2) is the only way to know.

If +30% total frame cost is unacceptable, skip Path A. Verify with the user before starting (decision-tree §4.1).

### 2.6 Verification

Re-run the existing two tests against the Path A binary at the cam.md Sponza viewpoint:

1. **Per-region RMSE** (`tools/analysis/phase1_region_metrics.py`):
   - **Pass:** lit_floor m4_cone-vs-m3 RMSE ≤ 0.024 (Mode-0 baseline). Other two regions remain ≤ their Mode-0 baselines.
   - **Fail:** lit_floor RMSE > 0.024 OR any other region degrades by >20% vs current Mode 4.
2. **RenderDoc raymarch timing** (`--auto-rdoc` per phase 1 protocol):
   - **Expected:** raymarch ≈ 21 ms. **Confirm against measured value, not the prediction.**
   - Side-check (critic 07 M5): record `gi_blur` and `glDrawElements` timings; if either varies by >5% across captures, flag as separate timing-noise question — don't conflate with Path A.
3. **Add a second viewpoint** (critic 07 §6 single-viewpoint-bias): pick a Sponza alcove view where Mode 4 was suspected to leak (manually identified during the bake-leak test in §4 below). Run both metrics there too.
4. **`uConeTan` sweep** if step 1 fails at the default value: try `tan(π/D)` (wider, less restrictive), `tan(π/(2D))` (narrower, more restrictive), and 2–3 values in between. Record per-value RMSE; if the empirical optimum is consistent across viewpoints, accept as the default. If not, escalate to per-bin LUT (§2.3 fallback) — if LUT also fails, Path A is dead.

### 2.7 Decision gate at end of Path A

| Outcome | Recommendation |
|---|---|
| Quality PASS + user accepts ~+22% frame cost (Q2 = YES) | Default-flip Mode 4-with-cone. Schedule Path B as the architectural endpoint (lower urgency). |
| Quality PASS + user rejects +22% frame cost (Q2 = NO) | Keep Mode 4-with-cone as opt-in. Prioritize Path B. |
| Quality FAIL after `uConeTan` sweep AND per-bin LUT fallback | Skip default flip. Path B is the only remaining option. |
| Quality PASS at cam.md but bake leaks visible at the second viewpoint (§2.6 step 3) | Path B is mandatory regardless. Path A becomes interim opt-in only. |

### 2.8 Why Path A might fail

- **Lit_floor RMSE may be aliasing-driven, not occlusion-driven.** High-frequency railings + bright lighting at 1280×720 single-frame can have sub-pixel-sampling differences between Mode 0 and Mode 3 that no visibility-mode change can fix. Mode 0 vs Mode 3 baseline of 0.024 is the strong evidence for this hypothesis.
- **Cone correction may over-correct.** Reducing false-positive occlusion can reintroduce the leaks Mode 4 was supposed to fix. Need to verify columns and shadowed regions don't degrade.
- **Octahedral non-uniformity may dominate** — single global `θ_half` won't track the area variation. Falling back to per-bin LUT is the escape hatch.

---

## 3. Path B — Phase 2: Interval Atlas (refined)

This is the **source of truth** for Phase 2 implementation. The corresponding section in [visibility_unified_plan.md](visibility_unified_plan.md) §"Phase 2 — Interval atlas" is **historical** as of this plan's approval; refer here for the current spec (critic 07 E2).

### 3.1 Goal

Eliminate `probeVisibility()` and `uVisibilityMode` entirely by making the atlas store **radiance intervals** (RGB + α-as-transparency) per the RC paper. Visibility becomes a property of the data, not a render-time test. Fixes:

- **Render-time correctness** (Mode 4's quality, possibly better)
- **Bake-time leaks** (Mode 4 cannot reach this — the gating question)
- **Render-time cost** — strictly faster than Mode 4 (no per-bin compare); **expected to match Mode 0** (one extra multiply per bin is essentially free; **predicted, not measured — verify in Step 5**, per critic 07 M2/E1)

### 3.2 Hard prerequisite

`temporal_blend.comp` `cur.a` preservation patch is **already landed** (Phase 1 commit A). Phase 2 inherits this rule and extends it: α is **fresh-only** in Phase 2 too, never EMA-blended (otherwise hit/miss flicker would silently produce soft α values mid-bake).

### 3.3 Atlas format — NO CHANGE (revised per pre-flight audit)

The atlas has been **`GL_RGBA16F`** since Phase 5g — not `GL_RGB8` as earlier revisions of this plan stated. See [demo3d.cpp:2856-2858](../../../src/demo3d.cpp#L2856-L2858); same for the temporal-history atlas at [demo3d.cpp:2871](../../../src/demo3d.cpp#L2871). The pre-flight audit found this; full enumeration in [visibility_phase2_preflight_audit.md](visibility_phase2_preflight_audit.md).

**Implications:**

- **No allocation change needed.** Atlas is already 4-channel half-float (16-bit per channel = 64-bit per texel = 8 bytes/texel). Phase 2 reuses the existing alpha channel.
- **Memory cost: zero increase.** The "+33%" caveat in earlier plan revisions was based on the wrong baseline.
- **Phase 2A (the format-only sub-commit + bit-exact verification checkpoint) is empty.** No code change, nothing to verify, nothing to commit. Skipped entirely.
- **Higher precision than originally planned.** `GL_RGBA16F` gives effectively continuous α (not just binary 0/1), so the "Soft α (0..1) is a future refinement" caveat in §3.6 is no longer architectural. Soft α can be added in a future commit without an atlas-format change — just by changing the bake's α derivation.

### 3.4 Bake changes (`radiance_3d.comp`)

**`hit.a` definition (added per critic 08 N4):** the bake shader writes the per-bin ray result into the alpha channel of the directional atlas with this convention (live in `radiance_3d.comp:428` today):

- `hit.a > 0.0` — surface hit at distance `hit.a` (world units along ray)
- `hit.a == 0.0` — in-volume miss (ray reached interval end without hitting geometry)
- `hit.a < 0.0` — sky exit sentinel (ray left the SDF volume)

Phase 1's Mode 4 reuses this encoding directly as a hit-distance. Phase 2 reinterprets the same encoding into a transparency-α derivation:

Per-direction loop already classifies the ray result. Phase 2 stores it in α:

```glsl
float alpha;
if      (hit.a > 0.0) alpha = 0.0;   // surface hit → opaque interval
else if (hit.a == 0.0) alpha = 1.0;  // in-volume miss → transparent
else                   alpha = 1.0;  // sky sentinel → transparent (sky fill)
```

Modify the cascade-inheritance merge at the far boundary to use the RC paper's interval composition:

    L_{a,c} = L_{a,b} + β_{a,b} × L_{b,c}
    β_{a,c} = β_{a,b} × β_{b,c}

In code:

```glsl
// thisRad, thisAlpha = this cascade's near interval (from the local raymarch)
// upperDir.rgb, upperDir.a = upper cascade's far interval (from texelFetch)
rad   = thisRad + thisAlpha * upperDir.rgb;
alpha = thisAlpha * upperDir.a;
imageStore(oAtlas, atlasTxl, vec4(rad, alpha));
```

The current `l` smoothstep at the boundary is a **separate concern**. Phase 2 sub-task: keep `l` applied to **radiance only** (the smoothstep softens the cascade-handoff seam visually, independent of the α merge). Or replace `l` with an explicit interval-boundary handoff. **Pin this decision before the semantic-change sub-commit lands** (don't merge a half-decided combination).

### 3.5 Render changes (`raymarch.frag`)

`sampleProbeDir` reads RGBA. The α channel becomes the per-bin visibility weight inside the existing cosine-weighted hemisphere integration:

```glsl
vec4 a = texelFetch(uDirectionalAtlas, ...);
float w = max(0.0, dot(bdir, normal)) * a.a;
irrad += a.rgb * w;
wsum  += w;
```

`sampleDirectionalGI` blends 8 corners with the trilinear weights as before. The blend is now **safe by construction** (interpolating intervals, not full radiance — the paper's penumbra condition guarantees linear interpolatability).

**Mark as deprecated** in Phase 2 commit B (do not delete in same commit): `probeVisibility()`, `uVisibilityMode`, modes 0..4, `sampleProbeDirDepthAware`, `sampleProbeDirPerBinOccluded`. Keep through verification so A/B comparisons remain available.

**Delete** in a separate cleanup commit after Phase 2 verification passes.

### 3.6 EMA discipline

α is **fresh-only — never EMA-blended.** The `temporal_blend.comp` patch from Phase 1 already establishes "preserve `cur.a`" as the convention. Phase 2 inherits it with no change. Soft α (a future refinement) requires an explicit decision; not in initial Phase 2.

### 3.7 Cost expectation (revised per critic 07 M2/E1; further revised per pre-flight audit)

Per pixel, the new render path:
- 8 corners × D² × `vec4` fetch — **same fetch the existing Mode 0 path already does** (the atlas was always `GL_RGBA16F`; Mode 0 just uses `.rgb` and discards `.a`). Bandwidth literally unchanged.
- Per bin: 1 multiply (cosine × α), 1 multiply-add (irrad += a.rgb × w), 1 add (wsum += w)

vs Mode 0's current:
- Same fetch (vec4 at hardware level; Mode 0 swizzles `.rgb`)
- Per bin: 1 multiply (cosine), 1 multiply-add, 1 add

**Predicted: Phase 2 render cost ≈ Mode 0 cost ± single-digit %.** No per-bin compare, no per-bin sqrt, no outer `probeVisibility` shadow trace, no atlas format change to invalidate caches. The only added op is the α multiply, fused into the existing cosine multiply.

This is **strictly faster than Mode 4** (which has the +50% raymarch overhead from the per-bin signed-projection test). vs Mode 0 specifically, the prediction is "essentially equal" — and the audit-confirmed shared fetch pattern strengthens the prediction further. **Still a prediction, not a measurement.** Verify in §3.9 Step 5.

**Memory cost: zero** (atlas was already RGBA16F; reusing existing alpha channel).

### 3.8 Pre-flight tasks (revised per pre-flight audit)

1. ~~**Fetch-site enumeration + 2A bit-exactness check.**~~ **Done — and it eliminated 2A from the plan.** [Pre-flight audit](visibility_phase2_preflight_audit.md) found the atlas is `GL_RGBA16F` already (since Phase 5g); no allocation change exists for 2A to bit-exact-verify. Atlas-fetch sites enumerated in the audit doc.
   - **Practical consequence:** the original 3-sub-commit shape (2A → 2B → 2C) collapses to **2B → 2C** (semantic change → cleanup). No "inert format change" checkpoint exists between them.

2. **Bake-leak test scene authoring** (critic 07 H3 — the original plan asserted Cornell-orig has an "occluded region inside" but it's a closed box with no inside-the-wall void). Three options, pick one:
   - **(A) Sponza alcove probe (no new scene work).** Use Sponza-master, navigate to a probe position deep inside an alcove with a column between it and the main light source. Define "occluded region" as "probes inside the alcove whose `pos.dot(bdir_to_light) > 0` AND a wall is between them and the light." Quantitative test: sum atlas radiance for those probes in their light-facing bins; should be ~0 post-Phase-2. **Pro:** no new asset needed. **Con:** "is the wall between this probe and the light" check is fiddly and needs scene-specific code.
   - **(B) Modify Cornell-orig to add an interior partition.** Author a cornell-with-alcove.obj — Cornell + a free-standing wall inside that creates a shadowed alcove. ~1h asset work. **Pro:** clean test geometry; quantitative threshold is "atlas radiance behind the partition ~0". **Con:** new asset to maintain.
   - **(C) Analytic-SDF test scene.** Two parallel walls + a light in front; check probes in the gap behind the second wall. **Pro:** programmable, no .obj. **Con:** need to plumb a new analytic-SDF preset.
   - **Recommendation:** option B. ~1h asset work pays off as a permanent regression scene — useful for future visibility/lighting tests too. **Independent of the 2B/2C sequencing decision below; can be done in parallel.**

3. **Pick the decision for the smoothstep `l`** (critic 04 holdover): keep `l` applied to radiance only, or replace with explicit interval-boundary handoff. Decide before the 2B commit.

4. **NEW — 2B sequencing decision (X/Y/Z) — see audit doc §"Implications".** Mode 4 is the just-landed default; 2B's bake-side α-derivation **breaks Mode 4** (its `a.a`-as-`hitDist` reading collapses to binary 0/1, which makes the signed-projection visibility test meaningless). Modes 0/1/2/3 are unaffected (they ignore alpha).
   - **Option X (recommended):** Land 2B + revert default to Mode 0 in the same commit (Mode 0 leaks reappear briefly). 2C cleanup ships within hours and the new α-gated Mode-0-equivalent path becomes the permanent default. Two clean commits in history.
   - **Option Y:** Land 2B + update Mode 0's `sampleProbeDir` to the new α-gated path simultaneously. Default stays nominally at Mode 4 but Mode 4 renders broken — confusing for ImGui combo readers.
   - **Option Z:** Bundle 2B + 2C in one commit. Bigger blast radius if regression found; cleanest end-state. No A/B fallback during verification.
   - **Pick before 2B starts.** This decision did not exist in earlier plan revisions because the pre-flight audit hadn't surfaced the Mode 4 break.

### 3.9 Verification

1. **Build + smoke** (per Phase 1 protocol).
2. **Quality A/B** at cam.md Sponza viewpoint:
   - Phase 2 capture vs Phase 1 Mode 4 capture: should match or improve on FLIP/RMSE metrics.
   - Phase 2 capture vs Phase 1 Mode 0 capture: should be visibly different in occlusion-relevant regions (corridor depth, columns).
3. **Per-region RMSE** (re-run `phase1_region_metrics.py` against Phase 2 captures): all three regions should beat their Mode-0 baselines.
4. **Bake-time leak test** (per pre-flight task #2 above):
   - Place a probe in the occluded region of the chosen test scene.
   - **Capture the atlas after EMA convergence** (added per critic 08 N7) — either ≥ N=60 frames at temporalAlpha=0.1 (so RGB values are stable), OR with temporal accumulation OFF for a deterministic single-frame bake. Mid-bake reads conflate temporal noise with the merge-formula behaviour we're trying to test.
   - Inspect bins facing the light source via RenderDoc texture viewer or `glReadPixels` of `uDirectionalAtlas`.
   - Pre-Phase-2: those bins carry nonzero RGB (cross-wall leak). Post-Phase-2: α=0 OR RGB=0.
   - Quantify: `sum(bin.rgb * bin.a)` across all bins of all probes in the occluded region. Pre-Phase-2 baseline > 0; post-Phase-2 ~0.
5. **RenderDoc raymarch timing** (the cost-prediction verifier — critic 07 M2): expected ≤ Mode 0 + 5%. **If raymarch is unexpectedly slow (>20% over Mode 0), file as a separate perf investigation; don't ship Phase 2C cleanup until resolved** (deprecated modes still useful for fallback during investigation).
6. **Per-cascade visibility heatmap** (deferred from Phase 1 verification — same script applies once `wvis` becomes the α channel).
7. **Atlas memory** measured on target GPU; confirm against the perf-tooling Step 12 budget.
8. **Multi-viewpoint quality** (critic 07 §6): Sponza (cam.md) + Cornell-orig + the new bake-leak test scene. Each should pass §3 RMSE checks.

### 3.10 Decision gate at end of Phase 2

| Outcome | Action |
|---|---|
| Phase 2 matches/beats Mode 4 quality, raymarch ≤ Mode 0 +20%, bake-leak test PASS | Run Phase 2C cleanup commit. Mode 4 + companions deleted. New default. |
| Phase 2 matches Mode 4 quality but raymarch unexpectedly slow (>+20%) | Keep deprecated modes around; investigate before cleanup. File as a perf follow-up. |
| Phase 2 quality regresses vs Mode 4 (e.g. soft transitions show banding from binary α) | Promote α to soft (smoothstep based on SDF proximity) — a Phase 2.5 task — or revert Phase 2B and reconsider. |
| Bake-leak test still shows leaks in occluded region | The interval merge formula is wrong — bisect Phase 2B's bake changes. Don't ship cleanup. |

### 3.11 Scope estimate (revised per pre-flight audit)

**2–3.5 days**, broken down (down from 2.5–4.5 — pre-flight grep audit done, 2A removed):

- ~~Pre-flight grep audit + fetch-site enumeration: 0.5 day~~ — **DONE** (see [audit doc](visibility_phase2_preflight_audit.md))
- Pre-flight bake-leak scene authoring (option B from §3.8): 0.5 day
- ~~Phase 2A (atlas format change, no-op): 0.5 day~~ — **REMOVED** (atlas was always RGBA16F)
- Phase 2B (bake-side α + interval merge + render-side α gate; coordinated with default-flip per X/Y/Z choice): 1 day
- Verification (quality A/B + bake-leak test + timing + heatmap + multi-viewpoint): 1 day
- Phase 2C (cleanup + CLI deprecation grace) + buffer for surprises: 0.5–1 day

(Net savings: 1 day vs the rev-2 estimate. The bake-leak scene authoring stays as the only remaining pre-flight work.)

---

## 4. Combined sequencing decision tree (revised per critic 07 M3+M4+E3)

The original tree had a question (Q1) the user couldn't answer (no manual A/B at other viewpoints) and a question (Q2) phrased as yes/no without quantifying the cost in user-meaningful terms. Revised:

### 4.0 Prerequisite empirical test (replaces Q1)

**Run a 30-min manual bake-leak A/B session before any path branches.** Goals:
- Load Sponza-master and Cornell-orig in turn.
- For each scene, navigate to 3–4 viewpoints: corridor along axis, alcove looking at wall, surface near a wall, view inside a column-shadowed area.
- Toggle `--visibility-mode={0, 4}` (or use ImGui combo) and look for cross-wall light bleed.
- Record viewpoints + observations. If any viewpoint shows visible cross-wall bleed in Mode 4 (not just Mode 0), Path B is mandatory.

**Why this matters:** the Q1 in the original tree assumed the user already knew. They don't — neither do we. This empirical test produces the data the decision branch needs. If the user can't spend 30 min on this, default to Path B (safer; avoids Path A wasted work if leaks turn out to be material).

### 4.1 Decision branches (revised per critic 08 N5 + structural concern 2)

After §4.0 prerequisite. Secondary RMSE under the corrected criterion (`m4-vs-m3 ≤ m0-vs-m3 × 1.3`) **already passes for the existing Mode 4** — so Mode 4-today is now a defensible default if cost is acceptable, not just an option for "good enough."

| Empirical leak test result | + Cost tolerance for default-on visibility | → Path |
|---|---|---|
| Clear leaks at one or more viewpoints in Mode 4 | Any | **Path B only.** Skip Path A entirely (its cone correction doesn't fix bake-time leaks). |
| **Ambiguous** — mild bleed at some angles, hard to distinguish from baked-in indirect lighting | Any | **Path B with normal urgency.** Don't ship Mode 4 as default; the architectural fix removes the question. Path A still wasteful — same reason. |
| Clean (no observable cross-wall bleed at any tested viewpoint) in Mode 4 | User accepts current Mode 4 cost (+10% frame) | **Default-flip Mode 4 today** (corrected secondary already passes). Schedule Path B as architectural cleanup, lower urgency. Skip Path A. |
| Clean | User accepts only Mode 0 cost (no defaultable Mode 4 at any cost) | **Path B only.** Mode 4 stays opt-in indefinitely. |
| Clean | User actively requires Path A's quality bump (one of §2.1 (a/b/c)) AND accepts +30% frame cost | **Path A first** (rare). If quality improvement materialises, default-flip Mode 4-with-cone. Path B as scheduled cleanup. |

### 4.2 Cost tolerance — what does "X% frame cost" mean to the user? (revised per critic 08 N2)

The previous version used "60 FPS baseline" — wrong. **Phase 1's measured frame total is 51.8 ms (~19.3 FPS) at 1280×720 Sponza.** Table rebuilt with the measured baseline + recalibrated Path A:

| Variant | Quality vs Mode 3 | Frame time (measured / predicted) | Approx FPS |
|---|---|---:|---:|
| Mode 0 (current default) | Differs by 0.024 RMSE; leaks through walls | **51.8 ms (measured)** | ~19 |
| Mode 4 today | 0.019 RMSE; secondary now PASSES under corrected criterion; no banding | **57.2 ms (measured, +10.5%)** | ~17.5 |
| Mode 4 + cone (Path A) | Predicted to tighten lit_floor diff; primary purpose moot under corrected criterion | **~67 ms (predicted +30%; recal per N1)** | ~15 |
| Phase 2 (Path B) | Matches Mode 4 + fixes bake-time leaks | **~52 ms (predicted ≈ Mode 0)** | ~19 |

Caveats kept explicit: Path A and Path B rows are predictions, not measurements. The Mode 0 / Mode 4 rows are measured. If you're already running on a faster GPU than this development machine, scale proportionally.

### 4.3 What happens to existing Mode 4 + CLI users during Path B development (critic 07 M6)

If the decision is to ship Path A as default while Path B is in flight:
- Mode 4-with-cone becomes the default in the next release.
- `--visibility-mode={0,1,2,3,4}` CLI continues to work. Mode 4 specifically reads the cone-correction code path.
- ImGui combo continues to expose all 5 modes.
- During Path B development, no breaking changes to the CLI/UI.
- When Path B's commit 2C cleanup lands, `--visibility-mode=N` becomes a no-op stub with a deprecation warning for one release. Removed in the release after that.

If the decision is to skip Path A and go directly to Path B:
- Mode 4 stays opt-in (default 0 unchanged).
- CLI/UI unchanged.
- 2C cleanup deletes the whole switch.

### 4.4 Happy-path timeline (critic 07 E5; revised per critic 08 + pre-flight audit)

What's actually happened so far + remaining work, with measured progress:

| Day | Event | Status |
|---|---|---|
| 0 | Plan approved. | ✅ Done |
| 0–0.5 | §4.0 prerequisite empirical leak test (autonomous: 5 viewpoints, 3 with valid scene content). | ✅ Done — verdict CLEAN |
| 0.5 | **Mode 4 default-flip** (`visibilityMode = 4`). | ✅ Done; build clean; smoke RMSE 0.000000 vs explicit `--visibility-mode=4` |
| 0.5 | Path B pre-flight #1 (grep audit + format discovery). | ✅ Done — atlas was always `GL_RGBA16F`; **2A is empty**; see [audit doc](visibility_phase2_preflight_audit.md) |
| 0.5–1 | Path B pre-flight #2 (bake-leak test scene authoring, Cornell + interior partition; option B from §3.8). | ⏳ Pending |
| 1 | **2B sequencing decision (X/Y/Z)** picked by user. | ⏳ Pending |
| 1–2 | Phase 2B implementation (bake-side α + interval merge + render-side α-gate; coordinated with default-flip per X/Y/Z choice). | ⏳ Pending |
| 2–3 | Verification (quality A/B + bake-leak test + RenderDoc timing + multi-viewpoint). | ⏳ Pending |
| 3–3.5 | Phase 2C cleanup + CLI deprecation grace begins. | ⏳ Pending |
| 3.5+ | Modes 0–4 deleted; visibility is "the atlas just stores it." Default-flip from Day 0.5 is retired (2C deletes the mode it pointed at). | ⏳ Pending |

If §4.0 had found bake leaks → would have skipped the Day-0.5 default-flip; Path B would still be the same path. Path A was not run (corrected secondary criterion already passed for Mode 4).

---

## 5. What is explicitly NOT in this plan

- **Mode 5 (mode 4 + 1 confirmation shadow ray).** Removed from the gate by critic 04. Filed; no current motivation.
- **Soft α (0..1) in Phase 2.** Future refinement requiring explicit decision; not in initial Phase 2.
- **Per-cascade `uConeTan` / per-bin LUT default.** Path A starts with one global value; per-cascade tuning OR per-bin LUT only if quality varies cascade-to-cascade or the global value can't satisfy the verification criterion.
- **Per-cascade α derivation logic** (critic 07 L4). Current Phase 2 uses the same hit/miss/sky classification rule per cascade; cross-cascade α-coherence is a future Phase 2.5 if quality issues emerge near cascade boundaries.
- **Half-resolution visibility (Strategy 6 from the broader visibility plan).** Obsolete after Phase 2.
- **Strategies 3, 4, 5, 7 from the broader visibility plan.** All accelerate `probeVisibility()` which Phase 2 deletes.
- **GUI cleanup of the `uVisibilityMode` combo.** When Phase 2C lands, the combo entry disappears with the rest. Don't pre-remove.
- **Path B's pre-flight bake-leak scene authoring** is in scope as Phase 2 prerequisite work (§3.8 option B, ~0.5 day) — promoted from "out of scope" in critic 07 H3 fix.

---

## 6. Open risks

- **Path A may not help.** If lit_floor RMSE is aliasing-driven (high-frequency floor detail at single-frame 1280×720), no visibility-mode tweak fixes it. Path A would burn a half day proving this.
- **Path B's cost prediction (≤ Mode 0 + 5%) is unverified** (critic 07 M2). Verification §3.9 step 5 is the only thing that confirms it. If Phase 2 turns out to be unexpectedly slow, the cleanup commit is blocked pending investigation.
- **Path B's interval-merge formula assumes binary α.** With binary α, the formula `rad = thisRad + thisAlpha * upperDir.rgb` only contributes the upper cascade when `thisAlpha = 1` (this cascade missed). That's correct for opaque walls. For thin walls or partial-coverage surfaces, binary α produces hard discontinuities that soft α (a future refinement) would smooth. Not an immediate blocker but worth noting.
- ~~**Atlas memory increase.**~~ **Not a risk** — pre-flight audit found atlas is already `GL_RGBA16F`. No format change, no memory delta. Risk closed.
- **Mode 4 break window** (added after pre-flight audit). The just-landed Mode 4 default reads `a.a` as `hitDist`. Phase 2B replaces `hit.a` semantics with transparency-α (0/1 binary); Mode 4 starts rendering wrong the moment 2B lands. The X/Y/Z sequencing decision in §3.8 task #4 picks how to manage this — Option X (revert default to Mode 0 for one commit until 2C ships) is recommended.
- **Phase 2 cascade-inheritance smoothstep `l` interaction with α-gated merge.** Critic 04 flagged this; Phase 2 §3.8 task #3 to pin before merge.
- **Octahedral non-uniformity** (critic 07 H2) — single-value `uConeTan` may not satisfy quality across all bin directions; per-bin LUT fallback is filed.
- **Bake-leak test scene needs authoring** — option B from §3.8. Until that's done, the bake-leak gating question for Path B priority isn't decidable from existing assets.
- **Cone correction at `t < 0` and large lateral** (critic 07 L5) — surfaces behind the probe and far off-axis return `wvis = 1.0` but their radiance contribution from this bin is geometrically irrelevant. Cosine weighting `wcos = max(0, dot(bdir, normal))` mitigates but doesn't eliminate. Verification step should confirm no cross-volume radiance leakage from such surfaces.

---

## 7. Files this plan would produce, when executed

### Path A
- `res/shaders/raymarch.frag` (edit `sampleProbeDirDepthAware` + new uniform)
- `src/demo3d.h`, `src/demo3d.cpp` (uniform binding + ImGui slider in Sampling tab)
- `src/main3d.cpp` (`--cone-tan=X` CLI)
- `tools/phase1.5_sponza_m4cone.png` (capture at cam.md)
- `tools/phase1.5_sponza_alcove_m4cone.png` (capture at the second viewpoint chosen during §4.0)
- `tools/phase1.5_rdoc_m4cone.rdc` + manifest
- `tools/phase1.5_region_metrics.json`
- `doc/6/claude_plan/visibility_unified_plan_phase1.5_impl.md` (renamed for convention; critic 07 L3)
- `doc/6/claude_plan/visibility_unified_plan_phase1.5_decision_gate.md`

### Path B
- `res/scene/cornell-orig-alcove/` (new test scene — pre-flight asset, option B from §3.8)
- `res/shaders/radiance_3d.comp` (α storage + interval merge)
- `res/shaders/raymarch.frag` (RGBA fetch + α-gate + delete deprecated samplers post-cleanup)
- `src/demo3d.h`, `src/demo3d.cpp` (atlas format change in allocation; ImGui combo entry removed in 2C)
- `src/main3d.cpp` (`--visibility-mode=N` deprecated stub)
- `tools/phase2_*.png` (captures for Sponza + Cornell + new alcove test scene)
- `tools/phase2_atlas_leak_test.json` (bake-leak quantification)
- `doc/6/claude_plan/visibility_unified_plan_phase2_impl.md`
- `doc/6/claude_plan/visibility_unified_plan_phase2_decision_gate.md`

### Build/branch state for verification (critic 07 L2)
- All captures taken from a Release build at the head of the corresponding feature branch (`phase1.5` or `phase2`), with the impl commit being the one referenced in the impl doc. No mixing captures from different commits.
