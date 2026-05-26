# v2.0 — ShaderToy 3D RC vs our 3D RC: algorithmic diff & theoretical-fix candidates

**Status:** First diff-against-reference, replacing the measurement-only
posture for the v2.0 program. Direct response to user pivot 2026-05-25:
*"Why are we still visual testing comparison, should we just fix MB RC
with theoretical correction? what essentially prevent our 3d rc impl to
not act the same quality as from shader toy 3d rc"*.

**Goal:** identify the algorithmic delta(s) that plausibly explain the
[CV1](v20_cv1_convergence_impl.md) 35% mean-energy dim gap, so v2.0 can
close it with a targeted fix rather than another measurement campaign.

**Date:** 2026-05-25.

## 1. Inputs read

ShaderToy 3D RC reference impl (in-tree, [3d/shader_toy/](../../shader_toy/)):

- [Common.glsl](../../shader_toy/Common.glsl) — scene + TraceRay
- [CubeA.glsl](../../shader_toy/CubeA.glsl) — cascade bake (cubemap pass)
- [Image.glsl](../../shader_toy/Image.glsl) — same cascade bake body
  (mirrored — both ShaderToy passes share the cascades-and-merging code
  via Common.glsl include conventions)

Our impl:

- [res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp) —
  our cascade bake (compute shader, 862 lines)
- [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag) — our
  cascade consumer + final renderer (1064 lines, only the `sampleProbeDir`
  / `sampleDirectionalGI` paths read for this diff)

## 2. Topology delta — surface-attached vs volumetric

This is the **architectural prior** that constrains every other delta below.
It is not itself a fixable bug; it shapes which ShaderToy primitives can
even be ported.

