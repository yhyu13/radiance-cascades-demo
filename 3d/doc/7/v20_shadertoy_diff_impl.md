## v20 ShaderToy diff — per-delta source-of-truth

**Stage 0 Deliverable A** of M0 ([v3_m0_stage0_plan.md](v3_m0_stage0_plan.md)).
**Date:** 2026-05-26.
**Purpose:** single source-of-truth for the 7 ShaderToy→current deltas. Each M1 per-delta impl doc links to its entry below instead of re-deriving the diff.

**Per-entry structure:** ShaderToy code paste → current-impl code paste → semantic diff paragraph → topology dependency tag → port disposition.

**Topology tag legend:**
- ✓ portable — works in volumetric 3D grid without re-derivation
- ⚠ portable-with-redefinition — concept transfers but the volumetric formulation is structurally different from the literal ShaderToy code
- ✗ requires surface-attached — depends on gTan/gBit/gNor/gPos that volumetric doesn't have
- **[→#N]** sequencing dependency — this delta must follow Delta #N (cannot be tested independently)

**Status snapshot:**

| # | Name | Status | Topology | M1 work |
|---|------|--------|----------|---------|
| 1 | Bin-center direction in consumer | LANDED v2.0-postfix | ✓ | — |
| 2 | -0.5 center-aligned offset in consumer | LANDED v2.0-postfix | ✓ | — |
| 3 | Per-corner visibility-weighted merge | NOT STARTED | ⚠ | M1 (redefined; see C+ audit). Bundles #6. |
| 4 | Multi-bounce 4-cube-read average | NOT STARTED | ✓ | M1 (formulation-comparative; see Delta #4) |
| 5 | Bake-time cosine + ΔΩ pre-weighting | NOT STARTED | ✗ | Path B only (M2) |
| 6 | WeightedSample θ-of-ray vs θ-of-bin | NOT STARTED | ⚠ [→#3] | M1 (bundled with #3 in 2×2 A/B; see Delta #6) |
| 7 | Probe-position -0.5 offset | CONFORMANT | ✓ | — (removed per C audit) |

---

### Delta #1 — Bin-center direction in consumer

**Status:** LANDED in v2.0-postfix.

**ShaderToy source:** [shader_toy/CubeA.glsl:147-148](../../shader_toy/CubeA.glsl#L147-L148) — `probeDir` is computed from per-bin spherical coords (probeTheta, probePhi) that index the bin center.

**Current impl mirror:** [res/shaders/raymarch.frag:345-348](../../res/shaders/raymarch.frag#L345-L348):
```glsl
vec3 binToDir(ivec2 bin, int D) {
    return octToDir((vec2(bin) + 0.5) / float(D));
}
```

**Semantic diff:** Pre-v2.0-postfix, the consumer reconstructed ray direction from `(vec2(bin) / D)` without the +0.5 offset — i.e., from the bin's lower-left corner rather than center. This produced a half-bin angular bias between bake-time `binToDir(bin)+0.5` (already centered) and consume-time `binToDir(bin)` (not centered). The postfix added +0.5 to consume-side too. Same convention as Delta #7 (spatial center-alignment), applied to the directional axis.

**Topology dependency:** ✓ portable. Octahedral mapping works identically in volumetric and surface-attached layouts.

**Port disposition:** Already landed. No M1 work. Documented here for completeness.

---

### Delta #2 — -0.5 center-aligned offset for consumer trilinear/bilinear

**Status:** LANDED in v2.0-postfix.

**ShaderToy source:** Implicit throughout — ShaderToy uses `(probeUV - 0.5)` style center alignment for all probe-grid sampling (e.g., [CubeA.glsl:206](../../shader_toy/CubeA.glsl#L206) `flPUVPos = floor(lPUVPos - 0.5) + 0.5`).

**Current impl mirror:** [res/shaders/raymarch.frag:437](../../res/shaders/raymarch.frag#L437) (Phase 5d trilinear consumer):
```glsl
vec3 pg = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5,
                vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
```
And [raymarch.frag:628](../../res/shaders/raymarch.frag#L628) (Phase 5f bilinear consumer, mode 5).

**Semantic diff:** Pre-v2.0-postfix, the consumer's spatial blend sampled at probe corners instead of probe centers, producing a half-cell spatial bias. The postfix added -0.5 to invert the +0.5 cell-center offset used at bake time. Together with Delta #7's bake-side +0.5 (already conformant — see [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md)), this restores center-to-center correspondence between bake writes and consumer reads.

**Topology dependency:** ✓ portable. Texel-center convention is mesh-independent.

**Port disposition:** Already landed. No M1 work.

---

### Delta #3 — Per-corner visibility-weighted bilinear merge

**Status:** NOT STARTED. **The scope's original framing ("skip rgb when α=0") is wrong** — see [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) (Deliverable C+). The α conventions are inverted between implementations, so a literal port would discard all surface + sky radiance.

**Audit context:** The C+ audit evaluated three possible outcomes — Case A: `upperDir.rgb` already zero when α=0 → #3 is a no-op, drop from M1; Case B: `upperDir.rgb` has semantic content (sky/miss) → #3 requires volumetric analog definition before any code change; Case C: `upperDir.rgb` is nonzero garbage → #3 ports naively as "skip rgb on α=0." **Case B was found** — the most complex outcome, requiring structural per-corner gating rather than a threshold test. The other two cases would have produced much smaller M1 budgets (zero work and ~1 line respectively).

**ShaderToy source:** [shader_toy/CubeA.glsl:21-42](../../shader_toy/CubeA.glsl#L21-L42) (WeightedSample) and [CubeA.glsl:196-219](../../shader_toy/CubeA.glsl#L196-L219) (merge):
```glsl
vec4 S0 = WeightedSample(...);  // vec4(rgb, 1.) on visible, vec4(0.) on reject
vec4 S1 = WeightedSample(...);
vec4 S2 = WeightedSample(...);
vec4 S3 = WeightedSample(...);
vec3 lastOutput = mix(mix(S0.xyz, S1.xyz, fx), mix(S2.xyz, S3.xyz, fx), fy)
                  / max(0.01, mix(mix(S0.w, S1.w, fx), mix(S2.w, S3.w, fx), fy));
if (!isnan(lastOutput.x))
    Output.xyz = Output.xyz * l + lastOutput * (1. - l);
```
The mechanism: a rejected corner contributes 0 to BOTH numerator (.xyz) AND denominator (.w). The weighted-bilinear denominator renormalizes over surviving corners.

**Current impl mirror:** [res/shaders/radiance_3d.comp:660-687](../../res/shaders/radiance_3d.comp#L660-L687) and the merge at [radiance_3d.comp:760-781](../../res/shaders/radiance_3d.comp#L760-L781):
```glsl
upperDirTrilinear = sampleUpperDirTrilinear(triP000, triF, rayDir, uUpperDirRes);
if (uUseWeightedSample != 0) {
    vec4 ws = sampleUpperDirWeighted(triP000, triF, rayDir, uUpperDirRes, worldPos);
    upperDir = vec4(upperDirTrilinear.rgb, ws.a);  // .rgb = full average; .a = visible fraction
}
// ...
float aFactor = (uUseWeightedSample != 0) ? upperDir.a : 1.0;
rad = hit.rgb * l + upperDir.rgb * (1.0 - l) * aFactor * uGIStrength;
```
The current impl computes `.a` as a **scalar visibility fraction** (`wVisible / wTotalSpatial`, [radiance_3d.comp:342](../../res/shaders/radiance_3d.comp#L342)) and applies it as a **uniform attenuator** on a full 8-corner trilinear .rgb sum. Rejected corners still contribute their .rgb to the numerator at full weight; rejection only damps the overall sum.

**Semantic diff:** ShaderToy gates per-corner contribution to both numerator and denominator → rejected corner truly disappears from the merge → surviving corners reweight automatically. Current impl uses scalar aFactor uniform attenuation → all corners contribute even when rejected → the entire sum is damped instead of selectively reweighted. **These differ substantially when corners disagree** (one corner near a lit wall while three see far field): ShaderToy can discard the lit corner cleanly if rejected; current impl smears the lit corner through the attenuation.

**Topology dependency:** ⚠ portable-with-redefinition. The per-corner gating concept transfers to 3D, but the trilinear merge in volumetric has 8 corners (not 4) and the .w renormalization is over a trilinear denominator rather than bilinear. Implementation requires changes to both `sampleUpperDirTrilinear` ([radiance_3d.comp:229-266](../../res/shaders/radiance_3d.comp#L229-L266) — make per-corner output gated) and the merge call site ([radiance_3d.comp:660-687](../../res/shaders/radiance_3d.comp#L660-L687) — bundle ShaderToy's normalization into the trilinear or split returns).

**Volumetric analog sketch.** The ShaderToy 4-corner bilinear becomes an 8-corner trilinear in volumetric. Per-corner gating formula:

```glsl
// Pseudocode for the new sampleUpperDirTrilinearGated.
// triF in [0,1]^3 is the trilinear fractional position; triP000 is the base corner.
vec3  sumRgb = vec3(0.0);
float sumW   = 0.0;
for (int i = 0; i < 8; ++i) {
    ivec3 cornerPos = triP000 + offsets[i];
    float wTri      = triWeight(triF, offsets[i]);   // (1-fx|fx) * (1-fy|fy) * (1-fz|fz)
    bool  visible   = perCornerVisibilityTest(cornerPos, rayDir, lowerProbeWorld);
    if (visible) {
        vec3 cornerRgb = sampleUpperDir(cornerPos, rayDir, Du).rgb;
        sumRgb += cornerRgb * wTri;       // rejected corner contributes 0 to numerator
        sumW   += wTri;                   // AND 0 to denominator
    }
}
vec3 result = (sumW > 0.01) ? (sumRgb / sumW) : vec3(0.0);  // NaN guard, same role as ShaderToy `if (!isnan(...))`
```

Two return-shape options:
- **Option A — fused:** return `vec4(result, sumW/Σw_tri)` where `.a` is the visibility fraction (≈ what current `sampleUpperDirWeighted` returns, but applied as a numerator-AND-denominator gate, not a scalar attenuator). Merge call site uses `.rgb` directly (already correctly normalized) and uses `.a` only as a confidence indicator.
- **Option B — split:** return per-corner arrays `(rgb[8], visible[8])` and let the merge call site do the normalization. More flexible but verbose.

**Recommendation: Option A** — keeps signature compatible with existing call sites and bundles normalization in one place. Option B reserved if Delta #6's cone-derivation refactor requires per-corner visibility access at the call site.

**Port disposition:** M1 work. **Estimated effort: 1-2 sessions** (was budgeted at <1 session in scope §3; revise upward). M1 impl doc must front-load the C+ audit findings and define the volumetric analog before any code lands. Sponza-relevant: per-corner gating likely matters more for complex occlusion than for cornell.

---

### Delta #4 — Multi-bounce 4-cube-read average (bounce light from cubemap)

**Status:** NOT STARTED.

**ShaderToy source:** [shader_toy/CubeA.glsl:166-170](../../shader_toy/CubeA.glsl#L166-L170):
```glsl
//Bounce light
vec2 suv = clamp(rayHit.uv*128., vec2(0.5), rayHit.res*0.5 - 0.5) + rayHit.uvo;
Output.xyz = TextureCube(suv, 0.).xyz + TextureCube(suv + vec2(rayHit.res.x*0.5, 0.), 0.).xyz +
             TextureCube(suv + vec2(0., rayHit.res.y*0.5), 0.).xyz + TextureCube(suv + rayHit.res*0.5, 0.).xyz;
```
At each surface hit during bake, ShaderToy reads the previous-frame cubemap at the hit location across **4 spatially-offset taps** (hit point + 3 corner-offset positions), summing them as the multi-bounce incoming radiance. This is an explicit 4-sample spatial average over the bounced-light surface neighborhood.

**Current impl mirror:** [res/shaders/radiance_3d.comp:479-493](../../res/shaders/radiance_3d.comp#L479-L493) (sampleC0AtlasStochastic):
```glsl
vec3 sampleC0AtlasStochastic(vec3 pos, vec3 normal, uint baseSeed) {
    if (uHasPrevFrame == 0 || uPrevFrameC0DirRes <= 0) return vec3(0.0);
    uint rng = mbHash(baseSeed ^ floatBitsToUint(pos.x) ^ ...);
    vec2 r2  = mbRand2(rng);
    vec3 dir = mbCosineSample(normal, r2);
    int   D  = uPrevFrameC0DirRes;
    ivec2 bin = dirToBin(dir, D);
    return sampleC0AtlasOneBin(pos, bin, D);  // 8-corner trilinear at ONE bin
}
```
The current impl uses **stochastic single-bin Monte Carlo** with cosine-weighted random direction sampling at the surface normal — one bin, decorrelated per-pixel by RNG. The 8-corner trilinear in `sampleC0AtlasOneBin` is spatial (over probes), not directional (over bins).

**Semantic diff:** ShaderToy averages 4 spatially-near taps of the bounced cubemap (no directional choice — the cubemap IS the hemisphere, indexed by surface position). Current impl samples 1 directional bin via MC with cosine PDF, trilinear-interpolated across 8 probes. **The two approaches discretize the same `∫ L dω` integral differently:** ShaderToy averages 4 spatial samples per bake-ray (deterministic, low variance per ray, but spatial blur); current MC samples 1 direction per ray with high per-ray variance, relying on temporal accumulation to converge. Numerically: ShaderToy's 4-tap is a "wider acceptance" of multi-bounce energy per hit; current MC is "narrower" per ray but accumulates more rays.

**Topology dependency:** ✓ portable. The current impl's MC formulation IS the volumetric analog of ShaderToy's spatial 4-tap (just a different discretization choice). Port option is to replace MC with deterministic 4-bin spatial average around the hit normal — but this loses MC's PDF correctness and temporal decorrelation benefits.

**Port disposition:** M1 work, but **the port shape is "test whether deterministic-N-sample replaces stochastic-1-sample favorably"** — not a literal "do what ShaderToy does." A/B should measure variance vs bias trade-off on cornell/sponza. **Recommendation:** keep MC formulation, add an optional N-tap deterministic mode behind a flag, A/B at M1 Stage 1.

**Formulation-comparative note (gate semantics).** This delta's A/B is **formulation-comparative, not mechanism-additive** — the current impl already has an MB formulation (MC); the question is "which formulation is better," not "does the missing mechanism close the gap." The STRONG/MARGINAL/DEAD bands apply to the deterministic-N-sample mode *relative to MC baseline*:
- **Deterministic STRONG** → replace MC default with deterministic.
- **MC wins** → keep MC default, remove the flag — #4 is marked "verified-equivalent-or-better" rather than "ported." (Not a failed port — the comparative experiment was the point.)
- **MARGINAL** → neither dominates; both modes carry similar metric outcomes.

**Post-A/B disposition.** Flags accumulate quickly if not retired. After the A/B verdict:
- **Deterministic STRONG:** MC mode removed in one cleanup session (flag deleted, `sampleC0AtlasStochastic` simplified to deterministic form).
- **MC wins:** flag removed, deterministic-mode code deleted.
- **MARGINAL:** both modes kept for one additional session of investigation (e.g., per-scene split — one wins on cornell, the other on sponza); if unresolved, default to MC (current default, cleaner code) and delete the flag. Avoid permanent dual-mode maintenance overhead.

---

### Delta #5 — Bake-time cosine + ΔΩ pre-weighting (hemisphere normalization)

**Status:** NOT STARTED. **Path B only** (see scope §1.1).

**ShaderToy source:** [shader_toy/CubeA.glsl:189-192](../../shader_toy/CubeA.glsl#L189-L192):
```glsl
//Hemisphere normalized area and BRDF
Output.xyz *= (cos(probeTheta - 3.141592653/probeSize) -
               cos(probeTheta + 3.141592653/probeSize))/(4. + 8.*floor(probeThetai));
Output.xyz *= cos(probeTheta); //Diffuse
```
At bake time, each bin's stored radiance is pre-multiplied by **(a)** the bin's solid-angle area `(cos(θ-Δθ/2) - cos(θ+Δθ/2))/Nφ_bins` and **(b)** the Lambertian cosine `cos(θ)` where θ is the bin-center elevation angle relative to the surface normal `gNor`. The atlas stores `L · cos(θ) · ΔΩ` per bin.

**Current impl mirror:** No bake-time analog. Cosine is applied at consume-time in [res/shaders/raymarch.frag:395-420](../../res/shaders/raymarch.frag#L395-L420) (sampleProbeDir):
```glsl
for (int dy = 0; dy < D; ++dy) {
    for (int dx = 0; dx < D; ++dx) {
        vec3  bdir = binToDir(ivec2(dx, dy), D);
        float wcos = max(0.0, dot(bdir, normal));
        vec4  a    = texelFetch(uDirectionalAtlas, ..., 0);
        float w    = wcos * a.a;
        irrad   += a.rgb * w;
        wsum    += w;
        ...
    }
}
// irrad = irrad / max(wsum, 1e-4);
```
Atlas stores `L` per bin (no cosine, no ΔΩ). Consumer does cosine-weighted normalize-over-visible sum at integration time.

**Semantic diff:** Both formulations compute the same Lambertian integral `∫ L · cos⁺ dω` discretized differently — ShaderToy evaluates cosine at bin centers (bake-time, exact for bin center), current impl evaluates cosine per consume ray (consume-time, exact for the actual ray direction). For dirRes=8, bin solid angle is ~π/16 sr ≈ 25° angular width — both are O(angular_width²)-bounded discretizations of the same integral. The deeper difference is that ShaderToy's bake assumes a **surface-attached topology** (the cosine is well-defined because the probe has a normal `gNor`); volumetric probes lack a canonical normal at bake time, so the cosine cannot be pre-computed.

**Key insight (from [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md)):** both formulations evaluate `cos(θ)` at the SAME point — the bin center θ_b — because neither impl subdivides bins. The numerical difference is therefore a **uniform scale shift bounded by < 3%** (D=8 Jensen bound on `sin(h)/h` with h=π/16), not a distribution-shape change. This is why #5 is "small magnitude leverage" regardless of topology — the bake-vs-consume choice is a storage refactor, not a numerical lever.

**Topology dependency:** ✗ requires surface-attached. The bake-time cosine needs `gNor` at the probe; volumetric probes have no normal. A volumetric "port" would have to defer cosine to consume-time anyway (which is what current does), so Delta #5 has no portable form in Path A. Path B's leverage on #5 is the **topology** (hemisphere-only sampling above gNor, fewer ill-conditioned grazing rays), not the bake-time pre-weighting per se.

**Port disposition:** **Path A: skip.** **Path B: integral to the rewrite** (surface-attached requires it). M0 Stage 0 Deliverable B (algebraic estimate) predicts #5 itself has small magnitude leverage; #5's value is the topology change that makes it tractable.

**Path B mechanism candidates (deferred to M2 scope).** The ceiling estimate ruled out #5 itself as a magnitude lever, which raises the strategic question: if Path B's value isn't #5, what closes the gap under M1_PARTIAL_MAGNITUDE? Three Path B mechanisms beyond #5 are hypothesized but **unquantified** — magnitude estimates require Path B prototyping that is out of scope for M0 Stage 0:

| Mechanism | Hypothesis | Magnitude |
|-----------|------------|-----------|
| Hemisphere-only sampling (above gNor) | Eliminates grazing-angle bins that produce low-cosine but high-energy artifacts in the volumetric full-sphere bake. | Unknown — likely small (these bins are also down-weighted by cos⁺ in current). |
| Per-corner gating under surface-attached topology | Rejected corners gain clearer geometric meaning (ray hit own surface → reject) vs volumetric (ray hit nothing → unclear). May amplify Delta #3's effectiveness. | Unknown — possibly multiplicative with #3. |
| Surface-aware probe placement | Probes concentrated on surfaces where GI matters; fewer wasted volume-probes in empty space. | Unknown — significant for sparse scenes (Sponza atrium), small for filled scenes (Cornell). |

These belong in a future **M2 Path B scope doc** with prototyping-derived magnitude estimates. v20 lists them so M1_PARTIAL_MAGNITUDE evaluation has a defined next action (M2 Stage 0 = mechanism scoping + prototyping plan), not so they bias Stage 0 verdicts.

---

### Delta #6 — WeightedSample θ-of-ray vs θ-of-bin

**Status:** NOT STARTED. Conditional on Delta #3 outcome.

**ShaderToy source:** [shader_toy/CubeA.glsl:21-42](../../shader_toy/CubeA.glsl#L21-L42) (WeightedSample):
```glsl
vec3 relVec = probePos - lastProbePos;
float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*3.141592653*0.5;
float phi = atan(-dot(relVec, gTan), -dot(relVec, gBit));
// ... lookup atlas at bin indexed by phi ...
float lProbeRayDist = TextureCube(luvo + floor(phiUV)*uvo + luvp).w;
if (lProbeRayDist < -0.5 || length(relVec) < lProbeRayDist*cos(3.141592653*0.5 - theta) + 0.01) {
    // visible — return sample
}
```
The visibility cone half-angle `theta` is computed from **the probe-size geometry**: it represents the angular extent of the lower probe as seen from the upper, derived purely from the probe-size index. The cone is "centered on the look-back ray direction" (which is reconstructed from probe-relative geometry, not from the bin's nominal direction).

**Current impl mirror:** [res/shaders/radiance_3d.comp:283-344](../../res/shaders/radiance_3d.comp#L283-L344) (sampleUpperDirWeighted):
```glsl
// Look-back bin: read ONLY the stored hit-distance (.a) ...
ivec2 lookBackBin = dirToBin(dirToLower, Du);
float lProbeRayDist = texelFetch(uUpperCascadeAtlas,
    ivec3(cornerPos.x * Du + lookBackBin.x, ...), 0).a;

bool visible = (lProbeRayDist < 0.0)
            || (length(relVec) < lProbeRayDist * uUpperBinConeSin + 0.01);
```
The cone half-angle is `uUpperBinConeSin` — a **uniform** representing the angular width of one bin in the upper cascade's directional resolution. The current impl mixes "look-back bin direction" (per-corner geometric reconstruction) with "bin-width cone" (uniform, bin-discretization-derived).

**Structural diff:** ShaderToy's cone is **geometric** — derived from the probe-size index, representing the angular extent of the lower probe as seen from the upper. Current impl's cone is **discretization-derived** — the angular width of one upper-cascade bin (octahedral average per [demo3d.cpp:2461-2463](../../src/demo3d.cpp#L2461-L2463)). These measure fundamentally different angular extents and are not interchangeable without re-derivation.

**Numerical diff:** ShaderToy `sin(theta)` ≈ 0.92 for probeSize=4 (cascade 1), → 1.0 for larger cascades. Current `uUpperBinConeSin` ≈ 0.248 for D=8. **Current's cone is ~4× tighter** — it rejects more aggressively than ShaderToy. (Whether this is a correctness concern in volumetric is unclear; see Port considerations.)

**Port considerations:** ⚠ portable-with-redefinition (also [→#3] sequencing). The "probe apparent angular extent" concept transfers from surface-attached to volumetric, but the "lower probe size" interpretation must be replaced by "lower probe cell size" + a careful re-derivation of the look-back geometry (the visibility cone's geometric meaning differs in volumetric where the probe is in empty space, not attached to a surface). The current impl's cone size is a v1 octahedral-average; per [visibility_phase3_plan.md §3.6](../../doc/6/claude_plan/visibility_phase3_plan.md), v2 fallback (per-bin LUT) is queued if v1 fails. This delta is therefore **"cone-derivation refactor + tuning sweep,"** not a single-line code change. Whether widening the current cone (toward ShaderToy's permissive 0.92+) improves results depends on Delta #3's per-corner gating being in place — without #3, a wider cone in current's scalar-attenuation merge just smears MORE radiance through the uniform aFactor.

**Port disposition:** M1 work, **conditional on Delta #3 landing first.** Per scope §3 M1 work order: re-evaluate #6 after #3's A/B — if #3 is STRONG, #6 piggybacks naturally; if #3 is DEAD, #6 may be standalone-marginal. **Recommendation:** bundle #6 with #3 in the same M1 impl doc; A/B both ON/OFF combinations (#3 alone, #6 alone, both).

**Bundling trade-off (critic-07 I2 acknowledgment).** Bundling #3 + #6 in one impl doc trades **per-delta isolation** (scope rule #8) for shared-A/B-harness efficiency. The 2×2 A/B matrix:

| Condition | #3 state | #6 state |
|-----------|----------|----------|
| Baseline  | OFF      | OFF      |
| #3 alone  | ON       | OFF      |
| #6 alone  | OFF      | ON       |
| Both      | ON       | ON       |

requires 4 capture configs per scene/N and produces two deltas' gates from one impl doc — attribution is partially contaminated. The bundling is justified because #6 alone is hypothesized DEAD without #3 (see Port considerations above), so the shared harness saves a session that would otherwise be wasted on a likely-DEAD standalone #6 A/B.

**Drop-rule:** if the 2×2 matrix shows #6 alone is DEAD (no measurable delta vs baseline), **#6 is dropped regardless of #3's outcome.** This protects against carrying a contaminated #6 verdict forward; #6 only proceeds to land if it shows independent value or measurable amplification of #3.

**Correctness criterion (DNR #4 analog).** A STRONG verdict for #6 requires **both** metric improvement AND a principled geometric justification for the chosen cone size. "The cone matches the volumetric cell's apparent angular extent" is principled; "we widened it until metrics improved" is not. This echoes [DNR #4](v3_shadertoy_adoption_scope.md) ("no more merge-formula reshapes targeting bright-tail isolation") — the mechanism must be correct, not just effective. v2.4.b (output-side luminance clamp) was killed for failing this criterion; #6 must not repeat it.

---

### Delta #7 — Probe-position -0.5 / +0.5 offset

**Status:** **CONFORMANT.** Removed from M1 work order per [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md) (Deliverable C).

**ShaderToy source:** Throughout — e.g., [CubeA.glsl:132](../../shader_toy/CubeA.glsl#L132) `vec2 probeUV = floor(modUV/probePositions) + 0.5;` (+0.5 to probe index for center) and [CubeA.glsl:206](../../shader_toy/CubeA.glsl#L206) `flPUVPos = floor(lPUVPos - 0.5) + 0.5;` (-0.5 for center-aligned sampling).

**Current impl mirror:** All sites use the same convention (see audit doc for the full table). Authoritative comment at [radiance_3d.comp:599-600](../../res/shaders/radiance_3d.comp#L599-L600).

**Semantic diff:** None. Bake-side uses +0.5 to derive cell-center world positions; consume-side uses -0.5 to convert edge-aligned grid coords back to center-aligned for trilinear/bilinear interpolation. Conventions are inverses and applied uniformly. Delta #7 is a no-op in the current volumetric topology.

**Topology dependency:** ✓ portable. Texel-center convention is mesh-independent.

**Port disposition:** No work required. Audit doc is the deliverable.

---

## Summary table — M1 work order

Per [v3_shadertoy_adoption_scope.md §3 M1](v3_shadertoy_adoption_scope.md), updated post-Stage 0:

| Order | Delta | Topology tag | Bundling / sequencing | Estimated effort | Notes |
|-------|-------|--------------|------------------------|------------------|-------|
| 1 | #3 (redefined) | ⚠ | Bundled with #6; #6 sequenced after | 1-2 sessions | Per-corner gated trilinear + merge call-site change. Front-load with [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md). Verdict semantics: additive (STRONG/MARGINAL/DEAD bands apply directly). |
| 2 | #6 | ⚠ [→#3] | Bundled with #3 in single A/B harness | 1 session | Shares 2×2 A/B matrix with #3 (see Delta #6's Bundling trade-off). **Drop-rule:** if #6-alone-DEAD regardless of #3 outcome, retire #6 from M1 without per-cell completion. Verdict requires both metric improvement AND principled geometric justification (Correctness criterion, DNR #4 analog). |
| 3 | #4 | ✓ | Standalone A/B | 1 session | **Formulation-comparative, not additive port.** A/B deterministic-N-sample vs stochastic-1-sample (current MC). Verdict semantics: STRONG → replace MC default; MC-wins → mark #4 verified-equivalent-or-better, not "ported." **Conditional interpretation:** if #3+#6 cumulative reduced bright% to <7% AND \|p95\| to <0.60, a MARGINAL #4 verdict is expected and does NOT mean "MB formulation is a weak lever" — the remaining gap is too small for #4 to leverage. Re-evaluate against the residual headroom, not the absolute scale. |
| — | #5 | ✗ | — (Path B only) | Skip in Path A | < 3% magnitude leverage per [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md). Path B value is topology, not pre-weighting. M2 prep: mechanism scoping for hemisphere-only / per-corner-gating-under-surface-attached / surface-aware probe placement (deferred from Delta #5). |
| — | #1, #2, #7 | ✓ / ✓ / ✓ | — | No work | #1+#2 LANDED v2.0-postfix; #7 CONFORMANT per [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md). |

**Net M1 work order:** {#3, #6, #4} (reordered from scope's {#3, #4, #6} — #6 follows #3 to share impl doc and A/B harness). Status snapshot and this work-order table are synchronized: both show topology tag with sequencing notation (`[→#N]`), bundling status, and per-delta conditional notes.

## What this doc does NOT cover

- The **M1 cumulative gate logic** (M1_CLOSES_GAP / M1_PARTIAL_GEOMETRY / M1_PARTIAL_MAGNITUDE / M1_DEAD) — that lives in scope §3.
- The **per-delta A/B harness** — defined separately at M1 Stage 1.
- **Sponza-specific deltas** — Sponza inherits the same delta list, but per-corner gating likely matters more (see C+ audit's Sponza note).
- The **algebraic estimate for Delta #5 leverage** — that's [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md) (Deliverable B, in progress).
