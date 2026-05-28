# Delta #3 — α=0 semantics audit (current bake's `upperDir.rgb` content)

**Stage 0 Deliverable C+** of M0 ([v3_m0_stage0_plan.md](../../doc/7/v3_m0_stage0_plan.md)).
**Date:** 2026-05-26.
**Inputs:** [res/shaders/radiance_3d.comp:656-795](../../res/shaders/radiance_3d.comp#L656-L795), [shader_toy/CubeA.glsl:44-219](../../shader_toy/CubeA.glsl#L44-L219).

## Verdict

**Case B (semantic mismatch — Delta #3 cannot port naively).** Stronger than the scope's Case B definition: not only does the current `upperDir.rgb` contain semantic content when α=0, but **the α conventions between the two implementations are different signs**, and the ShaderToy "skip rgb on dead bin" mechanism uses a per-corner visibility-weighted normalization that the current impl does not have. Delta #3's M1 work item as currently scoped ("single-shader change: skip rgb when α=0") is **not portable** — it would discard meaningful radiance.

The correct port is structural: introduce per-corner gating of trilinear's `.rgb` sum (not just the `.a` output). This is a larger change than the scope assumed and warrants a dedicated impl doc with its own A/B before any code lands.

## α convention comparison

### Current impl (`res/shaders/radiance_3d.comp`)

Atlas storage convention (lines 700-702, 729-732, 150):

| Ray outcome | `hit.a` | atlas α written | atlas `.rgb` written |
|-------------|---------|------------------|----------------------|
| **Sky** (ray escaped scene) | `< 0` | `α = 0` | `rad = hit.rgb` (sky color from `GetSkyLight`-equivalent) |
| **Surface hit** (within [tMin, tMax]) | `> 0` | `α = 0` | `rad = hit.rgb * l + upperDir.rgb * (1-l) * aFactor * uGIStrength` |
| **In-volume miss** (no hit, no sky exit) | `== 0` | `α = 1` | `rad = upperDir.rgb * uGIStrength` (full upper continuation) |

Authoritative comment on line 700-702:
> "surface hit (hit.a > 0) → α = 0  (opaque — bin occluded)"
> "in-volume miss (hit.a == 0) → α = 1  (transparent — bin visible to far field)"

### ShaderToy reference (`shader_toy/CubeA.glsl`)

Atlas storage convention (lines 64, 155, 185):

| Ray outcome | atlas α (`Output.w`) | atlas `.rgb` |
|-------------|----------------------|--------------|
| **Sky** | `α = -1` | `Output.xyz = GetSkyLight(probeDir)` |
| **Surface hit** | `α = rayHit.t` (> 0) | direct lighting + bounce from cubemap fetch + sun term |
| **(α = 0)** | Never stored as a final value — `Output` starts at `vec4(0.)` at line 64 but the if-else at 154-187 always overwrites `.w` to either `> 0` (hit) or `-1` (sky) | n/a |

ShaderToy α=0 is essentially a **never-written sentinel**. It can only appear in the **outside-the-cube** branch (line 45: `Output = texture(iChannel3, rayDir)`), which is not part of the cascade merge.

### Convention divergence (the I11 issue, confirmed)

| Convention | α=0 meaning | α>0 meaning | α<0 meaning |
|------------|-------------|-------------|-------------|
| Current | hit OR sky (opaque, has rgb) | in-volume miss (transparent, has rgb continuation) | n/a |
| ShaderToy | sentinel — almost never stored | surface hit at distance α | sky |

These conventions are **incompatible**. A literal port of "if (α == 0) skip rgb" in ShaderToy semantics → "if never-stored, skip rgb" (vacuous). The same code in current semantics → "if hit or sky, skip rgb" (catastrophic — discards all surface and sky contributions).

## ShaderToy's actual "Delta #3" mechanism

The scope doc's Delta #3 description ("Bake's smoothstep merge feeds dead `.rgb` when α=0") is a paraphrase of the ShaderToy code's behavior, not a literal code feature. Looking at the ShaderToy merge (CubeA.glsl:196-219):

```glsl
// WeightedSample returns vec4(rgb, 1.) on visibility-success, vec4(0.) on rejection
vec4 S0 = WeightedSample(...);  // upper probe corner 0
vec4 S1 = WeightedSample(...);  // ...
vec4 S2 = WeightedSample(...);
vec4 S3 = WeightedSample(...);

// Bilinear in xyz, weighted by per-corner visibility (.w) in denominator
vec3 lastOutput = mix(mix(S0.xyz, S1.xyz, fx), mix(S2.xyz, S3.xyz, fx), fy)
                  / max(0.01, mix(mix(S0.w, S1.w, fx), mix(S2.w, S3.w, fx), fy));

if (!isnan(lastOutput.x))  // all-rejected guard
    Output.xyz = Output.xyz * l + lastOutput * (1. - l);
```

Mechanism: a **rejected** corner returns `vec4(0.)` — its `.xyz` does NOT contribute to the numerator AND its `.w` does NOT contribute to the denominator. The bilinear normalization automatically reweights surviving corners.

The "skip rgb on α=0" framing is shorthand for **"a rejected corner contributes zero to both numerator and denominator of the weighted-bilinear merge"** — a per-corner gating of the rgb sum, not a single threshold on the merged result.

## Current impl's analog (and what it's missing)

The current impl (radiance_3d.comp:660-687) uses `sampleUpperDirWeighted` to compute `upperDir.a` as a **scalar visibility fraction** (line 342: `wVisible / max(0.01, wTotalSpatial)`), then applies it as a **uniform attenuator** (line 760: `aFactor = upperDir.a`) on a separately-computed trilinear `.rgb` sum (line 661: `upperDirTrilinear = sampleUpperDirTrilinear(...)`).

The structural difference:

| Aspect | ShaderToy | Current |
|--------|-----------|---------|
| `.rgb` summation | Per-corner gated: rejected corner contributes 0 to numerator | Full trilinear average; all corners contribute even when rejected |
| Visibility weight | Per-corner: rejected corner contributes 0 to denominator → bilinear renormalizes | Scalar `aFactor`: rejection attenuates the entire merged rgb uniformly |
| Effect of one rejected corner | The other 3 corners' radiance contributes at full weight | All 4 corners' radiance contributes; the entire sum is attenuated by ~3/4 |

**Numerically these can differ substantially** when corners disagree (e.g., one corner near a lit wall while three see far field). The current impl smears the lit corner's radiance through the attenuation; the ShaderToy version discards the lit corner cleanly if it's the rejected one.

This is the **actual** Delta #3 mechanism, and it requires changes in two places:
1. `sampleUpperDirTrilinear` (lines 229-266) — make per-corner output gated, returning 0-rgb for rejected corners.
2. The merge call site (lines 661-687) — use the new per-corner-gated trilinear that bundles ShaderToy's normalization, or split into per-corner rgb + per-corner weight returns and renormalize at the merge call site.

## What this means for M1

- **The M1 Delta #3 work item as written ("single-shader change: skip rgb when α=0") is incorrect** and should not be implemented as written.
- **Two options for re-scoping:**
  1. **Redefine Delta #3** to "per-corner visibility-weighted bilinear merge" — a structural change touching `sampleUpperDirTrilinear` + merge call site. Likely closer to ShaderToy semantics. **Estimated effort: 1-2 sessions** (was budgeted at <1 session).
  2. **Park Delta #3** until M2 (Path B) where surface-attached topology makes the per-corner gating natural. Path A ships without #3.
- **Recommendation:** Option 1, with the impl doc front-loaded with this audit's findings. The per-corner gating is a meaningful mechanism (the failure-learnings showed the bright tail is structural; per-corner gating could legitimately attenuate it). Worth the extra session in M1.
- **The "skip rgb on α=0" framing must be retired from the scope doc** to prevent a future session from implementing the wrong thing. Updating §2 of the scope to reflect this audit.

## Sponza implications

Sponza has many highly-occluded corners (atrium pillars, second floor) — the per-corner rejection in the bilinear merge likely matters more for Sponza than for cornell. If Path A Delta #3 (redefined) lands STRONG on cornell but DEAD on Sponza, that would suggest per-corner gating isn't sufficient for complex occlusion — strong signal toward Path B for Sponza.

## What this audit does NOT verify

- That **per-corner gating** would actually close the bright tail (this is the M1 A/B's job to determine).
- That the WeightedSample visibility test itself is the right rejection criterion in volumetric topology — the test was designed for surface-attached probes; it may need re-derivation for volumetric.
- The exact volumetric-mapped formulation of WeightedSample (this is a separate concern — the current impl's `sampleUpperDirWeighted` is already a volumetric port, but its α output is the wrong shape for Delta #3's per-corner gating).