**ShaderToy** ([CubeA.glsl:69-121](../../shader_toy/CubeA.glsl#L69-L121)):

- Probes are **surface-attached**. Each probe pixel carries a hardcoded
  `gTan`, `gBit`, `gNor`, `gPos` (the wall it sits on).
- Probe rays sweep only the **hemisphere above gNor** via octahedral
  `probeTheta ∈ [0, π/2]`, `probePhi ∈ [0, 2π)`.
- Bake stores fully pre-integrated `L × cos(θ) × ΔΩ` per bin.
- Consumer is just a cubemap fetch — the integral happens at bake time.

**Ours** ([radiance_3d.comp:577-861](../../res/shaders/radiance_3d.comp#L577-L861)):

- Probes are **volumetric**. Each probe is a position in a 3D grid with
  no associated surface normal.
- Probe rays sweep the **full sphere** via octahedral `binToDir(dx, dy, D)`
  over [0,1]² → S².
- Bake stores raw `L_in(ω)` per bin (+ Phase 2 binary α).
- Consumer fetches per-pixel-normal and does the hemispheric integral
  at consume time via `wcos = max(0, dot(bdir, normal))`.

**Implication:** ShaderToy's bake-time cosine and area weighting
(diff #5 below) is only well-defined *because* it knows `gNor` at bake
time. Our impl cannot port that directly — the consumer-side cosine is
the correct architectural choice for volumetric probes. **But the math
inside our consumer differs from the correct hemispheric Riemann sum in
ways that ShaderToy avoids by sidestepping the consumer-side integral.**

## 3. The seven deltas (ranked by suspected leverage on CV1 gap)

### Delta #1 — Consumer drops surface-hit bins from the irradiance integral (HIGHEST LEVERAGE)

**Site:** [raymarch.frag:421-446](../../res/shaders/raymarch.frag#L421-L446)
(`sampleProbeDir`).

```glsl
float w    = wcos * a.a;          // a.a = Phase 2 binary {0,1}
irrad   += a.rgb * w;
wsum    += w;
...
r.irrad   = irrad / max(wsum, 1e-4);
```

**Phase 2 α encoding** ([radiance_3d.comp:706-741](../../res/shaders/radiance_3d.comp#L706-L741)):

| bake outcome | α |
|---|---|
| sky exit (ray exited volume) | 0 |
| surface hit (ray hit geometry in [tMin, tMax]) | **0** |
| in-volume miss (ray traveled tMin..tMax without hit) | 1 |

Surface hits write `α = 0`. Consumer's `w = wcos × a.a` is therefore
**zero on every surface-hit bin** — the near-field indirect radiance
(which is exactly what cascade is supposed to deliver) is **dropped
from the irradiance integral**.

Only "in-volume miss" bins contribute. For those, the bake stored
`rad = upperDir.rgb * uGIStrength` ([radiance_3d.comp:797](../../res/shaders/radiance_3d.comp#L797))
— i.e., the upper cascade's value passed through. So our irradiance is
effectively *"upper cascade far-field, filtered to hemisphere bins
that didn't terminate locally"*, with all near-field contribution
deleted.

**ShaderToy** does NOT α-gate at consume time. Every hemisphere bin's
.rgb (pre-merged with smoothstep + WeightedSample-blended upper) is
summed unconditionally.

**Theoretical fix:** drop `* a.a` from `w`. The bake-time `rad`
already does interval composition (`hit.rgb * l + upperDir.rgb * (1-l)`)
— that's the merged answer; the consumer must read it, not re-gate it.
Keep `a.a` available for diagnostics (mode 14 leak / mode 15 oscillation)
but stop using it as a per-bin radiance multiplier.

**Predicted effect:** large brightening across all cascade-fed pixels.
Likely closes most of the 35% mean-energy gap. May expose Phase 2
visibility leaks that the α-gate was masking — those need to be
addressed separately (likely via the ShaderToy-style WeightedSample
already in `sampleUpperDirWeighted` which our bake already implements
behind `uUseWeightedSample`).

**Risk:** the historical Phase 2 cleanup notes (Phase 2C cleanup,
`feedback_cascade_merge_is_bake_time`) explicitly removed alternative
consumer paths and committed to "the bake-side α-gate inside
sampleProbeDir is now the single visibility path." Reverting that needs
a clear contract with the bake-side merge to take full responsibility
for visibility. The user feedback memory
[[feedback_cascade_merge_is_bake_time]] supports this: cascade merging
IS bake-time work, so consumer should not be re-doing visibility logic.

### Delta #2 — Consumer normalizes irradiance to a weighted mean, not the Riemann sum

**Site:** [raymarch.frag:442](../../res/shaders/raymarch.frag#L442).

```glsl
r.irrad = irrad / max(wsum, 1e-4);
```

This divides by `Σ(wcos × a)`, giving the **weighted MEAN radiance**
across visible bins. The correct Lambertian irradiance is the **sum**:

```
# Our binToDir covers the FULL SPHERE (S²) over D² bins,
# so ΔΩ ≈ 4π/D² per bin (not 2π/D² — that would be hemisphere).
E_irrad = Σ L_i × cos⁺(θ_i) × ΔΩ_i ≈ (4π/D²) × Σ L_i × cos⁺(θ_i)
L_out   = (albedo/π) × E_irrad     = albedo × (4/D²) × Σ L_i × cos⁺(θ_i)
```

where `cos⁺ = max(0, n·ω)` already gates the lower-hemisphere bins to
zero (the consumer-side hemispheric mask). The dimensional constant is
**`4/D²`**, not `2/D²` as an earlier revision of this doc claimed —
that error is corrected here, attributed to confusing the volumetric
full-sphere bin layout with ShaderToy's surface-attached hemispheric
layout. Empirical verification of the constant is part of the Finding 6
pre-fix experiment (see §8).

Ours computes `albedo × Σ(L × cos × a) / Σ(cos × a)`. The dimensional
factor `(4/D²)` is replaced by `1/Σ(cos × a)` — i.e., the proper Riemann
ΔΩ is replaced by `1/(number of weighted-visible bins)`.

**Net direction is hard to predict without instrumentation** — depends on
how `Σ(cos × a)` compares to D²/4 (the all-visible limit). When visibility
is sparse (high occlusion), the ratio can over- or under-brighten depending
on which bins were dropped. The dropped-surface-hit problem of #1 compounds
this: a probe near a wall has many surface-hit bins, so `Σ(cos × a)` is
small, and the normalization divides by a small number — producing
**spuriously bright** output from the few visible-and-far-field bins.

**Why CV1 reads DIM despite this**: in the dim regions (28.6% of pixels at
N=2048 with cascade < 0.5× PT), most bins are surface-hit (α=0) AND the
far-field upper cascade is dim (no sky/strong light source visible) →
output is dim from both terms. The 5.4% bright tail likely IS the
"normalize over few visible bins blows up" pathology.

**Theoretical fix (paired with Delta #1):** replace
`r.irrad = irrad / wsum` with `r.irrad = irrad * (4.0 / float(D*D))` —
i.e., compute the proper Riemann sum over full-sphere bins, with `cos⁺`
already masking the lower hemisphere. Combined with dropping `* a.a`
(Delta #1), this becomes `L_out = albedo × (4/D²) × Σ(L × cos⁺)` — the
correct Lambertian outgoing radiance for our full-sphere octahedral
binning convention.

**This is a paired fix** — applying Delta #2 without #1 would still drop
surface-hit bins; applying #1 without #2 would brighten by a factor that
depends on `Σ(cos × a)`. Both together are the minimal-change
"correct hemispheric integral" patch.

### Delta #3 — Bake's smoothstep merge feeds dead .rgb when α=0

**Site:** [radiance_3d.comp:776-798](../../res/shaders/radiance_3d.comp#L776-L798).

```glsl
if (hit.a < 0.0) { rad = hit.rgb; }                              // sky → α=0
else if (hit.a > 0.0) {                                          // surface → α=0
    rad = hit.rgb * l + upperDir.rgb * (1-l) * aFactor * uGIStrength;
} else {                                                          // miss → α=1
    rad = upperDir.rgb * uGIStrength;
}
```

For surface hits (α=0), the smoothstep blend writes a meaningful `rad`
that combines near-field (`hit.rgb`) with upper-cascade far-field
(`upperDir.rgb * (1-l)`). But under the current consumer (Delta #1),
this `.rgb` is **never read** for irradiance — only the diagnostic
modes 14/17/22 read it directly.

**This is a wasted bake computation under current consumer.** Fixing
Delta #1 makes this bake work matter. Fixing Delta #1 incorrectly (e.g.,
just reading `.rgb` unconditionally without dropping α gate) could
double-count visibility.

**Theoretical fix:** none on its own — this delta is fixed downstream
of Delta #1's α-gate removal.

### Delta #4 — Multi-bounce stochastic single-bin vs ShaderToy multi-bounce 4-cube-read average

**Site:** [radiance_3d.comp:439-501](../../res/shaders/radiance_3d.comp#L439-L501)
(`sampleC0AtlasStochastic`), and ShaderToy's bounce light
[Image.glsl:167-170](../../shader_toy/Image.glsl#L167-L170):

```glsl
// ShaderToy
vec2 suv = clamp(rayHit.uv*128., vec2(0.5), rayHit.res*0.5 - 0.5) + rayHit.uvo;
Output.xyz = TextureCube(suv, 0.).xyz + TextureCube(suv + vec2(rayHit.res.x*0.5, 0.), 0.).xyz +
             TextureCube(suv + vec2(0., rayHit.res.y*0.5), 0.).xyz + TextureCube(suv + rayHit.res*0.5, 0.).xyz;
```

ShaderToy averages 4 cubemap reads (4 probes in a 2×2 corner of the
hit-surface's UV map). It is essentially a deterministic 4-tap
"approximate hemisphere irradiance from local cubemap" estimator. No
Monte Carlo sampling, no random direction, no per-frame seed.

Our `sampleC0AtlasStochastic` uses MC: cosine-sample one direction, fetch
one bin, trilinear-blend over 8 probes. Per-frame MC sample relies on
temporal EMA to converge.

**This is the right call for volumetric probes** — ShaderToy can do the
4-cube-read trick because its probes are surface-attached with a 2D UV
on the wall (`rayHit.uv`). We don't have that. The stochastic single-bin
is the volumetric analogue and is theoretically correct (cosine PDF
cancels with cosine in render eq).

**Theoretical fix:** none. Our impl is correct for the volumetric
topology.

**Adjacent concern (NOT a delta with ShaderToy, but worth noting):** our
MB feedback at [radiance_3d.comp:441-481](../../res/shaders/radiance_3d.comp#L441-L481)
(`sampleC0AtlasOneBin`) **already reads `a.rgb` without applying the
`* a.a` multiplier** — the inline comment notes "Removing the `a.a`
factor here matches v1 hemisphere's effective output." So the MB
feedback path has *already* been quietly bypassing the consumer α-gate;
only the direct display consumer at `sampleProbeDir` still applies it.

This means: (a) the "MB equilibrium will shift on fix" risk is smaller
than initially feared (MB was already integrating against ungated
radiance), (b) the dim gap is more directly attributable to the
display-consumer α-gate (Delta #1) rather than to MB feedback under-
feeding, (c) Delta #1's fix removes the last surviving α-gate in the
radiance flow, restoring contract consistency.

### Delta #5 — Bake-time cosine and per-bin solid-angle pre-weighting (ShaderToy only)

**Site:** [Image.glsl:189-192](../../shader_toy/Image.glsl#L189-L192):

```glsl
// ShaderToy
Output.xyz *= (cos(probeTheta - 3.141592653/probeSize) -
               cos(probeTheta + 3.141592653/probeSize))/(4. + 8.*floor(probeThetai));
Output.xyz *= cos(probeTheta); //Diffuse
```

This pre-multiplies each bin's stored radiance by `ΔΩ_ring × cos(θ)`
so the cubemap consumer just sums hemisphere bins to get irradiance.

**Cannot port directly** to our impl — requires a fixed `gNor` at bake
time (volumetric probes don't have one). The consumer-side
`wcos × ΔΩ` (Deltas #1 + #2 fix together) is the volumetric analogue.

**Theoretical fix:** none — solved by Deltas #1 + #2 in our topology.

### Delta #6 — WeightedSample semantics: theta-of-ray vs theta-of-bin

**Site:** ShaderToy [Image.glsl:26-36](../../shader_toy/Image.glsl#L26-L36)
vs ours [radiance_3d.comp:291-352](../../res/shaders/radiance_3d.comp#L291-L352).

ShaderToy's visibility threshold:
```glsl
float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*3.141592653*0.5;
...
if (lProbeRayDist < -0.5 || length(relVec) < lProbeRayDist*cos(π/2 - theta) + 0.01) { ... }
//                                                          = lProbeRayDist * sin(theta)
```

ShaderToy's `theta` is the **upper bin's polar angle in the hemisphere**
(roughly: how much the look-back ray tilts from gNor). The visibility
test is "is the lower probe within `dist × sin(theta)` of the look-back
ray's terminal point" — a wedge in the angular domain of the upper probe.

Ours:
```glsl
bool visible = (lProbeRayDist < 0.0)
            || (length(relVec) < lProbeRayDist * uUpperBinConeSin + 0.01);
```

`uUpperBinConeSin = sin(theta_half)` where `theta_half = acos(1 - 2/D²)`
is the **upper bin's half-angle cone** (the directional bin's angular
extent, NOT the polar angle of that bin in the hemisphere).

**These are different geometric quantities.** ShaderToy's threshold scales
with WHICH bin you're testing against (equator bins get tighter cone
than near-pole bins). Ours uses a single global `theta_half` per bin
of the cascade (since all our octahedral bins have the same nominal
solid angle in the iso-area approximation — actually they don't, but
we treat them as if they do).

**Theoretical fix:** if Delta #1 is applied (consumer drops α-gate) and
the bake-side WeightedSample is enabled (`uUseWeightedSample=1`) to
prevent leaks that the α-gate was masking, the threshold should be
reviewed. Current `uUpperBinConeSin` is a reasonable approximation; the
ShaderToy-style angle-adaptive threshold would be more correct but is a
secondary refinement. Defer until after #1+#2 land.

### Delta #7 — Probe-position cell convention (-0.5 offset placement)

**Site:** [radiance_3d.comp:616](../../res/shaders/radiance_3d.comp#L616)
vs ShaderToy [CubeA.glsl:130-132](../../shader_toy/CubeA.glsl#L130-L132).

ShaderToy computes probe positions directly from `gPos + tan/bit × cellSize`
(probe at the corner of its cell). Ours uses
`(vec3(probePos) + 0.5 + jitter) * cellSize` (probe at cell CENTER + jitter).

This is the long-running `-0.5` ambiguity that surfaced in
[v20_cprime_spatial_trilinear_impl.md](v20_cprime_spatial_trilinear_impl.md)
and was traced to camera-direction asymmetry (h.c)'/(h.c)''/(h.c)'''.
Our consumer-side trilinear `pg = uvw × volSize - 0.5` matches the
center-aligned convention. ShaderToy's surface-attached topology means
the offset question doesn't arise the same way.

**Theoretical fix:** the (h.c)''' ST=0 mitigation already addresses the
camera-direction asymmetry caused by this; not a new delta needing fix.

## 4. Recommended fix: paired Deltas #1 + #2 (irradiance integral correction)

**The minimum-change fix that closes the most predictable amount of the
CV1 gap is the paired #1+#2:**

```glsl
// raymarch.frag sampleProbeDir() — proposed change
for (int dy = 0; dy < D; ++dy) {
    for (int dx = 0; dx < D; ++dx) {
        vec3  bdir = binToDir(ivec2(dx, dy), D);
        float wcos = max(0.0, dot(bdir, normal));
        vec4  a    = texelFetch(uDirectionalAtlas, ...);
        // OLD: float w = wcos * a.a;  irrad += a.rgb * w;  wsum += w;
        // NEW: drop alpha gate; do proper hemispheric Riemann sum
        irrad += a.rgb * wcos;
        // (no wsum needed for the integral form)
        // Keep leak / oscillation metric formulas using a.a (they're diagnostic-only)
        leakRgb += a.rgb * wcos * (1.0 - a.a);
        oscSum  += wcos * 4.0 * a.a * (1.0 - a.a);
        wcosSum += wcos;
    }
}
// Full-sphere bins ⇒ ΔΩ = 4π/D²; cos⁺ masks lower hemisphere.
// E_irrad = (4π/D²) × Σ L cos⁺ → L_out = albedo × (4/D²) × Σ L cos⁺
r.irrad       = irrad * (4.0 / float(D * D));
r.leak        = dot(leakRgb, vec3(0.2126, 0.7152, 0.0722));
r.oscillation = oscSum / max(wcosSum, 1e-4);
```

**Predicted CV1 outcome after fix** (probabilistic, widened post-critic):

- BAND 1 ratio: **0.65 → likely 0.7-1.3** (the wide band reflects three
  uncertainties: the constant `4/D²` is theoretically correct but
  unverified empirically, the visible-bin count `Σ(cos⁺·a)` interacts
  multiplicatively with the constant in the current code so the swing is
  large, and the MB equilibrium will re-balance under the new direct-
  consumer scale even though MB feedback was already ungated). A
  "ratio comes in at 1.5" outcome is plausible and would require a
  second corrective pass — the Finding 6 pre-fix experiment exists
  specifically to de-risk this.
- BAND 1 per-pixel `|p95|`: **1.28 → likely 0.4-1.0** (narrows tail but
  may not fully close if the constant is mis-tuned by a factor or two).
- BAND 1 dim%: **28.6% → likely 3-20%** (most dim pixels are near-wall
  pixels whose surface-hit bins were being dropped — this is the
  delta most directly addressed).
- BAND 1 bright%: **5.4% → uncertain, range 2-25%** (the small-`Σ(cos×a)`
  normalization-blowup pathology disappears with #2; bins previously
  "saved" by over-bright normalization may lose compensating brightness,
  while pixels with many surface-hit bins now get full near-field
  contribution. Direction depends on which population dominates).

If leaks become objectionable post-fix (e.g., bright halos near walls),
the bake-side `uUseWeightedSample=1` should be enabled — this is what
the WeightedSample primitive was built for, and Delta #1 was previously
NOT applied because the α-gate was acting as a poor-man's leak filter.

## 5. Self-critique

**Strengths:**

- Deltas are derived from line-level diff against reference impl in-tree,
  not from generic "make cascade brighter" intuitions.
- The paired #1+#2 fix is **theoretically motivated** (correct
  hemispheric Riemann sum vs current normalized-mean) and **dimensionally
  consistent** with PT's `(albedo/π) × ∫L cos dω` expectation.
- Predicted CV1 outcome (band-by-band) is **pre-committed** before any
  measurement — same falsification discipline as P2/CV captures.
- Identifies which ShaderToy primitives are **architecturally portable**
  (Deltas #1, #2, #6) vs **architecturally specific to surface-attached
  probes** (Deltas #4, #5) — avoids the wrong-primitive trap of
  Phase 3 v1/v2/v3.

**Weaknesses:**

- The fix is paired (#1 + #2 together). Applying only one is well-defined
  but is **not what's recommended**, and would yield ambiguous CV1 readings
  if attempted as an intermediate ablation step. The "fix one, measure,
  fix the other, measure" instinct should be resisted here — apply both
  together, then measure once.
- The dropped-surface-hit hypothesis (Delta #1) is reasoned from code, not
  from a per-pixel diagnostic (would CV5 — render-mode-19 cascade-PT delta
  map — show "dim concentrated near walls"? Likely yes, but unmeasured).
  An optional pre-fix sanity check is a single mode-19 capture, but is
  not strictly needed to justify the fix.
- The 35% MB-ON CV1 gap also has a multi-bounce-equilibrium component:
  Delta #4 notes MB feedback reads `a.rgb` which (under Delta #1 fix) will
  itself become the corrected merged radiance. The MB equilibrium will
  shift on fix — possibly producing MORE than the predicted brightening
  (positive feedback) or LESS (if MB was previously over-feeding due to
  the normalize-mean bias). The "predicted CV1 ratio: 0.85-1.05" range is
  intentionally wide to reflect this MB-equilibrium-shift uncertainty.
- The historical Phase 2 design **chose** the α-gate-in-consumer path with
  documented rationale (interval composition). Reverting it is a substantive
  architectural change, not a small tweak. The user-facing description
  should be "we're changing what cascade computes," not "we're fixing a
  bug." The CV1 measurement framework is the falsification gate.

## 6. What this diff does NOT address (deferred)

- **Per-pixel attribution** (CV5-style): which pixels carry the dim tail.
  Diff #1 predicts "pixels with high surface-hit fraction in their
  hemisphere" — empirically testable but not required for the fix.
- **Camera-axis asymmetry** (h.c branch findings): the ST=0 mitigation
  already in place addresses the major cam0/cam2 asymmetry observed in
  v1.3.x. Delta #1+#2 fix is independent and additive.
- **Multi-cascade compositing across c0/c1/c2/c3**: the per-cascade bake
  uses the same smoothstep merge between adjacent cascades. Delta #1's
  consumer-side change applies once (at the C0-fed display fetch). The
  bake-side interval composition between cascades is unchanged.
- **Hybrid retirement target**: this fix is targeted at cascade-only
  quality (CV1 baseline = `--use-hybrid=0`). A successful #1+#2 fix
  improves the cascade-only baseline by 30-50%; whether that's enough
  to retire hybrid is decided by post-fix CV2 (hybrid-ON A/B).

## 7. Critic pass (2026-05-25) — addressed findings

A second-pass critic was run against §§3-5 before committing to the
substantive consumer-α revert. Findings folded in:

**Finding 1 — Delta #2 dimensional constant was off by a factor of 2.**
Doc originally claimed `L_out = albedo × (2/D²) × Σ L cos`. Our
`binToDir` covers the FULL SPHERE (`octToDir` over `[0,1]² → S²`),
so D² bins distribute over 4π sr ⇒ ΔΩ ≈ 4π/D², and the Lambertian
constant reduces to **`4/D²`**. The ShaderToy `2/D²`-style intuition
applies only because its `probeTheta ∈ [0, π/2]` covers only the
hemisphere. Corrected throughout §3 Delta #2 and §4.

**Finding 2 — MB feedback already bypasses the α-gate.**
`sampleC0AtlasOneBin` at [radiance_3d.comp:441-481](../../res/shaders/radiance_3d.comp#L441-L481)
reads `a.rgb` without `* a.a`. Documented in Delta #4's adjacent-concern
paragraph. Implication: the dim gap is more directly attributable to the
display-consumer α-gate than to MB under-feeding, and Delta #1's fix
restores contract consistency rather than introducing it.

**Finding 3 — Predicted CV1 band was too narrow.**
Widened §4's predicted ratio from `0.85-1.05` to `0.7-1.3`, dim% from
`5-15%` to `3-20%`, bright% to `2-25%`. The wider band acknowledges
constant-tuning risk and MB-equilibrium re-balance.

**Finding 4 — Alternative explanations not surveyed.**
The diff doc was code-diff focused; possible additional contributors to
the 35% gap that were NOT distinguished by the diff alone:
- Albedo color-space mismatch (sRGB vs linear): `0.65 ≈ 2.2^(-0.5)` is
  ballpark suggestive but unverified.
- Light intensity scaling between cascade and PT paths.
- Self-occlusion at cell-center (probe inside SDF<0 at near-wall cells).
- Per-stage cascade merge leak c0→c1→c2→c3 (currently uses same
  smoothstep, but cross-cascade scale drift could compound).

These are NOT ruled out by the §4 fix landing in-band; they only become
ruled out by post-fix CV1 + targeted diagnostics.

**Finding 5 — Recommended path: pre-fix single-pixel bin-dump experiment.**
Before reverting the consumer-α contract, run a single-pixel probe-bin
printf to empirically validate Delta #1's dominance and resolve the
`4/D²` constant. Concretely: at a known-dim cornell pixel (mode-19 delta
map shows the dimmest region — likely a near-wall floor patch on cam0),
dump the 64 bins (D=8) the consumer reads as `(dx, dy, wcos, a.a, a.rgb)`.
The bin dump should reveal:

- **If many bins have `a.a == 0` with substantial `a.rgb`**: Delta #1 is
  the dominant lever (predicted).
- **If `a.a == 0` bins also have `a.rgb ≈ 0`**: Delta #1 is a no-op for
  these pixels and the gap is elsewhere (likely cascade merge leak or
  MB equilibrium); the fix would land at ratio ~unchanged and we'd
  need to chase a different delta.
- **If `Σ a.rgb × wcos` already approximates `irrad_PT × π / albedo`
  scaled by `D²/4`**: the `4/D²` constant is empirically correct.
- **If the predicted-correct sum is off by a clean factor of 2**: the
  constant is wrong; the doc's full-sphere reasoning needs review.

This experiment is **~2 minutes of code** (a single `if (gl_FragCoord.xy
== ivec2(target))` printf block in `sampleProbeDir`) and **<1% the cost
of CV1 re-measurement**. It de-risks the architecturally significant
consumer-α revert with empirical evidence.

**Critic verdict:** SHIP WITH CAVEATS — paired #1+#2 fix is sound, but
Finding 5's pre-fix bin-dump experiment is the recommended path to
avoid a "fix landed at ratio 1.5" outcome that re-opens the loop. After
the bin-dump confirms the constant and the dominance, apply #1+#2 with
the empirically-confirmed constant and capture CV1.

Also updated the [[feedback_theoretical_fix_over_measurement]] discipline
in passing: *theoretical-fix-over-measurement* is the right default for
algorithmic deltas with a reference impl, BUT a **single-pixel printf**
(not a full measurement campaign) is the cheapest possible empirical
gate against constant-tuning errors in a substantive contract revert.
The discipline isn't "no measurement"; it's "no full-measurement-as-
characterization." A 2-minute targeted printf is cheap insurance.

## 8. Cross-reference

- CV1 measurement that motivated this diff: [v20_cv1_convergence_impl.md](v20_cv1_convergence_impl.md)
- User pivot saved as feedback: `feedback_theoretical_fix_over_measurement.md`
  in `~/.claude/projects/d--GitRepo-My-radiance-cascades-demo/memory/`
- Cascade-merge-is-bake-time precedent: `feedback_cascade_merge_is_bake_time.md`
- Phase 2 α-gate origin: [doc/6/claude_plan/visibility_phase2_impl.md](../6/claude_plan/visibility_phase2_impl.md)
- WeightedSample bake-side impl: Phase 3
  [doc/6/claude_plan/visibility_phase3_impl.md](../6/claude_plan/visibility_phase3_impl.md)
- ShaderToy reference impl: [3d/shader_toy/](../../shader_toy/)
