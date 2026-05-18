# Critic Review 03 — probe_visibility_acceleration_plan.md

Reviewed: 2026-05-12

Target: [doc/6/claude_plan/probe_visibility_acceleration_plan.md](../probe_visibility_acceleration_plan.md)

## Verdict

The plan is directionally appealing — the "use the alpha channel that is
already baked" idea is correct in principle, the cost-vs-mode-3 framing is
sound, and the bake-side data is genuinely there
([res/shaders/radiance_3d.comp:428](../../../res/shaders/radiance_3d.comp#L428)
and [:431](../../../res/shaders/radiance_3d.comp#L431) both write
`vec4(rgb, hit.a)` into the RGBA16F atlas, format confirmed at
[src/demo3d.cpp:2858](../../../src/demo3d.cpp#L2858)). The "honest
expectation note" at the bottom is also welcome — the author recognizes
the cone-angle correction is fragile.

However, the plan ships at least **three load-bearing technical errors**
that will make the as-written shader either over-occlude badly, under-
occlude trivially, or silently degrade when temporal blending is enabled.
Two of them stem from copying the ShaderToy formula incorrectly; one is a
pipeline interaction that the plan does not mention. None require
abandoning Mode 4, but all three should be fixed before the shader patch
lands or the verification step (mode 3 vs mode 4 visual A/B) will report
the wrong conclusion.

## Findings

### F1 (HIGH) — Cone-angle correction is the wrong trig function

The ShaderToy reference test the plan cites
([shader_toy/CubeA.glsl:21-42](../../../shader_toy/CubeA.glsl#L21))
is:

```glsl
length(relVec) < lProbeRayDist * cos(PI*0.5 - theta) + 0.01
```

`cos(PI/2 - theta) = sin(theta)`. For a narrow cone (small θ), `sin(θ)`
is **near 0**. The visibility predicate is intentionally restrictive —
"only count the probe as visible if the surface is much closer than the
probe's hit distance, scaled down by sin(θ)."

The plan transcribes this as:

```glsl
float cosCone = cos(3.14159265 / float(D) * 0.5);  // ← cos, not sin
...
wvis = (distSP < hitDist * cosCone + 0.01) ? 1.0 : 0.0;
```

For D=8: `cos(π/16) ≈ 0.98`. The plan's threshold is essentially
`distSP < 0.98 * hitDist + 0.01`, which is **~16× more permissive** than
the ShaderToy formula (`sin(π/16) ≈ 0.195`, and the plan's intended
match would be `sin(π/D * 0.5) ≈ 0.098` if you also halve the angle).
Result: Mode 4 will under-occlude across the board — closer to "no
visibility test" than to mode 3.

Either:
- Use `sin(...)` (matching ShaderToy semantics — restrictive, narrow
  cone), OR
- Pick a 3D-derived constant that is justified by the per-bin angular
  extent and document the derivation, OR
- Replace the scalar comparison entirely (see F2).

The plan's "Honest expectation note" anticipates this fragility but
does not flag that the formula as written is *literally the wrong trig
function*, not just "needs tuning."

### F2 (HIGH) — Direction mismatch: scalar `distSP` vs per-bin `hitDist` is comparing apples to oranges

The ShaderToy `WeightedSample` is **not** iterating all D² bins per
probe and applying the same comparison to each. Looking at the
reference more carefully:
[shader_toy/CubeA.glsl:21-42](../../../shader_toy/CubeA.glsl#L21)
computes `relVec = probePos - lastProbePos`, derives the bin index
`phi` from the *direction of relVec*, then fetches a **single** bin —
the one along the surface→probe axis — and tests whether the probe's
hit in *that* direction is farther than the surface. It is a
direction-aware single-bin lookup.

The plan's `sampleProbeDirDepthAware` iterates all D² bins per probe
and reuses the same `distSP` scalar for every bin's `hitDist`. That
test is geometrically meaningful only when bin direction `bdir`
roughly matches the surface→probe axis. For all other bins, `hitDist`
describes geometry along a totally unrelated ray.

Concrete failure mode: probe hangs 1 m above the floor. Surface point
is on the floor, 0.5 m horizontal offset → `distSP ≈ 1.12 m`.

- "Down" bin shoots toward floor, hits at 1.0 m. `distSP > hitDist` →
  marked **occluded**. But this is the bin whose radiance is most
  relevant to the floor surface!
- "Up" bin shoots toward ceiling at 3 m. `distSP < hitDist` →
  marked **visible**. Sky/ceiling radiance leaks into the floor's
  gather even though geometrically the floor never sees it through
  the bin's path.

The result: depending on probe layout, mode 4 can systematically
*invert* the correct visibility decision per bin, making the mode-3
A/B comparison meaningless.

Two ways out:
1. **Faithful port**: per surface pixel, compute `bin =
   dirToBin(normalize(probeCenter - surfacePos))`, fetch only that bin
   per probe, test `distSP < hitDist * sin(θ) + ε`, and use the
   result as a *probe-level* visibility weight (similar to current
   mode 1 binary, but data-driven instead of SDF-traced). This is the
   actual ShaderToy algorithm — it does NOT eliminate dot-banding
   relative to mode 1, because the visibility signal is again
   per-probe (one binary decision per corner), not per-bin.
2. **3D-correct per-bin**: for each bin direction `bdir`, project
   `surfacePos − probeCenter` onto `bdir` to get the signed distance
   `t = dot(surfacePos − probeCenter, bdir)`. The probe's hit at
   distance `hitDist` along `bdir` blocks the bin **only if** `t > 0`
   (surface is on the far side of the hit) AND `t > hitDist`. Bins
   where the surface is on the same side as the probe (`t < 0`) or
   between probe and hit (`0 < t < hitDist`) remain visible. This
   gives per-bin granularity (and so plausibly retains mode 3's
   no-banding property) without the apples-to-oranges scalar test.

The plan should pick one and rederive `wvis` accordingly. The current
formulation ("plan-as-written = ShaderToy in 3D") is neither.

### F3 (HIGH) — Temporal blend silently corrupts the alpha channel

[res/shaders/temporal_blend.comp:82](../../../res/shaders/temporal_blend.comp#L82)
runs `imageStore(oHistory, coord, mix(his, cur, uAlpha))` on a
`vec4` — so the alpha channel (hit distance) is EMA-blended with the
history buffer's stale alpha just like RGB. The AABB clamp at
[temporal_blend.comp:79](../../../res/shaders/temporal_blend.comp#L79)
also clamps the vec4, including alpha.

Cerebrum says temporal is OFF by default
(`doc/cluade_plan/AI/phase10_temporal_perf.md`-era decision), so the
default render path through Mode 4 reads the pristine bake-time
`hit.a`. **But:**

- Anyone enabling temporal (CLI / UI toggle) silently degrades Mode 4.
  The visibility test then compares `distSP` against a numerically
  meaningless EMA of recent hit distances — possibly across very
  different surfaces if the probe moves through thin geometry frame-
  to-frame.
- Future work that re-enables temporal-by-default (the plan in
  `doc/5/claude_plan/zero_init_cascade_textures_step11_followup_plan.md`,
  any staggered-cascade revival) will silently break Mode 4 with no
  shader-side error.

The plan should either:
- Document that Mode 4 requires `temporalEnabled == 0` and assert it
  in C++ (or in shader via a uniform check), OR
- Patch `temporal_blend.comp` to do `mix(his.rgb, cur.rgb, uAlpha)` and
  pass through `cur.a` (matching the same fresh-data-only treatment
  that radiance_3d.comp:428 already gives `hit.a`), OR
- At minimum: add a verification step "capture mode 4 with temporal
  ON, confirm visibility behaves as if uniform" so the regression is
  caught.

This is a real footgun, not a hypothetical one — the project ships a
`useTemporalAccum` toggle (cerebrum, Phase 9) and there are open
follow-up plans to re-enable it.

### F4 (MEDIUM) — `a.a == 0.0` exact float comparison is fragile

Plan uses:

```glsl
if (hitDist <= 0.0) { wvis = 1.0; }
else { /* surface-hit branch */ }
```

The semantics in [radiance_3d.comp:428](../../../res/shaders/radiance_3d.comp#L428)
write `hit.a` directly without any rounding/sentinel layer. If the
upstream cascade-trace ever returns a positive epsilon hit distance
(e.g. `1e-7` from a near-immediate intersection), this branch
classifies it as a tiny-distance occluder and almost everything gets
marked occluded.

Conversely, if temporal blend is enabled (F3), `hit.a` will commonly
be near-zero noise from EMA-decayed sky or miss bins — and the test
will fall through to the "surface hit at distance ≈ 0" branch, which
is even worse than the corrupted-but-plausible distance values.

Fix: replace `hitDist <= 0.0` with `hitDist < 0.5 * voxelSize` (or a
named `MISS_DIST_EPSILON`) to reject "effectively zero" as a miss.

### F5 (MEDIUM) — `probeCenter` formula is co-located, but project uses non-co-located probes

The plan computes:

```glsl
vec3 probeCenter = uAtlasGridOrigin
                 + (vec3(pc) + 0.5) * (uAtlasGridSize / vec3(uAtlasVolumeSize));
```

This matches the formula in
[raymarch.frag:316](../../../res/shaders/raymarch.frag#L316) used by
the existing `probeVisibility`. But cerebrum (Phase 5d) explicitly
states the project uses **non-co-located** probes between cascades,
and the current `sampleDirectionalGI` at
[raymarch.frag:413](../../../res/shaders/raymarch.frag#L413) does its
own `pos → uvw → pg` map, then trilinearly blends 8 surrounding probe
corners with `f = fract(pg)` — which is consistent with the
`(pc + 0.5)` offset formula but only if "probe k" lives at world
coord `origin + (k + 0.5) * cellSize`.

Either:
- The non-co-located layout is a per-cascade concept that does NOT
  affect this single-cascade atlas (the directional atlas is one
  cascade's bins, and within that cascade probes ARE on a regular
  grid). In that case the formula is fine and this finding is moot —
  but the plan should note that explicitly.
- Or the formula is wrong, in which case the existing
  `probeVisibility` is also wrong and Mode 4 will inherit the same
  bug.

Worth a one-line confirmation against [src/demo3d.cpp](../../../src/demo3d.cpp)
"AtlasGridOrigin" upload site, not just trusting the existing
helper's formula by analogy.

### F6 (MEDIUM) — Cost analysis vs Mode 3 understates Mode 3 by 4-8×

Plan claims:

> vs **mode 3**: 8 × 64 × 8 SDF samples (sphere-trace from surface in
> bin direction) = **~4096 sampler3D fetches + branches** per pixel.

Looking at the actual mode-3 implementation
[raymarch.frag:387-405](../../../res/shaders/raymarch.frag#L387):
each per-bin shadow trace runs `for (int i = 0; i < 8 && t < maxLen;
++i)` — so up to **8 SDF samples per bin per corner**, NOT
"8 samples per ray". The mode-3 worst case is `8 corners × D² bins ×
8 SDF samples = 32768` sampler3D fetches per pixel at D=8, not 4096.
And each sphere-trace fetch is sequential/dependent — defeats GPU
prefetching as the plan notes.

This actually *strengthens* the plan's case ("Mode 4 is even cheaper
relative to Mode 3 than I claimed"), but the as-written number is
wrong by 8× and will confuse anyone reading the plan to estimate
budget delta.

Also missing: per-bin branch divergence cost. At D=8, the per-bin
loop has a `wcos <= 0.0` early-out (already skipped for back-facing
bins), and a depth-aware branch on `hitDist <= 0`. Two branches per
bin in a 64-bin loop is non-trivial divergence; the "ALU only"
budgeting line under-counts this on real hardware.

### F7 (LOW) — Plan glosses over "Mode 4 doesn't beat dot-banding if F2 fix uses single-bin"

If the F2 fix path #1 is taken (faithful ShaderToy port: per-probe
single-bin visibility), then Mode 4 reduces to "data-driven Mode 1" —
same dot-banding signature as Mode 1, just sourced from atlas alpha
instead of SDF traces. The plan's quality table

> | **4 depth-aware (ShaderToy-style)** | **~1.05×** | **NONE** | **NONE** |

should not promise "banding: NONE" until the F2 resolution is chosen.
The plan's "no banding" claim only holds for the per-bin path (F2 #2),
which is no longer "ShaderToy-style" but a 3D-derived test the plan
hasn't actually written.

### F8 (LOW) — Verification A/B is visual-only; lacks quantitative pass criterion

Plan says Mode 4 should look "≈ mode 3" and "should not over-darken."
For a quality verdict that will swap the default render path, this is
too qualitative — the existing project has Phase 12b auto-burst +
Claude Vision triage and Step 11 mean-luminance logging, and either
would give a numeric pass criterion (mean lum delta < X%, SSIM > Y).
Recommend the verification capture also dump `meanLum` (already in
the per-frame log line) and require Mode 4's mean within ±5% of Mode
3's at the same camera + light + scene.

Without that, there is no falsifiable claim — "looks similar" can be
asserted regardless of whether F1 + F2 actually got fixed.

### F9 (LOW) — Existing visibility-mode comment block is already stale

[raymarch.frag:307-311](../../../res/shaders/raymarch.frag#L307)
documents modes 0/1/2 only, but actual code at
[raymarch.frag:441](../../../res/shaders/raymarch.frag#L441) and
[:470](../../../res/shaders/raymarch.frag#L470) checks modes 0, 1, 3.
Adding mode 4 will compound this — the comment will document 0/1/2/4
while code handles 0/1/3/4. Plan's "Phase 2 — C++ side" should
include a one-shot rewrite of that comment block to match the modes
that actually exist.

### F10 (LOW) — "Default = mode 4" decision tied to verification outcome but no rollback path

The plan says default flips to Mode 4 if visual matches. There is no
mention of:
- What happens to existing screenshot baselines / golden images that
  were captured under the prior default (Mode 0).
- Whether mode 4 is locked behind a CLI flag for the first N captures
  before promotion.
- How a regression caught post-promotion gets rolled back (revert PR,
  toggle in settings, etc.).

For a single-developer hobby project this is overkill, but for a
plan that explicitly recommends "deprecating modes 1/2," at least one
paragraph on "if mode 4 ships and turns out to be subtly wrong on
edge scenes, the rollback is X" would prevent the next contributor
from re-deriving the answer.

## What the plan got right

- Correctly identifies that atlas alpha already carries hit distance
  ([radiance_3d.comp:428,431](../../../res/shaders/radiance_3d.comp#L428))
  and that texture format is RGBA16F
  ([demo3d.cpp:2858](../../../src/demo3d.cpp#L2858)).
- Correctly identifies that `texelFetch().rgb` discards data we
  already paid bandwidth for; switching to `.rgba` is genuinely free.
- Reduction pass writing `vec4(avg, 0.0)` to the *isotropic*
  `probeGridTexture` ([reduction_3d.comp:47](../../../res/shaders/reduction_3d.comp#L47))
  does NOT corrupt the directional atlas alpha — Mode 4 reads the
  per-cascade directional atlas, which reduction never touches. Plan
  is implicitly correct here, even if it does not call out the
  separation.
- "Out of Scope" correctly defers cascade-inheritance / bake-time
  visibility — that is a separate problem and the plan should not try
  to solve it inline.
- "Honest expectation note" + named Mode 5 fallback (depth-aware +
  one confirmation shadow ray) is the right hedge if the algorithm
  doesn't match Mode 3 quality. Good design discipline.

## Recommended Pre-Implementation Edits

Before writing any shader code:

1. **Resolve F2** — pick "faithful single-bin port" or "3D-correct
   per-bin" and rewrite the algorithm section. The current
   pseudocode is neither.
2. **Fix F1** — replace `cosCone = cos(...)` with `sin(...)` (or with
   the F2-resolved formula's correct constant), and walk through one
   numeric example to confirm the threshold is restrictive, not
   ~unity.
3. **Address F3** — add a "Pre-requisites" section noting
   `temporalEnabled == 0` is required, OR include a one-line patch to
   `temporal_blend.comp` to pass-through `cur.a`.
4. **Patch F4** — replace `hitDist <= 0.0` with an epsilon-fenced
   miss test.
5. **Tighten F8** — add a numeric pass criterion (meanLum delta <
   5%) to the verification section.

Findings F5, F6, F7, F9, F10 are documentation/cleanup; can be
folded in during the implementation pass without blocking.

## Evidence Checked

- [doc/6/claude_plan/probe_visibility_acceleration_plan.md](../probe_visibility_acceleration_plan.md) — full read.
- [res/shaders/radiance_3d.comp:400-435](../../../res/shaders/radiance_3d.comp#L400) — confirms `vec4(blended, hit.a)` and `vec4(sanitizeRadiance(rad), hit.a)` writes; alpha is fresh-frame, not blended.
- [res/shaders/temporal_blend.comp:78-82](../../../res/shaders/temporal_blend.comp#L78) — confirms vec4 EMA + vec4 AABB clamp; alpha channel corrupted on temporal path. Load-bearing for F3.
- [res/shaders/reduction_3d.comp:47](../../../res/shaders/reduction_3d.comp#L47) — `imageStore(oRadiance, probePos, vec4(avg, 0.0))` confirms reduction writes a separate texture (`probeGridTexture`), not the directional atlas. Mode 4 unaffected.
- [res/shaders/raymarch.frag:97](../../../res/shaders/raymarch.frag#L97), [:124](../../../res/shaders/raymarch.frag#L124), [:127](../../../res/shaders/raymarch.frag#L127), [:130](../../../res/shaders/raymarch.frag#L130), [:133](../../../res/shaders/raymarch.frag#L133) — uniforms `uVisibilityMode`, `uDirectionalAtlas`, `uAtlasVolumeSize`, `uAtlasGridOrigin`, `uAtlasGridSize` all exist; plan's helper signatures are compatible.
- [res/shaders/raymarch.frag:298-300](../../../res/shaders/raymarch.frag#L298) — confirms `binToDir(ivec2 bin, int D)` exists and uses octahedral encoding.
- [res/shaders/raymarch.frag:307-311](../../../res/shaders/raymarch.frag#L307) — comment documents modes 0/1/2 but code at [:441](../../../res/shaders/raymarch.frag#L441) / [:470](../../../res/shaders/raymarch.frag#L470) handles 0/1/3. Comment is stale (F9).
- [res/shaders/raymarch.frag:316-317](../../../res/shaders/raymarch.frag#L316) — `probeCenter` formula matches the plan's; same one Mode 4 will use (F5 caveat).
- [res/shaders/raymarch.frag:355-370](../../../res/shaders/raymarch.frag#L355) — current `sampleProbeDir` does `.rgb` only, throwing away atlas alpha; plan's claim that switching to `.rgba` is free is correct.
- [res/shaders/raymarch.frag:378-407](../../../res/shaders/raymarch.frag#L378) — `sampleProbeDirPerBinOccluded` (Mode 3) actual cost is up to **8 SDF fetches per bin per corner**, not "8 SDF samples per pixel" as the plan summarizes (F6).
- [src/demo3d.cpp:2160](../../../src/demo3d.cpp#L2160), [:2858](../../../src/demo3d.cpp#L2858) — `probeAtlasTexture` bound RGBA16F; alpha precision sufficient for hit distance.
- [src/demo3d.cpp:2430](../../../src/demo3d.cpp#L2430) — `uDirectionalAtlas` bound to texture unit 3.
- [shader_toy/CubeA.glsl:21-42](../../../shader_toy/CubeA.glsl#L21) — reference WeightedSample. Uses `cos(PI*0.5 - theta) = sin(theta)` (F1) and a single direction-aware bin (F2), not a D² loop with a shared scalar test.

## Severity Summary

| # | Severity | Topic | Blocks impl? |
|---|---|---|---|
| F1 | HIGH | `cosCone` should be `sin(theta)` | yes |
| F2 | HIGH | Per-bin scalar test geometrically ill-defined in 3D | yes |
| F3 | HIGH | Temporal blend corrupts alpha (silent regression on toggle) | should fix or document |
| F4 | MEDIUM | `hitDist <= 0.0` exact-zero compare | should fix |
| F5 | MEDIUM | Confirm `probeCenter` formula vs non-co-located layout | clarify |
| F6 | MEDIUM | Mode 3 cost understated by 8× | doc-only |
| F7 | LOW | "No banding" claim depends on F2 resolution | doc-only |
| F8 | LOW | Verification needs numeric pass criterion | tighten |
| F9 | LOW | Existing visibility-mode comment is stale | one-line fix |
| F10 | LOW | No rollback plan for default flip | doc-only |
