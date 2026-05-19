# Plan: Temporal Multi-Bounce Feedback in Cascade RC — rev 2

**Date:** 2026-05-18 (rev 1) → 2026-05-18 (rev 2, post critic-03)
**Goal:** Add temporal indirect-light feedback to the cascade bake so the renderer captures multi-bounce indirect lighting instead of just direct + 1 indirect bounce.
**Motivation:** PT reference (Phase 7) revealed cascade lacks multi-bounce. Measured contribution on cornell-orig: **~7-22% of total brightness from bounces 2+** (revised from 7-10% per critic-03 L1 — multi-bounce in TIME differs from PT@N).
**Critic chain extends:** [critic 03](critic/03_multi_bounce_temporal_plan_review.md) → revision 2. 3 HIGH + 4 MEDIUM + 3 LOW. All applied.
**Status:** Plan only. No code changes in this doc.

---

## TL;DR (rev 2, post critic-03)

- In the cascade bake's `raymarchSDF`, when a ray hits a surface, sample the **previous frame's atlas at the hit position** with **cosine-weighted hemisphere integration** (same shape as `sampleProbeDir` in raymarch.frag). Add as indirect contribution.
- **Directional feedback from v1** (per critic-03 H3). Isotropic would over-amplify (reduction is simple-average, not cosine-weighted — per critic-03 H1+H2 confirmed by reading [reduction_3d.comp](../../res/shaders/reduction_3d.comp)).
- **Use shared C0 history for ALL cascades' feedback** (per critic-03 M2). Single texture binding; converges at C0's update rate (1 frame); best spatial resolution.
- Over frames, bake converges to multi-bounce equilibrium. Stability bound: `albedo × <cos>_hemisphere < 1` (true for all physical scenes; geometric series).
- New uniform `uUseMultiBounce` (default OFF — preserves current single-bounce behavior).
- New uniform `uMultiBounceGain` (default 1.0; gain < 1 for energy clamping if needed).
- **History rejection via clamping to local-neighborhood AABB** (per critic-03 M4) — composes with existing temporal-α EMA fix.
- Validation via PT-RMSE: expect cascade brightness to increase from 0.242 → measurably closer to PT-cascade-match (0.42). Could close ~7-22% of the 42% gap; needs empirical measurement (critic-03 L1: speculative without prototype).
- Ship as v0.5 prototype first to MEASURE the actual gain, then iterate to final shader (critic-03 cross-cutting).
- Estimated effort: **~3-4 days** (was 2 in rev 1; critic-03 scope-creep correction).

---

## 1. Why we need this

### From [pt_reference_impl.md](pt_reference_impl.md) H3 finding

Three-way A/B on cornell-orig revealed cascade is **42% darker than PT reference** (cascade-match mode, RMSE 0.34). Bounce-count breakdown:

| PT max bounces | Brightness | Interpretation |
|---:|---:|---|
| 1 (direct only) | 0.345 | No GI at all |
| 2 (direct + 1 indirect — equivalent to cascade design) | 0.393 | What cascade SHOULD capture |
| 8 (effectively converged) | 0.421 | Full multi-bounce truth |
| **Cascade** | **0.242** | Current state |

