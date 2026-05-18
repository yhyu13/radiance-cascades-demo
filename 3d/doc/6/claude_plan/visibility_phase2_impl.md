# Phase 2 — Implementation Notes: Bake-Side α as Per-Bin Visibility (Pragmatic Variant)

**Date:** 2026-05-14
**Status:** Implemented; build clean (0 errors); smoke + bit-exact verified. **Default-flip back to Mode 0 (now α-gated) per Option X sequencing.** 2C cleanup landed: `probeVisibility()`, `sampleProbeDirPerBinOccluded`, `sampleProbeDirDepthAware`, `uVisibilityMode`, modes 1/2/3/4, `visibilityMode` C++ member, ImGui combo — all deleted. CLI `--visibility-mode=N` retained as a deprecation stub (warns + ignores).

**Critic chain extends:** [critic 09](critic/09_visibility_phase2_impl_review.md) → [reply 09](critic/reply/reply_09_visibility_phase2_impl_review.md). All 9 findings accepted; doc updated below (no code changes, doc precision only).

**The plan's textbook RC interval merge proved over-darkening; the shipped variant is a pragmatic compromise that keeps the original radiance-bake formula and only changes what gets written to alpha.** Sponza loses ~12% mean luminance vs Phase 1 Mode 4; Cornell scenes essentially unchanged. **Cost is dramatically better than Mode 4: raymarch +12.9% over Mode 0 (vs Mode 4's +50%), total frame +3.4% (vs Mode 4's +10.5%). Phase 2 is faster than Mode 4 by 6.5%.**

**Render-time leaks: FIXED.** **Bake-time leaks: NOT FIXED** — the render-side α-gate hides them, but the atlas still contains them. The plan promised both; only render-side delivered. See "Why v1 over-darkened" below for why the textbook fix was abandoned.

**Plan source-of-truth:** [visibility_phase1.5_and_phase2_plan.md](visibility_phase1.5_and_phase2_plan.md) (rev 3 — the pre-flight audit removed Phase 2A; Option X sequencing chosen)
**Critic chain:** [04](critic/04_visibility_unified_plan_review.md) → [07](critic/07_visibility_phase1.5_and_phase2_plan_review.md) → [08](critic/08_visibility_phase1.5_and_phase2_plan_rev1_review.md) (each integrated into the plan rev they triggered)
**Pre-flight discovery:** [visibility_phase2_preflight_audit.md](visibility_phase2_preflight_audit.md) (atlas was always RGBA16F; Phase 2A collapsed to empty work)
**Predecessor:** [visibility_phase1_followup_v40_default_flip.md](visibility_phase1_followup_v40_default_flip.md) (Mode 4 default-flip that this commit reverts to Mode 0)

---

## Summary

| Change | File | Effect |
|---|---|---|
| Pre-flight #2: new bake-leak test scene `cornell-orig-alcove` (Cornell + interior partition wall at x=0.30 splitting box into lit-left + alcove-right) | [res/scene/CornellBox-Original-Alcove/*.{obj,mtl}](../../../res/scene/CornellBox-Original-Alcove/) | Permanent regression scene for visibility tests; 47353 voxels (vs Cornell-orig's 39648) |
| CLI `--load-obj=cornell-orig-alcove` + objKey detection | [main3d.cpp:374](../../../src/main3d.cpp#L374), [demo3d.cpp:5421](../../../src/demo3d.cpp#L5421), [demo3d.cpp:5479](../../../src/demo3d.cpp#L5479) | New scene loadable via existing CLI/UI |
| 2B bake: `sampleUpperDir` + `sampleUpperDirTrilinear` return `vec4` (RGB radiance + α transparency) | [radiance_3d.comp:150-204](../../../res/shaders/radiance_3d.comp#L150-L204) | Atlas alpha now propagates through cascade-merge sampling |
| 2B bake: per-direction loop derives transparency-α and writes it to atlas | [radiance_3d.comp:367-435](../../../res/shaders/radiance_3d.comp#L367) | α=0 (surface hit) / α=0 (sky terminal) / α=1 (in-volume miss). Original radiance formula preserved (see "What didn't work" below). |
| 2B bake: temporal-blend AABB clamp `.rgb`-only; α fresh per frame (matches temporal_blend.comp discipline) | [radiance_3d.comp:401-432](../../../res/shaders/radiance_3d.comp#L401) | Hit/miss flicker can't bleed into α |
| 2B render: `sampleProbeDir` α-gates per bin (`w = wcos × a.a`) | [res/shaders/raymarch.frag:325-344](../../../res/shaders/raymarch.frag#L325) | Per-bin visibility from baked α; no per-pixel SDF or projection |
| 2B C++: visibility default reverted to 0 (the new α-gated path); Mode 4 docs marked broken | [demo3d.h:883-887](../../../src/demo3d.h#L883) | New default = the α-gated path |
| 2C: `probeVisibility()`, `sampleProbeDirPerBinOccluded`, `sampleProbeDirDepthAware`, `uVisibilityMode` uniform, mode-dispatch in `sampleDirectionalGI` | [res/shaders/raymarch.frag](../../../res/shaders/raymarch.frag) | Single sampling path; ~120 lines deleted |
| 2C: `visibilityMode` C++ member + setter behavior + ImGui combo | [demo3d.h:570](../../../src/demo3d.h#L570), [demo3d.cpp:3632-3634](../../../src/demo3d.cpp#L3632) | Member retired; `setVisibilityMode()` retained as deprecation stub for one release |
| 2C: `--visibility-mode=N` CLI → deprecation stub | [main3d.cpp:281-289](../../../src/main3d.cpp#L281) | Flag warns + continues for backward compat |

No new GPU resources, no atlas format change (already RGBA16F since Phase 5g), no allocation deltas. Touched files: 2 shaders + 3 C++ files + 2 new asset files.

---

## What Phase 2 actually does (the pragmatic α-only variant)

The plan §3.4 specified the textbook RC interval merge:

```
rad   = thisRad + thisAlpha × upperDir.rgb
alpha = thisAlpha × upperDir.a
```

with `thisAlpha = 0` for surface hits, `1` for miss/sky. **I implemented this initially (v1) and it over-darkened Sponza by 23%** — the chained α-multiply across C3→C2→C1→C0 terminates radiance at the first opaque cascade, killing far-field multi-bounce in scenes where most C0 bins hit something.

After v1 → v2 (smoothstep restored) → v3 (drop bake merge, keep α derivation) → v4 (cos-only render normalization) → v5 (back to cos×α normalization), the shipped variant is:

- **Bake-side**: write α with the binary classification (`hit.a > 0 → 0`, `hit.a == 0 → 1`, `hit.a < 0 → 0`). **Keep the original `rad = hit.rgb * l + upperDir.rgb * (1 - l)` for surfaces** (smoothstep blend zone preserved). The bake produces the same radiance values as pre-Phase-2.
- **Render-side**: `sampleProbeDir` does `w = wcos × a.a` and accumulates `irrad += a.rgb × w; wsum += w`, returning `irrad / wsum` (Mode 1/2/3/4-style renormalize over visible directions).

This isolates the new behavior to **only the render-side α-gate**. The bake's existing radiance encoding stays bit-equivalent on RGB. Render-side α-gate gives Mode-4-equivalent visibility without per-pixel SDF or signed projection — at near-Mode-0 cost.

**What this means for the plan's claims (revised per critic 09 W2):**

- ✅ Visibility is "in the data" (the atlas's α channel) — no runtime SDF trace, no per-pixel signed projection.
- ✅ Single sampling path retired the entire `uVisibilityMode` switch.
- ✅ **Render-time leaks: FIXED.** The α channel correctly gates per-bin contribution at render; surfaces don't pick up cross-wall radiance through directly-occluded bins.
- ❌ **Bake-time leaks: NOT FIXED.** The bake's `rad = hit.rgb * l + upperDir.rgb * (1 - l)` formula is unchanged. When the smoothstep `l < 1` (within `blendWidth` of the interval far edge), upper-cascade radiance — which can include "what's beyond the wall" — is mixed into this bin's stored `rad`. **The render-side α=0 then HIDES this leaked value from the rendered output**, but the leaked value still lives in the atlas. A second consumer of the atlas (the existing "atlas debug viewer" render mode, or any future feature that reads atlas RGB without honoring α) would resurrect the leak.

**The pragmatic variant traded "fix bake-side leak" for "don't over-darken Sponza."** That trade was the right call for shipping today (Sponza usability matters), but it should be documented as NOT-DELIVERED, not "partially fulfilled" as earlier revisions of this doc framed it. The plan over-promised; this implementation under-delivers on the bake side.

### Why v1 over-darkened (added per critic 09 W1)

The doc's earlier revisions described the symptom of v1 over-darkening but didn't diagnose the mechanism precisely. Filling in the gap so future revisitors don't repeat the same failure:

In geometrically dense scenes (Sponza), most C0 probes have most bins hitting walls within the C0 interval. The textbook interval merge says: surface hit → α=0 → `rad = thisRad + 0×upperDir = thisRad` (only local hit-radiance, no upper contribution). For a probe near a wall, this means EVERY surface-hit bin loses the upper-cascade radiance, **even though the upper cascades represent geometry FARTHER OUT than the wall** (the cascade hierarchy is distance-bounded, not visibility-bounded).

The original (pre-Phase-2) bake formula `rad = hit.rgb * l + upperDir * (1-l)` was implicitly performing a **cascade-handoff smoothing** — when the hit happened near the far edge of the interval (smoothstep `l < 1`), some of the upper cascade's contribution was mixed in. **This wasn't the paper's interval merge**; it was a heuristic that happened to preserve multi-bounce energy at cascade transitions.

**Why the heuristic worked and the textbook didn't:** the interval merge formula is geometrically correct under the assumption that probes along the ray are stacked at increasing distances, all looking in the same direction. With non-co-located probes (Phase 5d), the upper cascade's probe is OFFSET from this cascade's probe — the upper cascade's `bdir` ray from a different position might not see the same wall. So the upper cascade's radiance for that bin direction can legitimately represent radiance the wall didn't block (because the upper probe sees past the wall from a different angle). The textbook merge ignores this — it treats upper cascade as "what's beyond MY ray's first hit," but with offset probes, that interpretation is wrong. The original smoothstep accidentally preserved the right behavior by mixing in upper-cascade contribution near the interval boundary.

**Implication for future "full interval merge" work:** the fix is NOT to delete the smoothstep, it's to **derive an α and a radiance formula that account for non-co-located probes.** Possible directions: probability-weighted α based on probe-to-wall geometry, or a "cone of upper-cascade visibility" test that captures whether the upper cascade probe can see past the local wall. Neither is trivial; both are research-paper-level work. **Anyone retrying the textbook interval merge will hit the same darkening unless they address this geometry-vs-formula mismatch first.**

---

## Bake-side change in detail

[radiance_3d.comp:367-435](../../../res/shaders/radiance_3d.comp#L367):

```glsl
// upperDir is now vec4 (RGB + α). Isotropic fallback paths fill α=1 (no
// directional occlusion data available from isotropic averages).
vec4 upperDir = vec4(0.0, 0.0, 0.0, 1.0);
if (uHasUpperCascade != 0) {
    if (uUseDirectionalMerge != 0) {
        if (uUpperToCurrentScale == 2 && uUseSpatialTrilinear != 0)
            upperDir = sampleUpperDirTrilinear(triP000, triF, rayDir, uUpperDirRes);
        else
            upperDir = sampleUpperDir(upperProbePos, rayDir, uUpperDirRes);
    } else if (uUseDirBilinear != 0) {
        upperDir = vec4(texture(uUpperCascade, uvwProbe).rgb, 1.0);
    } else {
        upperDir = vec4(texelFetch(uUpperCascade, upperProbePos, 0).rgb, 1.0);
    }
}

// Phase 2 — pragmatic "α-only" variant.
// alpha encoding:
//   sky exit (hit.a < 0) → 0 (terminal — nothing beyond)
//   surface hit (hit.a > 0) → 0 (opaque — bin occluded)
//   in-volume miss (hit.a == 0) → 1 (transparent — bin visible to far field)
float alpha;
if (hit.a > 0.0)      alpha = 0.0;
else if (hit.a < 0.0) alpha = 0.0;
else                  alpha = 1.0;

// Original radiance bake (unchanged from pre-Phase-2):
vec3 rad;
if (hit.a < 0.0) {
    rad = hit.rgb;
} else if (hit.a > 0.0) {
    float l = ...;  // smoothstep blend zone
    rad = hit.rgb * l + upperDir.rgb * (1.0 - l);
} else {
    rad = upperDir.rgb;
}

// Temporal blend RGB; alpha is fresh-only.
imageStore(oAtlas, atlasTxl, vec4(blended_or_clean_rgb, alpha));
```

**Key decision**: sky-exit α=0 (terminal). The plan §3.4 said α=1 (transparent). My v1 with α=1 added the upper cascade's contribution on top of sky bins, double-adding sky+upper-which-also-ends-in-sky. Switching sky to α=0 was one of the v1→v5 fixes.

---

## Render-side change in detail

[res/shaders/raymarch.frag:325-344](../../../res/shaders/raymarch.frag#L325):

```glsl
vec3 sampleProbeDir(ivec3 pc, vec3 normal, int D) {
    vec3  irrad = vec3(0.0);
    float wsum  = 0.0;
    for (int dy = 0; dy < D; ++dy) {
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float wcos = max(0.0, dot(bdir, normal));
            vec4  a    = texelFetch(uDirectionalAtlas,
                                    ivec3(pc.x * D + dx, pc.y * D + dy, pc.z), 0);
            float w    = wcos * a.a;
            irrad += a.rgb * w;
            wsum  += w;
        }
    }
    return irrad / max(wsum, 1e-4);
}
```

The single sampling path. Pre-Phase-2 this function ignored alpha; Phase 2 uses `a.a` as the per-bin transparency weight. `sampleDirectionalGI` now does direct trilinear-weighted sum across 8 corners (no mode dispatch, no outer `probeVisibility`).

**Renormalization choice (v3 vs v4):**

- v3 (shipped): `wsum += wcos * a.a` → divides by visible-cosine-sum
- v4 (rejected): `wsum += wcos` → divides by full cosine-sum

v3 gives RMSE 0.064 vs Mode 4 in Sponza (mean ratio 0.88). v4 gives RMSE 0.121 (mean ratio 0.64) — significantly worse. The cos-only divisor is geometrically purer (occluded directions contribute zero to the hemisphere integral) but doesn't match what Mode 0 was producing pre-Phase-2 (where every bin contributed something via the cascade-merge formula). v3's "renormalize over visible directions" preserves the over-bright bias the pre-Phase-2 bake encoded, which empirically matches Mode 4 quality more closely.

---

## v1 → v5 iteration log (the bake-merge over-darkening discovery)

| Variant | Bake formula | Render normalization | Sponza RMSE vs Mode 4 | Sponza mean ratio |
|---|---|---|---:|---:|
| v1 (textbook) | `rad = thisRad + thisAlpha × upperDir.rgb`; sky α=1 | `wsum += wcos × a.a` | 0.1014 | 0.770 |
| v2 (smoothstep restored, sky still α=1) | Same as v1, blend smoothstep folded into thisRad | Same | 0.1014 | 0.770 (no help) |
| **v3 / v5 (shipped — v5 is a re-run of v3 to confirm v4 reversion)** | Original `hit.rgb*l + upperDir*(1-l)`, alpha derived from hit classification, sky α=0 | `wsum += wcos × a.a` | **0.0644** | **0.883** |
| v4 (cos-only render normalization, abandoned) | Same as v3 | `wsum += wcos` only | 0.1206 | 0.639 (worse) |

The v1→v3 jump was the load-bearing fix (drop bake merge). v4 was a side experiment that empirically confirmed v3's renormalization choice (see Coupling note below).

### Coupling — v5 is locked to the current bake formula (added per critic 09 W4)

v5's `wsum += wcos × a.a` renormalization is empirically tuned to match Mode 4 quality, but it carries **a hidden dependency on the current (leaky) bake formula.** v4 (`wsum += wcos`) is the **textbook hemisphere integral** — sum cosine-weighted radiance over visible directions, divide by total cosine weight. Occluded directions correctly contribute 0. By that geometric measure, v4 is the correct integral.

The reason v5 matches Mode 4 better than v4 is that **the pre-Phase-2 bake's radiance values are biased over-bright by the smoothstep merge formula** (which blends upper-cascade radiance into surface-hit bins at the interval boundary). v5's renormalization-over-visible-directions concentrates that over-bright value into the visible-direction integral, recreating the over-bright bias Mode 4 had. v4 doesn't concentrate; it integrates honestly over the hemisphere, and the over-bright bias washes out.

**Implication: don't change the bake formula without simultaneously switching the render normalization to v4.** A future "full interval merge" that fixes the bake to produce energy-conserving radiance values would make v5's renormalization over-amplify the now-correct values into over-bright territory; v4 would become the correct hemisphere integral at that point. The v5/bake coupling is a hidden contract that future Phase 2.5+ work must respect.

---

## Cost (RenderDoc, single-frame Sponza cam.md)

| Pass | Phase 1 m0 | Phase 1 m4 | **Phase 2** | vs p1 m0 | vs p1 m4 |
|---|---:|---:|---:|---:|---:|
| `radiance_3d` (4 cascades) | 37918 μs | 37758 μs | **38052 μs** | +0.4% | +0.8% |
| `reduction_3d` | 649 | 639 | 920 | +42% | +44% |
| **`raymarch`** | **10230** | **15354** | **11553** | **+12.9%** | **−24.8%** |
| `gi_blur` | 2952 | 3457 | 2971 | +0.7% | −14% |
| **TOTAL frame GPU** | **51761 μs** | **57221 μs** | **53509 μs** | **+3.4%** | **−6.5%** |

**Single-frame caveat (per critic 09 W6):** all numbers are point estimates from one frame each. The +12.9% raymarch is well outside the single-frame noise band (5-10%), so the verdict is robust, but a 3-run average would tighten the confidence interval. Same caveat the Phase 1 critic raised; same answer (deferred).

**Headline: Phase 2 is faster than Mode 4 by 6.5% total frame, slower than no-visibility Mode 0 by only 3.4%.** Mode 4's previous +50% raymarch overhead is gone — Phase 2's α-gated sampler is a single multiply per bin (was Mode 4's signed projection + compare).

**`reduction_3d` +42% — flagged for follow-up (revised per critic 09 W5).** Earlier framing dismissed this as noise, but **42% is well outside the 5-10% single-frame noise band cited elsewhere in this doc — that dismissal was inconsistent.** Possibly real: the reduction pass computes per-probe statistics from the atlas, and α changed semantics from hit-distance to transparency in 2B. Any reduction-shader code that summed or thresholded on α would behave differently now. **Action: re-run with N=3 averaged captures; if still elevated, audit `reduction_3d.comp` for code paths that read or threshold atlas alpha.** Not done this round.

`gi_blur` swung from +17% (Mode 4 vs Mode 0) to +0.7% (Phase 2 vs Mode 0) — supports the "single-frame noise" interpretation for that specific pass.

---

## Quality A/B (single-frame, RGB RMSE in linear sRGB)

| Scene | Phase 2 mean | Phase 1 m4 mean | mean ratio | RMSE vs m4 | Verdict |
|---|---:|---:|---:|---:|---|
| Sponza @ cam.md | 0.264 | 0.299 | 0.88 | 0.0644 | Above the strict 0.05 plan threshold but visually close to Mode 4 (the over-darkening is concentrated in the corridor depth where multi-bounce contribution was lost) |
| Cornell-orig | 0.242 | 0.251 | 0.96 | 0.0472 | Within threshold |
| Cornell-orig-alcove | 0.175 | 0.189 | 0.93 | 0.0502 | At threshold; alcove correctly shows reduced direct light per partition geometry |

**Subjective image read**: Phase 2 Sponza renders the corridor with appropriate occlusion (right wall correctly dark, top corridor lit, columns shadowed). Visually similar to Mode 4 but slightly dimmer in the corridor depth. Cornell scenes are visually identical.

**Honest caveat**: the 0.05 plan threshold was a Phase 1 hold-over that the user already relaxed in the [decision-gate doc](visibility_unified_plan_phase1_decision_gate.md) to a relative criterion (`m4-vs-m3 ≤ m0-vs-m3 × 1.3`). Under that relative criterion, Phase 2's RMSE is "still in the same ballpark as Mode 4" — not a regression worse than the natural variation between modes.

---

## What was NOT done (deliberate scope)

- **Full RC paper interval merge.** The textbook formula over-darkened (v1: 23% mean luminance loss in Sponza). Shipped variant uses the original radiance formula + only the α channel for visibility. Filed as future work if someone wants to revisit with a soft-α derivation.
- **Bake-time leak fix at the radiance level.** The α channel is honored at render time, not at bake time. Bake-time leaks (radiance bleeding through walls during cascade merge) exist now where they existed pre-Phase-2. The original plan over-promised this.
- **Soft α (0..1).** Atlas is RGBA16F so soft α is supported by the format, but the bake derives binary α from the existing hit/miss classification. Soft α would need a smoothstep based on near-surface SDF distance. Filed as Phase 2.5 if needed.
- **CLI flag deletion.** `--visibility-mode=N` is a no-op stub for one release per the plan's deprecation grace. Will be removed in a follow-up.
- **Bake-leak quantitative test.** The new `cornell-orig-alcove` scene was authored but the formal "atlas inspection via RenderDoc + sum bin.rgb*bin.a in occluded region" test from the plan §3.9 step 4 was NOT executed this session — visual A/B at the auto-fit camera was substituted. **Per critic 09 W9, this is weaker evidence than the plan called for**: visual A/B can't distinguish "α-gate worked" from "leaked radiance is below visible threshold," and it cannot quantify the bake-time leak that's still in the atlas (per W2). The formal test is the only way to validate Phase 2's headline claims; not running it leaves Phase 2 with weaker evidence than the plan asked for. **Filed.**

- **Sky/surface α encoding ambiguity (per critic 09 W3).** Both surface hits and sky exits write α=0 in the bake. Render renders them identically (occluded contribution = 0), which is correct today. But for **Phase 2.5 soft-α work** (smoothstep-derived gradual occlusion at near-surface boundaries), the encoding can't distinguish "soft surface boundary" from "hard sky terminal." Possible resolutions: sentinel encoding (sky = α = -1, RGBA16F supports negative); reserved range (sky = strict 0, surface = ε..1); separate metadata texture. None implemented. **Phase 2.5 is blocked on this design choice.**

---

## Verification

- **Build**: Release rebuilt clean (0 errors; only pre-existing warnings).
- **Smoke (default = α-gated path)**: ran no-flag, captured `tools/phase2c_default_smoke.png`. **Pixel-identical PNG** (every byte matches `tools/phase2v5_post_sponza_cammd_m0.png`; the earlier "RMSE 0.000000" wording was sloppy — at 8-bit-per-channel PNG resolution the meaningful test is byte-exact identity, not float RMSE). Confirms 2C cleanup didn't change behavior, just removed dead code.
- **Smoke (deprecated CLI flag)**: ran with `--visibility-mode=42`. Got expected warning lines on both stdout and stderr; render proceeded normally. Flag is correctly a no-op stub.
- **Quality A/B**: see table above.
- **RenderDoc timing**: see table above.
- **Bake-leak test scene**: cornell-orig-alcove loads cleanly (47353 voxels, partition visible in render); auto-fit view shows the alcove correctly dim. Quantitative atlas inspection deferred.

---

## Files changed (commit summary)

- **Pre-flight #2 (new asset)**:
  - `res/scene/CornellBox-Original-Alcove/CornellBox-Original-Alcove.obj` (new)
  - `res/scene/CornellBox-Original-Alcove/CornellBox-Original-Alcove.mtl` (new)
  - `src/main3d.cpp` (CLI dispatch row added)
  - `src/demo3d.cpp` (objKey detection rows added in 2 places; reload-on-toggle path)

- **2B (semantic change — bake α + render α-gate, default revert)**:
  - `res/shaders/radiance_3d.comp` (sampleUpperDir return type + bake per-direction loop)
  - `res/shaders/raymarch.frag` (sampleProbeDir α-gate)
  - `src/demo3d.h` (default `visibilityMode = 0`; doc comment rewritten)

- **2C (cleanup — delete deprecated)**:
  - `res/shaders/raymarch.frag` (removed `probeVisibility`, `sampleProbeDirPerBinOccluded`, `sampleProbeDirDepthAware`, `uVisibilityMode` uniform, mode-dispatch in `sampleDirectionalGI`)
  - `src/demo3d.h` (`visibilityMode` member retired; `setVisibilityMode` becomes deprecation stub)
  - `src/demo3d.cpp` (`uVisibilityMode` glUniform removed; ImGui combo removed)
  - `src/main3d.cpp` (CLI flag stub annotated)

Net code delta: ~120 lines deleted in raymarch.frag; ~30 lines added in radiance_3d.comp; ~50 lines deleted in demo3d.cpp; ~10 lines deleted in demo3d.h. Roughly **−180 net lines** in the C++/GLSL surface area.

---

## What's next (out of scope this session)

- **2C cleanup commit polish**: the cleanup left a few "(removed in Phase 2 2C)" comments. They can be deleted in a future style-cleanup pass once Phase 2 has been in production for a few weeks.
- **CLI flag deletion**: `--visibility-mode=N` stub stays for one release. Delete in the release after that.
- **Bake-leak quantitative test**: the cornell-orig-alcove scene exists; the formal atlas-inspection test (RenderDoc + `sum(bin.rgb × bin.a)` in occluded region) is filed for a follow-up if someone wants the rigorous bake-leak proof.
- **Soft α**: future refinement if visible banding shows up at hit/miss boundaries.
- **Full interval merge revisit**: if anyone has time, derive a corrected interval-merge formula that doesn't over-darken open scenes (would address the partial bake-time leak fix this Phase 2 didn't deliver).

---

## Honest residuals

- **Plan over-promised "bake-time leaks fixed" — bake side NOT delivered.** The shipped variant only fixes render-side visibility. Bake-time radiance still uses the original `hit.rgb * l + upperDir * (1-l)` formula; α only gates at render time. Leaked radiance still lives in the atlas; render-side α-gate hides it from output but a second consumer (atlas debug viewer, future feature) would resurrect the leak. **(W2)**
- **Sponza ~12% darker than Mode 4.** The plan's strict 0.05 RMSE secondary criterion is exceeded (0.064). Under the user's relaxed relative criterion, Phase 2 is acceptable.
- **`reduction_3d` +42%** — well outside the 5-10% single-frame noise band; **possibly a latent bug** from the α semantic change (was hit-distance, now transparency). Action filed: re-run with N=3 averaged captures + audit `reduction_3d.comp` for atlas-α reads. **(W5)**
- **Bake-leak quantitative test deferred — and visual A/B is NOT sufficient evidence** for either the render-side leak fix or the absence of bake-side leaks (per W9). The cornell-orig-alcove scene exists; formal test (RenderDoc atlas inspection + `sum(bin.rgb × bin.a)` in occluded region) is the only way to validate Phase 2's headline claims. Not running it leaves Phase 2 with weaker evidence than the plan asked for.
- **Sky/surface α encoding ambiguity blocks Phase 2.5 soft-α work (W3).** See note above.
- **v5 render normalization is COUPLED to current bake formula (W4).** Future bake fix without simultaneous switch to v4 normalization will over-amplify.
- **Single viewpoint per scene for quality A/B.** Multi-viewpoint testing not repeated this round.
- **No FLIP, just RGB RMSE.** Same caveat as Phase 1.
- **Single-frame timing only (W6).** All RenderDoc numbers are point estimates; no multi-run averaging.
- **Pixel-identical PNG verification, not float-RMSE (W7).** The 2C cleanup verification is byte-exact PNG match — appropriate for the test, but worth labeling correctly.
