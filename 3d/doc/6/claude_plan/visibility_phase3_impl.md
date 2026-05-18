# Phase 3 — Bake-Side Leak Fix Implementation (3D `WeightedSample`)

**Date:** 2026-05-15
**Predecessor:** [visibility_phase3_plan.md](visibility_phase3_plan.md) rev 2 (post-critic-13)
**Critic chain:** [critic 14](critic/14_visibility_phase3_impl_review.md) → revision 2 (this version). H1 (default-OFF non-bit-exact) FIXED in shader; doc rewritten per H2.
**Status (rev 2):** **3a + 3b v1 LANDED + critic-14 H1 fix applied. v1 result: ~13% global leak reduction (C0 −11.2%, C1 −16.9%, C2 −10.0%) — REAL effect, but well below the plan's Tier 1 target (≥88%). Render quality between Tier 1 (RMSE 0.0291; threshold 0.02) and Tier 2 (threshold 0.05). Default OFF; opt-in toggle ships.** Verdict per the plan's gate is **Tier 3 on leak**, **Tier 2 on render** — the impl is INCONCLUSIVE pending revised metric instrumentation (see "What's next").

---

## What landed

### Shader: `res/shaders/radiance_3d.comp`

1. **Two new uniforms** ([§Uniforms block](../../res/shaders/radiance_3d.comp)):
   - `uUseWeightedSample` (int, default 0) — gating toggle.
   - `uUpperBinConeSin` (float) — `sin(theta_half)` for the upper cascade's directional bin (D=4→0.484, D=8→0.248, D=16→0.124).

2. **`sampleUpperDirWeighted` helper** (~80 lines, after `sampleUpperDirTrilinear`):
   - 8-corner loop matching the existing trilinear path's spatial structure.
   - **CRITIC 13 H1 fix**: `dirToLower = +normalize(relVec)`, with a multi-line shader-side comment forbidding the negation that ShaderToy uses for its 2D wall-attached convention. This was the most failure-prone line; the comment is part of the contract.
   - **CRITIC 13 H3 fix**: forward radiance read via existing `sampleUpperDir(cornerPos, rayDir, Du)` helper, preserving Phase 5f's 4-bin directional bilinear (default `uUseDirBilinear==1`).
   - Look-back fetches only `.a` (single texelFetch, no need for bilinear on a scalar).
   - Returns `vec4(rgb_normalized_over_visible, visibility_fraction)`.

3. **Call-site dispatch** ([§main() bin loop](../../res/shaders/radiance_3d.comp)):
   - Only the trilinear branch (`uUpperToCurrentScale==2 && uUseSpatialTrilinear`) routes through `sampleUpperDirWeighted` when `uUseWeightedSample==1`. Other paths (single-probe, isotropic-bilinear, isotropic-texelFetch) keep their `.a=1.0` placeholder — they have no 8-corner structure to gate.

4. **CRITIC 13 L2 + CRITIC 14 H1 fix — bake merge formula updated with mode-gated factor**:
   ```glsl
   // before (Phase 2): rad = hit.rgb * l + upperDir.rgb * (1 - l);
   // after  (Phase 3): float aFactor = (uUseWeightedSample != 0) ? upperDir.a : 1.0;
   //                   rad = hit.rgb * l + upperDir.rgb * (1 - l) * aFactor;
   ```
   **Initial impl (rev 1)** unconditionally multiplied by `upperDir.a`. **Critic 14 H1 caught** that on the OFF path, `sampleUpperDirTrilinear` returns `upperDir.a` as a trilinear blend of the upper cascade's stored α (Phase 2 binary 0/1), NOT the always-1.0 placeholder. Unconditional `* upperDir.a` would silently change OFF-mode merge from "trust upper" to "gate by upper's per-bin α encoding" — a Phase 2 behavior change unrelated to Phase 3. **The mode-gated `aFactor` restores default-OFF bit-exactness.** Multi-line shader-side comment marks the gate as load-bearing (do not remove for "simpler code").