- Multi-bounce contribution (PT@2 → PT@∞): **+0.028 brightness** (~7% of cascade's gap)
- Cascade vs PT@2 (same bounce count): cascade misses **0.151 brightness** (~38% of total)

**This plan addresses ONLY the multi-bounce portion** (~7-10% improvement). The 38% integration loss is a separate workstream.

### Why multi-bounce is a "quick win"

- **Architecturally local**: only changes the bake's per-hit shading (~10 lines of shader).
- **Cheap**: 1 extra texture sample per hit (isotropic) or ~D² samples (directional).
- **Self-stabilizing**: feedback is convergent for albedo × cosine factor < 1 (true for all sane scenes; geometric series).
- **Toggleable**: opt-in via uniform; default OFF preserves current behavior bit-exactly.

### Confirmed: cascade is single-bounce only

[demo3d.cpp:2526-2528](../../src/demo3d.cpp#L2526):
```cpp
void Demo3D::injectDirectLighting() {
    // Phase 2: inject_radiance.comp is frozen; updateSingleCascade() handles lighting.
    return;  // no-op
}
```

Bake's `raymarchSDF` at hit shades `albedo × (direct × lightColor + ambient)` only. No previous-frame atlas lookup. Confirmed by direct code reading.

---

## 2. Scope (v1)

### In scope

- New uniform `uUseMultiBounce` (default 0) — gate the feedback path.
- New uniform `uMultiBounceGain` (default 1.0) — multiplier on feedback for energy-clamp safety.
- Modified `raymarchSDF` in `radiance_3d.comp`: when uUseMultiBounce==1, sample the previous frame's atlas/grid at the hit position and add `albedo × feedback × gain` to the hit color.
- C++ binding: pass `probeGridHistory` (already exists for temporal accumulation) as `uPrevFrameRadiance` to the bake.
- GUI toggle + slider for gain.
- CLI flag `--use-multi-bounce=N` and `--multi-bounce-gain=F`.
- Validation: A/B cascade vs PT cascade-match at PT@2, PT@4, PT@∞ — verify gap closes.

### Out of scope (v1)

- **Directional feedback** (sample D² bins per hit) — defer to v2 if v1 quality insufficient.
- **Per-cascade feedback chains** (each cascade samples its own history) — v1 uses C0's grid for all cascades' feedback (single shared history).
- **NEE / MIS** — already discussed as Phase 7 v2.
- **Specular feedback** (mirror bounces) — diffuse only.
- **Energy clamping beyond `uMultiBounceGain`** — fancy clamp schemes (luminance-aware, history rejection) deferred.

### Hard non-goals

- **NOT changing the cascade architecture.** No new textures, no new dispatch passes. Reuse existing history textures.
- **NOT changing the display path.** Multi-bounce shows up automatically in the atlas; display reads atlas as before.
- **NOT changing PT.** PT remains the reference.

---

## 3. Algorithm (rev 2 — directional feedback)

### What we actually have to work with (verified per critic-03 H1+H2)

[reduction_3d.comp:39-41](../../res/shaders/reduction_3d.comp#L39): `avg += samp; avg /= D²;` — **simple arithmetic mean** of D² atlas bins. NO cosine weighting. NO α-gating. Just `Σ bin / D²`.

[raymarch.frag's `sampleProbeDir`](../../res/shaders/raymarch.frag): the display path does cosine-weighted hemisphere integration with α-gate: `irrad += a.rgb × wcos × a.a; wsum += wcos × a.a; return irrad / max(wsum, 1e-4)`.

**Implication**: if we sampled `probeGridTexture` directly in the bake (the original isotropic plan), we'd be using `Σ bin / D²` as a stand-in for "incoming irradiance" — but bins include BACK-FACING directions and have no cosine weighting. The feedback gain would be ~2× too large (since back-bins contribute equal weight to forward-bins) and ignore surface normal entirely.

Per critic-03 H3, this would produce **muddy color bleed** (floor and back wall would receive the same indirect at the same point). **v1 must do directional sampling with normal awareness**, mirroring `sampleProbeDir`.

### v1 algorithm (directional feedback at bake hit)

In `raymarchSDF` at the surface-hit branch, port the display path's hemisphere integration into the bake. Sample the **previous frame's C0 atlas** (highest spatial resolution; same texture for all cascades per critic-03 M2):

```glsl
// Existing direct shading (unchanged)
vec3  lightDir = normalize(uLightPos - pos);
float shadowFact = ...;
float diff = max(dot(n, lightDir), 0.0) * (1.0 - shadowFact);
vec3 directColor = albedo * (diff * uLightColor + vec3(uAmbientBakeStrength));

// NEW: previous-frame indirect feedback (v1 — directional, cosine-weighted)
vec3 indirectColor = vec3(0.0);
if (uUseMultiBounce != 0 && uHasPrevFrame != 0) {
    // 1. Locate hit position in C0 probe grid (always C0, regardless of which
    //    cascade we're baking — per critic-03 M2 shared-feedback design).
    vec3 uvw = (pos - uGridOrigin) / uGridSize;
    if (all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0)))) {
        // 2. Trilinear-blend 8 surrounding C0 probes, each cosine-weighted-integrated
        //    over forward hemisphere around the hit normal.
        vec3 hemi = sampleC0AtlasIrradiance(uvw, n, uPrevFrameC0Atlas);
        // 3. Lambertian outgoing radiance: L_out = (albedo/π) × ∫ L_in × cos dω.
        //    sampleC0AtlasIrradiance returns "Σ wcos × a.a × bin / Σ wcos × a.a"
        //    which is the cosine-weighted hemisphere average. Multiplying by albedo
        //    gives the surface's outgoing Lambertian reflection (matches display path's
        //    `indirectColor = albedo × indirect` formula).
        indirectColor = albedo * hemi * uMultiBounceGain;

        // 4. History rejection clamp (per critic-03 M4): clamp feedback to local
        //    cascade-c0 neighborhood max to prevent dynamic-scene ghosting.
        //    Composes with existing temporal-α EMA fix.
        vec3 neighMax = sampleC0AtlasNeighborhoodMax(uvw);  // 3×3×3 sampling
        indirectColor = min(indirectColor, neighMax * 1.5);  // 1.5× allows headroom
    }
}

vec3 color = directColor + indirectColor;
return vec4(color, t);
```

**Helper `sampleC0AtlasIrradiance(uvw, n, atlas)`** (new function in radiance_3d.comp):
- Mirrors `sampleProbeDir` + `sampleDirectionalGI` from raymarch.frag (see [raymarch.frag](../../res/shaders/raymarch.frag) lines ~343-415)
- Trilinear-blend 8 C0 probe corners
- Each corner: sum forward-facing bins weighted by `wcos × a.a`
- Returns `irrad / max(wsum, 1e-4)` — cosine-weighted hemisphere average radiance

Cost per hit: ~8 × D² texture fetches = 128 at C0's D=4, or 512 if D=8 at higher cascades. At 32³ probes × 16 bins per cascade = 500k-2M hits per bake → ~64M-1G texture fetches. **~5-30 ms additional bake cost** depending on cascade res.

Acceptable: cascade bake is already ~16.5 ms; this adds proportionally. For early v1, restrict to C0 hits only (skip multi-bounce on higher-cascade bakes); v2 enables for all cascades.

### l-blending interaction (per critic-03 M3)

The bake's merge formula has three branches:
- `hit.a < 0` (sky exit): `rad = hit.rgb` — no upper cascade contribution
- `hit.a > 0` (surface hit): `rad = hit.rgb × l + upperDir × (1-l) × aFactor`
- `hit.a == 0` (miss in interval): `rad = upperDir.rgb` (or `× aFactor` per Phase 3)

Multi-bounce feedback is added to `hit.rgb` in the surface-hit branch. So:
- At `l == 1` (close hit): `rad = hit.rgb × 1 = direct + indirect_feedback` — multi-bounce works
- At `l == 0` (far hit, smoothstep zone): `rad = 0 + upperDir` — feedback gated out
- In smoothstep zone `0 < l < 1`: feedback partially blended

**v1 scope decision**: feedback ONLY applies at surface-hit bins in the smoothstep zone. Miss bins don't bounce off anything; sky bins are sky. This matches PT's behavior (PT bounce ray must hit a surface to add a bounce).

For miss bins, no feedback is added — `rad = upperDir.rgb` as before. The upper cascade itself will have multi-bounce baked in (recursive: upper baked first, then read by lower), so the miss-branch indirect IS multi-bounce via cascade chaining.

### Stability analysis (corrected per critic-03 H1)

`sampleC0AtlasIrradiance` returns cosine-weighted hemisphere average radiance `<L>_hemi+`. The recurrence is:

```
bake_radiance(x) = direct(x) + albedo(x) × <L>_hemi+(x) × gain
```

In equilibrium (assuming `<L>_hemi+` ≈ bake_radiance from the previous frame, locally):

```
bake_eq = direct + albedo × bake_eq × gain × hemi_factor
```

Where `hemi_factor` is the spatial coupling: probe at x integrates over surfaces at y; the ratio of "what's incoming at x" to "what's outgoing at y" depends on scene geometry. In a closed room (Cornell), `hemi_factor ≈ 1` (most flux stays in the room). In an open scene, `hemi_factor < 1` (flux escapes).

**Worst-case stability** (closed white-walled room):
- albedo = 0.9, gain = 1.0, hemi_factor = 1.0 → feedback = 0.9 < 1, stable
- Equilibrium: `bake_eq = direct / (1 - 0.9) = 10× direct`

That's a LOT of amplification — potentially physically correct for a 0.9-albedo closed room but visually shocking and may exceed `clamp(rad, 0, 100)` in the bake's sanitizeRadiance.

**Practical mitigations**:
1. **Default `gain = 0.7`** instead of 1.0 — caps worst-case at `direct / 0.37 = 2.7×`. Sacrifices physical correctness for visual stability.
2. **History rejection clamp** (per the algorithm above) — bounds feedback by local-neighborhood max.
3. **Per-color-channel `sanitizeRadiance` clamp at 100** — existing defense; will fire if amplification runs away.

Test on real scenes: Cornell-orig has ~0.5-0.7 effective albedo (mix of white/red/green), giving equilibrium ~1.5-1.7× direct. Matches PT@∞ observation (1.22× PT@1 ≈ 1.22 × cascade_direct_equiv).

### Convergence rate (recalibrated)

Per-frame error decay: `err_n+1 = err_n × albedo × gain × hemi_factor`. For Cornell-orig with effective 0.6 albedo × 0.7 gain ≈ 0.42 per frame:
- Frame 1: 58% of equilibrium
- Frame 5: 99% of equilibrium
- Frame 10: essentially converged

Fast enough for interactive use, modulo the `historyNeedsSeed` first-frame issue (see §4 invalidation).

### What about Phase 3 WeightedSample? (rev 2 — explicit verification)

Phase 3 v3 attenuates `(1-l) × upper` by `aFactor`. Multi-bounce adds to `hit.rgb`. The interaction at the merge formula:

```
rad = (hit.rgb_with_feedback) × l + upperDir × (1-l) × aFactor
    = (direct + indirect_feedback) × l + upperDir × (1-l) × aFactor
```

At `l=1`: `rad = direct + indirect_feedback` — both effects work
At `l=0`: `rad = upperDir × aFactor` — feedback gated out (Phase 3 active)
In smoothstep zone: linear blend

**Composition**: clean. Both effects work in their respective domains. No conflict. Multi-bounce + Phase 3 both ON should give the smallest cascade-vs-PT gap.

But: Phase 3 might attenuate UPPER's multi-bounce contribution at the merge (since upper's atlas also contains multi-bounce after this plan ships). That's geometrically appropriate (visibility rejection applies to all upper light, regardless of source). No fix needed; just verify in §6 validation.

---

## 4. Implementation (rev 2)

### 4.1 Shader changes (`radiance_3d.comp`)

Five new uniforms:
```glsl
uniform int       uUseMultiBounce;     // 0 default (off); 1 = enable feedback
uniform float     uMultiBounceGain;    // multiplier on feedback term; default 0.7 (was 1.0; per H1 stability fix)
uniform sampler3D uPrevFrameC0Atlas;   // C0's probeAtlasHistory (shared across cascades per M2)
uniform int       uHasPrevFrame;       // 0 = no history bound (first frame); 1 = ok to sample
uniform int       uPrevFrameC0DirRes;  // D for the C0 atlas (= cascadeDirRes[0])
```

Plus the C0 grid origin/size — but those equal `uGridOrigin`/`uGridSize` for shared volume, so no new uniforms needed.

In `raymarchSDF`, the surface-hit branch (current lines ~382-398), add ~25 lines for directional feedback per §3 algorithm.

New helper function (~30 lines, ported from raymarch.frag's sampleProbeDir + sampleDirectionalGI):
```glsl
vec3 sampleC0AtlasIrradiance(vec3 uvw, vec3 normal, sampler3D atlas, int D, ivec3 volumeRes);
```

Also need a small helper:
```glsl
vec3 sampleC0AtlasNeighborhoodMax(vec3 uvw, sampler3D atlas, ivec3 volumeRes);  // 3³ probe sample, channel-wise max
```

### 4.2 C++ binding (`demo3d.cpp`)

In `updateSingleCascade()`, before the dispatch — note: **always bind C0's history, regardless of which cascade is baking** (critic-03 M2 shared-feedback design):

```cpp
// Phase MB (multi-bounce): bind C0's previous-frame atlas history as feedback source.
// Shared across cascades for fastest convergence + best spatial resolution.
GLuint feedbackTex = (cascades[0].probeAtlasHistory != 0)
                     ? cascades[0].probeAtlasHistory
                     : cascades[0].probeAtlasTexture;  // first frame: read current (still valid; safer than seed-needed gate)
bool hasFeedback = useMultiBounce && feedbackTex != 0;
glUniform1i(glGetUniformLocation(prog, "uUseMultiBounce"), hasFeedback ? 1 : 0);
glUniform1f(glGetUniformLocation(prog, "uMultiBounceGain"), multiBounceGain);
glUniform1i(glGetUniformLocation(prog, "uHasPrevFrame"),    hasFeedback ? 1 : 0);
glUniform1i(glGetUniformLocation(prog, "uPrevFrameC0DirRes"), cascadeDirRes[0]);
if (hasFeedback) {
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, feedbackTex);
    glUniform1i(glGetUniformLocation(prog, "uPrevFrameC0Atlas"), 5);
}
```

**No `historyNeedsSeed` gate** (per critic-03 M1 redesign): on first frame, history is current atlas (still valid, zero-initialized → feedback = 0 → graceful degradation to single-bounce). After 1 bake, history has direct content → feedback kicks in. After ~5 frames, equilibrium.

### 4.3 New state members (`demo3d.h`)

```cpp
bool     useMultiBounce;       // default false (opt-in)
float    multiBounceGain;      // default 0.7; gain<1 for safety per critic-03 H1
```

Setters trigger cascade rebake (atlas content depends on this toggle).

### 4.3 New state members (`demo3d.h`)

```cpp
bool     useMultiBounce;       // default false (opt-in)
float    multiBounceGain;      // default 1.0; clamp safety
```

Setters trigger cascade rebake (atlas content depends on this toggle).

### 4.4 GUI (`demo3d.cpp` cascade panel)

In the "Hierarchy & Merge" panel, add (likely below WeightedSample checkbox):

```cpp
ImGui::Checkbox("Temporal multi-bounce (feedback indirect)", &useMultiBounce);
imHelpMarker(
    "Adds previous-frame atlas at bake hit positions to capture multi-bounce indirect.\n"
    "OFF (default): single-bounce only (current behavior, bit-exact preserved).\n"
    "ON: each frame's bake includes previous frame's indirect → converges to multi-bounce.\n"
    "Convergence: ~5-10 frames after camera/scene stabilizes.\n"
    "Stability: gain < 1/(albedo × hemisphere) = ~1/0.5 = 2 max for white walls; default 1.0 is safe.\n"
    "Measured cornell-orig gain vs PT-reference: ~7-10% brightness increase (closes part of the 42% gap).");

if (useMultiBounce) {
    ImGui::SliderFloat("Multi-bounce gain", &multiBounceGain, 0.0f, 2.0f, "%.2f");
}
```

### 4.5 CLI flags (`main3d.cpp`)

```cpp
--use-multi-bounce=N      // 0 default OFF, 1 ON
--multi-bounce-gain=F     // default 1.0
```

### 4.6 Invalidation

Changing `useMultiBounce` or `multiBounceGain` resets:
- `cascadeReady = false` (atlas content depends on toggle)
- `historyNeedsSeed = true` (rebake from a clean state)
- `renderFrameIndex = 0`

---

## 5. Sequencing (~3-4 days for v1; rev 2 per critic-03 cross-cutting)

### Day 1 — v0.5 prototype (cheap, measurable)

- **Minimum-viable feedback**: directional sampling from C0's history at hit pos, no neighborhood clamp, gain=0.7 hard-coded.
- Goal: get a single brightness number for "is multi-bounce contribution material?"
- Build + smoke test on cornell-orig.
- **Measurement** (per critic-03 L1 prototype-first concern): A/B vs PT@2, PT@∞, capture brightness ratios.
- **Decision**: if cascade-vs-PT gap closes >5%, proceed to Day 2-3 hardening. If <2%, the integration loss dominates more than expected and multi-bounce isn't worth the complexity.

### Day 2 — Hardening (algorithm cleanup, neighborhood clamp, GUI)

- Add `sampleC0AtlasNeighborhoodMax` for history-rejection clamp (per critic-03 M4).
- C++: state members + setters + 4 CLI flags.
- GUI: checkbox + gain slider + tooltips that reference critic-03 M3 l-blending behavior.
- Verify OFF mode bit-exact (mode 0 brightness unchanged on cornell-orig).

### Day 3 — Validation + Phase 3 composition

- Three-way A/B: cascade (off) / cascade-mb / PT-cascade-match — measure with `tools/compare_cascade_pt.py` (needs writing — critic-03 L3).
- Test Phase 3 + multi-bounce composition: both ON should give smallest cascade-vs-PT gap.
- Verify temporal stability (consecutive-frame RMSE with mode 15 oscillation heatmap).
- Test convergence: capture frames 1/5/10/30 — confirm convergence by frame 10.
- Test history-rejection clamp on dynamic-light scenario (turn light on/off; verify no ghosting > 2-3 frames).

### Day 4 — Buffer / iteration

Reserved for surprises (cf. Phase 7 H2 bbox-intersect surprise — algorithm contracts often have hidden assumptions).

If quality insufficient → write doc explaining what was tried and what didn't work; don't commit a half-broken multi-bounce.

---

## 6. Validation

### Primary metric: PT-RMSE delta

Before this plan: cascade vs PT cascade-match RMSE = 0.34, brightness ratio 0.575×.

Expected after multi-bounce ON: brightness ratio improves to ~0.62-0.65× (closes 7-10% of gap). RMSE drops marginally.

Workflow (reuses Phase 7 infrastructure):
```
build/RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=16 \
    --pt-cascade-match=1 --pt-max-bounces=8 --pt-rays-per-frame=4 \
    --screenshot=tools/pt_match_truth.png --exit-frames=2000

build/RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=0 \
    --use-multi-bounce=1 --screenshot=tools/cascade_mb_on.png --exit-frames=300

build/RadianceCascades3D.exe --load-obj=cornell-orig --render-mode=0 \
    --use-multi-bounce=0 --screenshot=tools/cascade_mb_off.png --exit-frames=300

python tools/compare_cascade_pt.py \
    cascade_mb_off.png cascade_mb_on.png pt_match_truth.png
```

Reports per-region RMSE + brightness ratio for both cascade modes vs PT truth.

### Sanity tests

- **Bit-exact OFF**: mode 0 with `--use-multi-bounce=0` produces same image as before this commit.
- **Brightness increase ON**: mode 0 with `--use-multi-bounce=1` brighter than OFF, especially on white walls.
- **Convergence**: capture frames 1, 5, 10, 30 with `--use-multi-bounce=1` — should converge by ~10 frames.
- **No flicker**: 10 consecutive frames with mode 0 + multi-bounce ON show RMSE < 0.001 per pair (no temporal instability introduced).
- **Phase 3 compose**: multi-bounce ON + WeightedSample ON should give the smallest cascade-vs-PT gap.

---

## 7. Risks

### Functional risks

- **Energy amplification on extreme scenes**: if user sets light intensity 10× or scene has retro-reflective regions (albedo > 1), feedback could grow unbounded. `multiBounceGain` slider provides clamping; default 1.0 is safe for sane inputs.
- **Bake-leak amplification**: previous-frame atlas has bake-side leaks (per critic-15). Multi-bounce feedback would amplify them. May need to compose with Phase 3 WeightedSample for cleanest result.
- **Convergence delay on camera move**: each camera move resets history → multi-bounce needs 5-10 frames to reconverge. May look "flat" briefly during motion. Acceptable for interactive use.

### Architectural risks

- **Coupling to temporal accumulation**: feedback uses `probeGridHistory`. If user toggles temporal OFF, history is empty → multi-bounce silently degrades to single-bounce. Acceptable; document the dependency.
- **Coupling to cascade staggering**: with stagger=8, C3 only updates every 8 frames. Multi-bounce in C3 would use 8-frame-old data for "previous frame." May cause low-frequency oscillation; verify experimentally.

### Quality risks

- **Isotropic feedback may dim/saturate**: average irradiance lookup doesn't account for surface normal. May give same answer for floor and ceiling at the same point (subtly wrong). v2 directional feedback fixes this.
- **PT comparison still has 38% integration gap**: multi-bounce closes ~7-10% of the gap. The remaining 28-31% requires separate work.

---

## 8. Open questions

1. **Use `probeGridHistory` or `probeAtlasHistory`?** v1 uses grid (isotropic, cheap). v2 should consider atlas (directional, costly).
2. **Per-cascade or shared feedback?** v1: each cascade samples its OWN history. Alternative: all cascades sample C0's history (highest resolution).
3. **Should `multiBounceGain` clamp to 1/(albedo × cosine_factor)`** automatically rather than being user-controlled? Adds complexity; user-controlled is simpler.
4. **Should Phase 3 WeightedSample composition be the default?** If multi-bounce ON without Phase 3 amplifies leaks, may want to auto-enable Phase 3.
5. **Should multi-bounce default to ON?** It's almost always desirable physically. But changes existing behavior; users may have tuned other knobs to compensate for missing multi-bounce. Default OFF preserves backward compatibility; flip default ON after a few rounds of user feedback.

---

## 9. Success criteria (rev 2)

### v0.5 prototype gate (Day 1)

- ☐ Build clean
- ☐ With multi-bounce ON, cascade brightness measurably increases over ~5 frames (visual confirmation)
- ☐ Brightness ratio cascade/(PT cascade-match) improves by ≥ 5% (e.g., 0.575 → ≥ 0.60)
- ☐ No NaN / Inf / runaway amplification (clamp check)

**If v0.5 fails any of these → write up findings, stop.** Don't sink Day 2-3 into a feature that doesn't deliver.

### v1 ship gate (Day 3)

- ☐ All v0.5 criteria still pass
- ☐ Default OFF preserves bit-exact pre-commit behavior (mode 0 brightness on cornell-orig matches baseline)
- ☐ Color bleed visibly directional (red wall's tint on floor in Cornell, not muddied to gray — confirms directional feedback works)
- ☐ Temporal stability preserved (consecutive-frame RMSE < 0.001 with multi-bounce ON, per mode 15 heatmap)
- ☐ Phase 3 + multi-bounce compose cleanly (no over-amplified leaks visible in mode 14 heatmap)
- ☐ Convergence ≤ 15 frames after camera stop
- ☐ History-rejection clamp prevents ghosting on dynamic-light test (≤ 3 frame after-image)
- ☐ GUI toggle + slider functional; 2 CLI flags work
- ☐ `tools/compare_cascade_pt.py` lands
- ☐ Documentation lands in `doc/7/multi_bounce_temporal_impl.md`

### v2 future (NOT v1)

- Cascade-tail feedback (currently v1 might restrict to C0-hit bins to control cost; v2 enables for all cascades)
- Higher-order temporal feedback (multi-stage history for smoother convergence)
- Adaptive gain (luminance-aware clamping vs fixed gain)

---

## 10. What this unlocks

- **Quantifiable quality improvement**: first time the cascade renderer's quality changes can be measured against PT truth with a concrete number.
- **Path to closing the rest of the 38% integration gap**: once multi-bounce is in, the remaining quality gap is purely "integration approximation" — investigations can target specific approximations (bin count, smoothstep, leak gating) with PT-RMSE as the success metric.
- **Realism for white-walled scenes**: Cornell box and similar high-albedo interiors get significantly more correct lighting.