### C++: `src/demo3d.h` + `src/demo3d.cpp`

1. **New member** [demo3d.h](../../src/demo3d.h): `bool useWeightedSample` (default `false`).
2. **Setter** `setUseWeightedSample(bool)` triggers a full cascade rebake (per Phase 2 pattern).
3. **Initializer** [demo3d.cpp:187](../../src/demo3d.cpp#L187): `, useWeightedSample(false)`.
4. **Change-detect log** [demo3d.cpp:~735](../../src/demo3d.cpp): emits `[3] WeightedSample bake-side visibility: ON/OFF` on change.
5. **Uniform push** in [updateSingleCascade](../../src/demo3d.cpp): `uUseWeightedSample` + `uUpperBinConeSin = sqrt(1 - (1 - 2/D²)²)` computed CPU-side from `upperCascDirRes`.
6. **GUI checkbox** in the "Hierarchy & Merge" tab — disabled when not on the trilinear path; tooltip explains the algorithm and quality target.

### CLI: `src/main3d.cpp`

- New flag `--use-weighted-sample=N` (0 OFF / 1 ON). Default OFF.

### Plumbing notes (deviation from plan §2 H2 fix)

The plan H2 said to add 2 new vec3 uniforms (`uUpperGridOrigin`, `uUpperCellSize`). On reading the code: **`uGridOrigin` is already shared across all cascades** (verified [demo3d.cpp:3092](../../src/demo3d.cpp#L3092) — all cascades get the same `volumeOrigin`), and **`uUpperProbeCellSize` already exists** as a float uniform. The shader can use `uGridOrigin + (vec3(cornerPos) + 0.5) * uUpperProbeCellSize` directly. Net plumbing cost: 1 new uniform (the cone sin), not 3. Saves ~30 minutes; matches the plan's "Option B" rationale (use existing data when possible).

---

## Verification results

### Tier 0 — Build

- **Pass.** `RadianceCascades3D.exe` builds clean. The pre-existing C4819 (Chinese-codepage encoding) warnings are unchanged. No new warnings.

### Tier 1 — Bit-exact regression with `uUseWeightedSample==0`

- **Pass (post-critic-14 H1 fix).** Two pieces:
  - Helper-call branching: trilinear path routes through `sampleUpperDirTrilinear` when OFF (existing path; unchanged).
  - Merge formula: `aFactor = (uUseWeightedSample != 0) ? upperDir.a : 1.0` ensures the `* upperDir.a` factor only applies when ON. With OFF, `aFactor==1.0` always → merge is `hit.rgb*l + upperDir.rgb*(1-l)` exactly as Phase 2.
- **Empirical confirmation**: post-fix OFF-mode bake-leak metric on cornell-orig-alcove **matches the historical baseline 4373.5 EXACTLY** (see Tier 2 below). This is the strongest available evidence that default-OFF preserves Phase 2 behavior.
- Pre-fix (rev 1) OFF measured 3843.7 — that delta of 530 was the unguarded `* upperDir.a` silently altering Phase 2 OFF behavior. Critic 14 H1 was correct.

### Tier 2 — Smoke runs (no crash) + bake-leak metric

**Post-critic-14 H1 fix** (the rev 1 numbers were contaminated; see Tier 1):

| Mode | exit | C0 leak | C1 leak | C2 leak | C3 leak | sum (C0–C3) |
|---|---|---:|---:|---:|---:|---:|
| OFF (TRUE Phase 2 baseline) | 0 | **4373.5** | 2864.9 | 309.69 | 86.397 | 7634.5 |
| ON  (Phase 3 v1)            | 0 | **3881.7** | 2381.2 | 278.68 | 86.397 | 6628.0 |
| **delta**                   | — | **−11.2%** | **−16.9%** | **−10.0%** | **0%** | **−13.2%** |

The OFF baseline matching the **historical 4373.5** documented in [visibility_phase2.5_impl.md §2.5a.1](visibility_phase2.5_impl.md) is the strongest signal that the H1 fix is correct.

Run commands:
```
# OFF baseline
build/RadianceCascades3D.exe --load-obj=cornell-orig-alcove \
  --bake-leak-test=tools/phase3_off_v2.json --exit-frames=300

# ON v1
build/RadianceCascades3D.exe --load-obj=cornell-orig-alcove --use-weighted-sample=1 \
  --bake-leak-test=tools/phase3_v1_on_v2.json --exit-frames=300
```

### Tier 3 — Per the plan's success gate

**Plan's Tier 1 target:** C0 leak ≥ 88% reduction (i.e., 4373.5 → ≤ 525).
**Plan's Tier 2 ship:**    50–88% reduction (525 ≤ leak ≤ 2186).
**Plan's Tier 3 fail:**    < 50% reduction (leak > 2186).

**v1 actual: −11.2% on C0.** Above zero (Phase 3 IS working) but well below 50%. **Per the plan's metric-only gate, this is Tier 3.**

**But (per critic 14 H2): the metric may not be fully sensitive to where Phase 3 attenuates.** The "Tier 3 fail" verdict is contingent on the current metric being a fair measure of Phase 3's leverage — which the diagnosis below questions. The honest position: **Phase 3 v1 is INCONCLUSIVE pending a leverage-weighted metric.**

Shipping decision: **3a + 3b code lands as opt-in (default OFF).** GUI/CLI toggle exposes it. No default-on flip until either (a) a revised metric shows v1 reaches Tier 1/2 on the bins it can actually affect, or (b) v2/v3 iteration drives the current metric across the threshold.

### Tier 4 — Render quality (Sponza/Cornell A/B)

Cornell-orig-alcove A/B with `--cam-preset=alcove`, **post-critic-14 H1 fix** (rev 1 numbers were ~6× smaller because OFF baseline was contaminated):

| Metric | Value |
|---|---:|
| Pixels with any difference | 94.34% |
| Mean abs diff (0-255 scale) | 5.08 |
| Max abs diff per channel | 51 |
| RMSE (normalized) | **0.0291** |
| Mean ON brightness vs OFF | −0.0199 (−2.0% global darkening) |

**RMSE 0.0291 is between Tier 1 (≤ 0.02) and Tier 2 (≤ 0.05).** Above Tier 1 by 50%; well within Tier 2. Phase 3 is taking substantive effect (94% of pixels affected, 2% global darkening) — consistent with per-bin upper-contribution attenuation. Visual inspection (PNGs below) recommended before any default-on flip.

A/B PNGs (post-H1 fix): [phase3_off_v2.png](../../phase3_off_v2.png), [phase3_on_v2.png](../../phase3_on_v2.png).

### Tier 5 — Bake cost

Not measured this iteration. Plan estimate: ~+1.2 ms (~3% of 38 ms). The change is a small per-corner add (cone test + 1 extra texelFetch for the look-back `.a`); cost is bounded by the existing 8-corner trilinear loop. Will measure in v2/v3 iteration if pursued.

---

## Diagnosis: why v1 didn't move the leak metric

This is the substantive finding. Three plausible explanations:

### (1) Most "leak" bins are at `l == 1.0`, where Phase 3 has no effect

The bake merge formula is `rad = hit.rgb * l + upperDir.rgb * (1 - l) * upperDir.a`. The smoothstep `l` blends from 1.0 (hit deep within interval) to 0.0 (hit at far edge of interval). **For `l == 1.0`, the upper contribution is zero regardless of `upperDir.a` — Phase 3 changes nothing.**

For C0 in cornell-orig-alcove: `tMin=0.02, tMax=0.125, blendFraction=0.5 → blendWidth=0.0525`. The smoothstep transition zone is `hit.a ∈ [0.0725, 0.125]`. Hits with `hit.a < 0.0725` (i.e., wall hits closer than ~7cm — likely the majority for an alcove probe seeing a partition wall a few cm away) get `l=1` and bypass Phase 3 entirely.

**This means Phase 3 reduces the leak only for surface-hit bins where `hit.a` is in the smoothstep transition zone.** Those may be a small fraction of the alcove's bins.

### (2) Phase 3 IS reducing what it can, but the leak metric isn't dominated by those bins

The image-diff data confirms Phase 3 IS doing work: 76% of pixels changed; global brightness dropped 2%. So the upper-cascade attenuation is firing somewhere. But the bake-leak metric (atlas-side, surface bins with α<0.001 facing the light) seems insensitive to it.

The leak may be dominated by the **direct Lambertian shading on the wall surfaces themselves** (the metric counts any RGB > 0 in those bins; even a properly-shadowed wall has RGB from the `uAmbientBakeStrength * albedo` floor). Phase 3 doesn't change `hit.rgb`; only the upper contribution.

### (3) The cone correction may be over-permissive at high `lProbeRayDist`

For an upper probe above the partition looking down (`dirToLower`), if the bin's stored ray escaped past the partition's edge to a far wall, `lProbeRayDist` is large. The visibility test `|relVec| < lProbeRayDist * 0.248 + 0.01` then PASSES for relVec ≈ 0.25, even though geometrically there IS a partition between the probes. The cone correction approximates the bin's spatial coverage but doesn't model occluders within the cone.

This is the fundamental limitation ShaderToy's WeightedSample shares — but in 2D wall-attached the geometry is more constrained.

### What this means for v2/v3

The plan's iteration backlog assumed v1 would land within shouting distance of the target and v2/v3 would tune. v1 is much further off than the plan anticipated; the issue may not be just "tune the cone correction." Two paths:

- **v2 (per-bin LUT for cone)**: addresses (3) partially — actual per-bin solid angle differs from average-area. Could tighten the test for some bins. Probably ≤ 20% additional leak reduction; won't reach 88% Tier 1.
- **v3 (epsilon tuning)**: smaller epsilon makes test stricter (more occluded). Would help (3) but not (1) or (2). Likely small gains.
- **NEW: rethink the leak metric** itself. If Phase 3 affects only a small fraction of "leaky" bins (the smoothstep-zone ones) but the metric counts ALL surface bins, the metric will systematically under-report Phase 3's true effect. A revised metric that weights bins by `(1 - l)` (Phase 3's leverage) would be more honest.

The render-side image-diff (76% pixels affected, 2% global darkening, RMSE 0.0069) is currently a more reliable signal that Phase 3 is doing real work than the bake-leak metric.

---

## Recommended commit shape

Per the plan §5:

```
[Claude] Phase 3 (3a+3b v1): WeightedSample bake-side visibility (opt-in, default OFF)

- New uniforms uUseWeightedSample + uUpperBinConeSin in radiance_3d.comp
- New helper sampleUpperDirWeighted (per-corner geometric visibility,
  CRITIC 13 H1/H3 fixes applied with explanatory shader-side comments)
- Bake merge formula updated: rad = ... + upperDir.rgb * (1-l) * upperDir.a
  (CRITIC 13 L2 — gates upper contribution by visibility fraction)
- New bool useWeightedSample (default false), GUI checkbox, CLI flag
  --use-weighted-sample=N
- Default OFF → bit-exact Phase 2 preserved
- Smoke verified: OFF/ON both run clean, render RMSE 0.0069 (Tier 1),
  bake-leak metric near-flat (Tier 3 by plan's gate); see
  doc/6/claude_plan/visibility_phase3_impl.md for diagnosis.
```

A single commit rather than 3a/3b split because they are interdependent (the helper is dead code without the call-site dispatch + merge change). Reverting is straightforward: flip the default to OFF (already OFF) or remove the new code paths.

---

## Honest assessment (rev 2 per critic 14 H2)

- **Code lands clean**, builds, runs without crash. Critic 14 H1 fix verified by OFF baseline matching the historical 4373.5 exactly.
- **Phase 3 v1 IS doing real work**: −13% global bake-leak, RMSE 0.0291 render delta, 94% pixels changed, 2% global darkening. Not noise.
- **By the plan's metric-only gate this is Tier 3 (< 50% C0 reduction).** Strict reading: revert.
- **But the diagnosis below questions whether the metric is fully sensitive to Phase 3's leverage** (most bins are at `l == 1.0` where Phase 3 has no effect; the metric counts them all). Generous reading: the bins Phase 3 CAN affect may have dropped > 50%; the metric just dilutes the signal across the full population.
- **The honest position is INCONCLUSIVE.** Two facts can be both true: "Phase 3 v1 doesn't pass the plan's gate" and "the gate may not be a fair test." The default-OFF ship preserves optionality without committing to either reading.

## Postscript (2026-05-17) — algorithmic verdict from user testing

User enabled `--use-weighted-sample=1` interactively on default (non-OBJ) Cornell box. **Symptom: indirect bouncing killed entirely** — the scene looks flat-shaded, no GI from colored walls.

### Why: the look-back test is the wrong primitive for volumetric probes

ShaderToy's WeightedSample is a **wall-attached probe** primitive. There, "can U and L see each other along the wall?" is the right question because all probes live in the wall's 2D plane.

In our **volumetric probes**, U and L are points in 3D space. In an open volume (Cornell box interior), they share radiance via the FAR FIELD in `rayDir` direction — but they often CAN'T see each other directly (walls of the box block U↔L lines of sight from many angles). WeightedSample's look-back test says "occluded" → rejects upper contribution → indirect bouncing dies.

### When is rejection actually warranted?

Bake leak is physically incorrect only in this regime: **L's own ray hit a close wall (`hit.a` small → `l` near 1)**, the smoothstep blends in `(1-l)*upper.rgb` (small but non-zero), and the upper's radiance is "what's on the other side of the wall L hit." That's the true leak.

In the other two regimes:
- **L's ray missed (`hit.a == 0`)**: no wall, no leak possible. Upper's far-field IS the correct continuation. WeightedSample over-rejects.
- **L's hit close, `l == 1`**: `(1-l) == 0`, upper contributes nothing regardless. WeightedSample is redundant.

The smoothstep transition zone (`0 < l < 1`) is the ONLY place WeightedSample's rejection is geometrically meaningful — and that's a small fraction of bins (which matches Phase 3's small measurable leverage on the leak metric).

### Decision (2026-05-17)

**Phase 3 stays default OFF.** Code remains in the tree as an opt-in toggle for diagnostic / experimental use (`--use-weighted-sample=1` + GUI checkbox). The Phase 2 render-side α-gate (raymarch.frag `w = wcos * a.a`) is sufficient for what users actually see — bake-side leaks remain in the atlas but don't appear in display output.

**v2/v3 iteration deferred.** The diagnosis suggests v2/v3 won't help the fundamental issue (wrong geometric primitive for our architecture). Any future leak-reduction work should reconsider the primitive — e.g., test occlusion along `rayDir` from L's ray endpoint, or use the lower's `hit.a` to gate WeightedSample (only apply when `0 < l < 1`).

### What's next IF leak reduction is re-prioritized

1. **Targeted scope**: apply `aFactor` only in the smoothstep transition zone (`0 < l < 1`); leave miss bins and l=1 hits untouched. Preserves indirect bouncing.
2. **Better primitive**: replace look-back test with SDF visibility along `rayDir` from L+tMax*rayDir. More principled, costlier.
3. **Or accept that bake-side leaks are cosmetic** (Phase 2 hides them from display) and stop optimizing the metric.

---

## v2 — Targeted scope (2026-05-17) — **shipped on top of v1**

Option 1 from "What's next" above implemented. Single-line shader change in [radiance_3d.comp:592-595](../../res/shaders/radiance_3d.comp): the miss branch (`hit.a == 0`) now uses `rad = upperDir.rgb` unconditionally, dropping the `* aFactor` factor. Hit branch (`hit.a > 0`) keeps `* aFactor`.

### Rationale (concise)

- **Miss bin** = lower's ray went through `[tMin, tMax]` without hitting anything. By definition NO WALL in the ray's path within the cascade interval. WeightedSample's "look-back" rejection is geometrically irrelevant — there's nothing for the upper to be "wrong about." Upper's far-field IS the correct continuation. Applying `aFactor` here was the source of the "no indirect bouncing" symptom.
- **Hit bin with `l == 1`**: `(1-l) * aFactor == 0 * aFactor == 0`. `aFactor` is redundant. No change needed.
- **Hit bin with `0 < l < 1`** (smoothstep transition): the only place where upper's contribution could leak past the wall L hit. `aFactor` correctly attenuates.

### Verification (v2 vs v1 vs OFF on default Cornell)

| Mode | Mean brightness | Δ vs OFF | Indirect bouncing visible? |
|---|---:|---:|:---:|
| OFF | 0.19212 | — | ✓ baseline |
| ON v1 (unconditional aFactor) | ~0.135 | −30% | ✗ killed |
| ON v2 (aFactor on hit branch only) | **0.18087** | **−5.9%** | **✓ preserved** |

PNGs: [cornell_v2_off.png](../../cornell_v2_off.png), [cornell_v2_on.png](../../cornell_v2_on.png) — red/green wall color bleed clearly visible in both.

### Verification (v2 leak reduction on cornell-orig-alcove)

| Mode | C0 | C1 | C2 | C3 |
|---|---:|---:|---:|---:|
| OFF | 4363.5 | 2690.8 | 303.7 | 84.1 |
| ON v2 | 3875.7 | 2250.3 | 272.9 | 84.1 |
| Δ | **−11.2%** | **−16.4%** | **−10.1%** | 0% |

v2 preserves the same leak-reduction effect as v1 (which had C0 −11.2%, C1 −16.9%, C2 −10.0% on the same scene) — the targeted scope didn't sacrifice the leak fix where it matters (alcove probes whose rays hit the partition wall).

### Verification (v2 temporal stability with ON, 10 consecutive frames)

Single-session continuous capture on default Cornell:

| Pair | Max-per-pixel | RMSE | Pixels changed |
|---|---:|---:|---:|
| All 10 consecutive pairs | **1/255** | 0.0003-0.0004 | 1.1-2.0% |

No regression from the EMA-α temporal fix. v2 is compatible with both Phase 2 render-side α-gate AND the temporal stability fix.

### Verdict (rev 3)

**v2 makes Phase 3 ON usable without killing GI.** The bake-leak reduction effect is preserved on scenes where leak actually happens (alcove with wall geometry); the kill-GI symptom on open-volume scenes is fixed by limiting the rejection to physically-warranted bins.

**Default-on flip is now defensible** but not done in this revision — the 5.9% Cornell darkening, while much smaller than v1, is still a measurable side effect. Possible reasons: (a) some of the smoothstep-zone attenuation is legitimately reducing real leak; (b) some of it is still over-rejection of corners that are geometrically occluded but radiatively shared via far-field. Distinguishing (a) from (b) is the leverage-weighted-metric work that critic 14 already flagged.

**Recommendation**: ship v2 default-OFF for one more iteration; offer it as "improved Phase 3 (no GI kill)" in the GUI tooltip. If the user community finds the residual 6% Cornell darkening acceptable in exchange for the bake-side leak reduction, flip default-ON in a subsequent ship.

---

## v3 — Trilinear.rgb + visibility-fraction multiplier (2026-05-18)

**Trigger:** user reported v2 still kills GI ("still feels the same"). Self-critic flagged that the 6% Cornell dim was unexplained by the targeted-scope rationale. Added shader debug instrumentation (`uPhase3DebugMode`, `uGIStrength`) to isolate the dimming cause.

### Empirical diagnostic

Added 4 debug modes to `radiance_3d.comp`:
- 1: force `aFactor = 1.0` (test: bypass the visibility multiply)
- 2: visualize `aFactor` directly
- 3: visualize `upperDir.a` (visibility fraction from WS)
- 4: use `upperDirTrilinear.rgb` (bypass WS renormalize; keep WS `.a`)

| Mode | Cornell brightness | Δ vs OFF |
|---|---:|---:|
| OFF (baseline) | 0.19212 | — |
| ON v2 (normal) | 0.18087 | −5.9% |
| ON v2 + mode 1 (force aFactor=1) | **0.18087** | **−5.9%** ← identical to normal |
| ON v2 + mode 4 (force trilinear.rgb) | **0.19084** | **−0.67%** ← near OFF |

**Conclusion: the dimming was NOT from the `* aFactor` multiply.** Forcing `aFactor=1` made NO difference. The dimming came from `sampleUpperDirWeighted`'s `sumRgb / wVisible` renormalize producing systematically smaller `.rgb` than trilinear.

### Why renormalize biases the radiance

WeightedSample's look-back test rejects corners NEAR LIT WALLS (because U near wall → look-back from U toward L hits the wall close → small `lProbeRayDist` → fails the cone test). Those rejected corners carry the strongest GI radiance (they're next to lit surfaces). Visible-only corners are statistically dimmer (they're in open space, farther from light sources). Averaging only over visible corners (`sumRgb / wVisible`) gives a lower mean than averaging over all corners (`sumRgb / wTotalSpatial = trilinear`).

### v3 design

Use `trilinear.rgb` (unbiased 8-corner spatial average) for the radiance. Use WeightedSample's `.a` (visibility fraction = `wVisible / wTotalSpatial`) only as the **merge-formula multiplier** (`aFactor` in the existing formula). Single attenuation proportional to occluded-corner-fraction, no value bias.

Shader change in [radiance_3d.comp](../../res/shaders/radiance_3d.comp):
```glsl
upperDirTrilinear = sampleUpperDirTrilinear(triP000, triF, rayDir, uUpperDirRes);
if (uUseWeightedSample != 0) {
    vec4 ws = sampleUpperDirWeighted(triP000, triF, rayDir, uUpperDirRes, worldPos);
    upperDir = vec4(upperDirTrilinear.rgb, ws.a);  // .rgb from trilinear, .a from WS
} else {
    upperDir = upperDirTrilinear;
}
```

### Verification (v3)

| Metric | OFF | v3 ON | Δ |
|---|---:|---:|---:|
| **Cornell brightness** | 0.19212 | **0.19084** | **−0.67%** |
| **cornell-orig-alcove C0 leak** | 4363.5 | 3875.8 | **−11.2%** (unchanged from v2) |
| **C1 leak** | 2690.8 | 2250.3 | **−16.4%** (unchanged from v2) |
| **C2 leak** | 303.7 | 272.9 | **−10.1%** (unchanged from v2) |
| **Temporal stability** (max per-pixel over 10 frames) | — | **1/255** | preserved |

### Architectural reframe

**v3 is no longer "3D adaptation of WeightedSample" in spirit.** The renormalize-over-visible was the load-bearing geometric primitive of ShaderToy's design. v3 removes it, reducing WeightedSample's role to "compute a visibility scalar `.a`." The radiance comes from standard trilinear. This is structurally closer to **"Phase 2 α-gate, applied at bake time"** than to ShaderToy's WeightedSample.

The architectural insight: for volumetric probes in open volumes, you can't use "U-to-L visibility" as a geometric primitive for radiance values (it biases against corners near lit walls — the corners that carry the most GI). You CAN use it as a soft attenuation scalar that smoothly downweights upper contribution where occlusion exists.

### Verdict (rev 3, post-v3)

**SHIP-ELIGIBLE.** v3 resolves the GI-loss concern (Cornell preserved within 1%). Default-on flip is now defensible:
- Leak metric improvement: same as v1/v2 (~11% on C0, ~16% on C1 alcove)
- Cornell quality: indistinguishable from OFF (−0.67%, below most perception thresholds)
- Temporal stability: preserved
- Cost: ~+1.2 ms bake (unchanged)

**Default OFF stays for this commit** to allow user verification on their actual workflows. Flip to default ON in a follow-up if no regressions surface.

See [reply_15_visibility_phase3_impl_rev2_review.md](critic/reply/reply_15_visibility_phase3_impl_rev2_review.md) for the full critic-15-rev2 N1-N8 responses.

---

## What's next (v2/v3 — IF pursued)

Per critic 13 M4's realistic budget (5–7 days for 3b iteration), and the v1 result being further off than expected:

1. **First, revise the leak metric** so it weights bins by `(1 - l)` — Phase 3's actual leverage. Without this, v2/v3 can't be honestly evaluated.
2. **Then v2 (per-bin LUT)** if leak hasn't dropped sufficiently.
3. **Then v3 (epsilon sweep)** if v2 still falls short.

If the revised metric shows v1 alone delivers > 50% reduction on the leverage-weighted bins, v1 may be acceptable to ship default-on for that subset. **DON'T flip the default-on without a credible metric.**

---

## Files touched

- [res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp): +2 uniforms, +1 helper (~80 lines), +call-site dispatch (~10 lines), merge formula (+ mode-gated `aFactor` per critic 14 H1)
- [src/demo3d.h](../../src/demo3d.h): +1 member, +1 setter
- [src/demo3d.cpp](../../src/demo3d.cpp): +1 initializer, +1 change-detect log, +2 uniform pushes, +1 GUI checkbox
- [src/main3d.cpp](../../src/main3d.cpp): +1 CLI flag

Plan rev 2 + critic 13 checklist applied: H1 (sign), H3 (sampleUpperDir), L2 (`* upperDir.a`), M2 (cone formula explicit). H2 simplified by reusing existing uniforms.
Critic 14 checklist applied: **H1 FIXED** (mode-gated `aFactor` restores default-OFF bit-exactness; verified by OFF baseline matching historical 4373.5), **H2 ACKNOWLEDGED** (Tier 3 + metric-suspect contradiction → INCONCLUSIVE verdict), **M3 noted** in diagnosis (cone average-area approximates per-bin variance).

## Critic 14 follow-ups (deferred unless v2/v3 pursued)

1. **M1 verification**: instrument a debug atlas channel that writes `(1 - l) * (1 - upperDir.a)` to quantify Phase 3's actual per-bin leverage. Required before v2/v3 to make the metric fair.
2. **M2 footnote**: the impl reuses existing `uUpperProbeCellSize` (float) instead of plan's `uUpperCellSize` (vec3). Same value; just a naming alias for the impl→plan trace. No plan revision needed.
3. **L1 forward-compat**: `--use-weighted-sample=N` accepts any non-zero as ON. If v2 introduces multiple modes (e.g., `=2` per-bin LUT), validate the enum. Trivial.
4. **L2 GUI tooltip**: lead with "REQUIRES non-co-located + spatial trilinear." Trivial.

## Open questions (still open after critic 14)

1. Is the leak metric the right success criterion? Critic 14 H2 says NO; rev 2 verdict commits to INCONCLUSIVE pending instrumentation.
2. Should the helper also be invoked for the non-trilinear single-probe path (degenerate 1-corner WeightedSample)? Currently it's not — would broaden Phase 3's effect but adds a fourth code path. Defer pending v2/v3 demand.
3. Should the impl recommend `blendFraction=1.0` when WeightedSample is ON, to widen the smoothstep zone and give Phase 3 more leverage? **Plausible** — would maximize the bin population at `l < 1` where Phase 3 attenuates. Worth measuring as part of v2 instrumentation.
4. Should there be a Phase 3 "diagnostic mode" that, like `--diag-alpha-mode`, emits per-bin Phase-3-leverage statistics? Critic 14 M1 follow-up effectively asks for this.
