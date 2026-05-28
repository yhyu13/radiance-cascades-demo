# ShaderToy Adoption Audit — v3 Status and Critical Gaps

**Date:** 2026-05-28  
**Scope:** Full audit of current code (radiance_3d.comp, demo3d.cpp/h), v3 scope/documentation, tools/scripts vs ShaderToy reference  
**Conclusion:** The M1 delta work has been partially IMPLEMENTED in shader code without the mandatory gated A/B evaluation. The tools are working but Sponza metrics have a validity concern.

---

## 1. Executive Summary

### The Question: "Are we getting closer to ShaderToy shaders at all?"

**Short answer:** The code has the mechanisms, but they've never been measured. The flags exist, they compile, they do something — but nobody has run the A/B against M0 baselines to prove they help.

### Top 5 Findings

| # | Severity | Finding |
|---|----------|---------|
| **F1** | **HIGH** | M1 Delta #3 (per-corner gated trilinear) and Delta #6 (geometric cone) are fully implemented in shader/C++ as flag-gated toggles, but **no A/B evaluation has been performed against the locked M0 baselines**. The v3 scope doc mandates STRONG/MARGINAL/DEAD gating per delta before landing — this process was skipped. |
| **F2** | **HIGH** | The M1 flag implementation pre-dates M0 Stage 1 baseline completion, meaning the flags were coded BEFORE there was a lock.json to measure against. This inverts the v3 discipline: "measure first, code second." |
| **F3** | **HIGH** | Delta #6's cone is hardcoded to `sin(3π/8) ≈ 0.924` (cascade 1's ShaderToy value) for ALL cascade levels. ShaderToy varies per cascade: sin(3π/8) for C0→C1, sin(7π/16)≈0.981 for C1→C2, sin(15π/32)≈0.995 for C2→C3. This is qualitatively correct per-cascade-1 but quantitatively wrong for higher cascades. |
| **F4** | **MEDIUM** | Sponza cascade-OFF baseline is **4.7× too bright** vs PT indirect at N=2048 (ratio=4.7148, bright%=100%, valid=693 pixels). The M1 deltas (#3 per-corner gating, #6 wider cone) are merge-noise tweaks that cannot close a 5× magnitude gap. The Sponza cascade pipeline is fundamentally broken for this geometry. |
| **F5** | **MEDIUM** | The Sponza valid pixel count is 693 out of likely ~900k pixels (0.08%). The PT indirect mean is 0.059 — tiny, meaning the valid mask only covers regions where PT actually registers indirect bounces. The metric is not representative of whole-scene quality. |

---

## 2. Code vs Documentation Audit

### 2.1. What's in the v3 scope doc (the PLAN)

[v3_shadertoy_adoption_scope.md](../doc/7/v3_shadertoy_adoption_scope.md) §3 defines:

- **M0 Stage 0**: Pre-work deliverables (A=v20 diff doc, B=delta5 ceiling, C=delta7 audit, C+=delta3 audit) → **SHIPPED**
- **M0 Stage 1**: Baseline captures → **SHIPPED** (Cornell + Sponza, lock.json on disk)
- **M1**: Three executable deltas with per-delta STRONG/MARGINAL/DEAD gating:
  - #3 (redefined): Per-corner gated 8-corner trilinear merge → **NOT STARTED per docs**
  - #6: WeightedSample cone width refactor → **NOT STARTED per docs**
  - #4: Multi-bounce deterministic-N vs stochastic-1 formulation comparison → **NOT STARTED per docs**

### 2.2. What's actually in the code (the REALITY)

The shader code (`radiance_3d.comp`) and C++ (`demo3d.cpp/h`) contain:

```cpp
// demo3d.h:1429-1430 — member variables
bool m1Delta3GatedTrilinear = false;
bool m1Delta6GeometricCone = false;

// demo3d.cpp:2475-2476 — shader uniforms
glUniform1i(glGetUniformLocation(prog, "uM1Delta3GatedTrilinear"), m1Delta3GatedTrilinear ? 1 : 0);
glUniform1i(glGetUniformLocation(prog, "uM1Delta6GeometricCone"), m1Delta6GeometricCone ? 1 : 0);
```

```glsl
// radiance_3d.comp:673-675 — Delta #3: switches between ws.rgb and trilinear.rgb
upperDir = (uM1Delta3GatedTrilinear != 0)
    ? ws                              // full WeightedSample (per-corner renormalized .rgb)
    : vec4(upperDirTrilinear.rgb,     // trilinear spatial average + WS visibility fraction
           ws.a);

// radiance_3d.comp:768 — Delta #3: when ON, aFactor=1.0 (gating already in .rgb)
float aFactor = (uUseWeightedSample != 0 && uM1Delta3GatedTrilinear == 0)
    ? upperDir.a : 1.0;
```

```cpp
// demo3d.cpp:2480-2485 — Delta #6: hardcoded cascade-1 cone angle
if (m1Delta6GeometricCone) {
    constexpr float kPi = 3.14159265358979323846f;
    sinT = std::sin(0.75f * 0.5f * kPi);  // = sin(3π/8) ≈ 0.924
}
```

**Conclusion:** The DELTA #3 and DELTA #6 mechanisms are fully coded, wired to GUI, and default-OFF. They compile. They function when toggled. But they have NEVER been measured against the M0 baseline_lock.json. No per-delta impl doc exists. No A/B captures exist. No STRONG/MARGINAL/DEAD verdict has been rendered.

### 2.3. Delta #3 Implementation Deep-Dive

**ShaderToy canonical behavior** (ref: `shader_toy/CubeA.glsl:196-219`):
```glsl
vec4 S0 = WeightedSample(...);  // vec4(rgb, 1.0) on visible, vec4(0.0) on reject
vec4 S1 = WeightedSample(...);
vec4 S2 = WeightedSample(...);
vec4 S3 = WeightedSample(...);
vec3 lastOutput = mix(mix(S0.xyz, S1.xyz, fx), mix(S2.xyz, S3.xyz, fx), fy)
                  / max(0.01, mix(mix(S0.w, S1.w, fx), mix(S2.w, S3.w, fx), fy));
```

Each corner independently gates BOTH `.rgb` (numerator) AND `.w` (denominator). Rejected corners contribute 0 to both. The weighted bilinear denominator renormalizes over surviving corners.

**Current v3 implementation** (ref: `radiance_3d.comp:668-678`):

When `uM1Delta3GatedTrilinear == 1`:
```glsl
upperDir = ws;   // ws = sampleUpperDirWeighted(...) = vec4(sumRgb/max(0.01,wVisible), wVisible/max(0.01,wTotalSpatial))
aFactor = 1.0;   // no scalar attenuation needed
```

This IS correct — `sampleUpperDirWeighted` already does per-corner gating (visible corners contribute `.rgb * wSpatial` to `sumRgb`, rejected corners don't add to `sumRgb` OR `wVisible`). The `.rgb` is `sumRgb / max(0.01, wVisible)` and `.a` is `wVisible / max(0.01, wTotalSpatial)`.

But the 8-corner trilinear weights (`wSpatial = wx * wy * wz`) are used inside `sampleUpperDirWeighted` rather than at the merge call site. This bundles the normalization inside the function rather than exposing per-corner data for the merge formula.

**Is this correct?** For the volumetric 8-corner analog of ShaderToy's 4-corner bilinear, yes — it gates each corner's contribution to both numerator and denominator. But the question isn't "is it coded correctly" — it's "does it help?" The v3 scope mandates a STRONG verdict (|p95| drops ≥ 30%, bright% drops ≥ 3pp on BOTH cornell and Sponza) before this can be landed. That measurement has never been done.

**Side note:** When `uM1Delta3GatedTrilinear == 0` (current DEFAULT), the code uses `vec4(upperDirTrilinear.rgb, ws.a)` — trilinear's unweighted spatial average for `.rgb`, WeightedSample's visibility fraction for `.a`. This is the Phase 3 v3 behavior that was shipped in v2.0-postfix.

### 2.4. Delta #6 Implementation Deep-Dive

**ShaderToy canonical behavior** (`shader_toy/CubeA.glsl:22-23`):
```glsl
float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*3.141592653*0.5;
```

Where `lProbeSize` is the SIZE of the upper-cascade probe (passed as `probeSize*2.0` from the merge code).

For cascade 0→1: lProbeSize=4, theta=3π/8, sin(3π/8)≈0.924  
For cascade 1→2: lProbeSize=8, theta=7π/16, sin(7π/16)≈0.981  
For cascade 2→3: lProbeSize=16, theta=15π/32, sin(15π/32)≈0.995

**Current v3 implementation** (`demo3d.cpp:2480-2485`):
```cpp
if (m1Delta6GeometricCone) {
    constexpr float kPi = 3.14159265358979323846f;
    sinT = std::sin(0.75f * 0.5f * kPi);  // sin(3π/8) ≈ 0.924 — cascade 1 ONLY
}
```

This sets `sinT = 0.924` for ALL cascades (C0→C1, C1→C2, C2→C3). It's only correct for C0→C1. For C1→C2, ShaderToy uses ~0.981; for C2→C3, ~0.995. The current hardcoded value is ~5.8% too tight for C1→C2 and ~7.1% too tight for C2→C3.

**However:** The cone width determines "how permissive is the visibility test." A too-NARROW cone rejects MORE corners — and the current default cone is ALREADY tighter (0.248) than ShaderToy's (~0.924-0.995). So even the hardcoded 0.924 is ~3.7× wider than default. The missing per-cascade variation is a nuance compared to the fundamental 3.7× widening.

**Note:** The shader uniform `uUpperBinConeSin` is set ONCE before dispatch and applies to ALL invocations in that dispatch. Since the dispatch processes a single cascade level, the cone IS per-cascade in practice (each dispatch level gets its own uniform value). But since `m1Delta6GeometricCone` doesn't vary per cascade level in the C++ code, all dispatches get the same 0.924.

---

## 3. Tools & Scripts Audit

### 3.1. `build.ps1` — PASS

Straightforward: CMake configure + build. No errors. Run from `3d/` directory.

### 3.2. `tools/v3_baseline/sponza_capture.ps1` — PASS (structure)

Parameterized capture harness with `-FrameList`, `-UseHybrid`, `-DryRun`. Correctly avoids forcing DM/ST/WS flags (verified in stage1 impl doc SC4). Inherits the leak-suppression-free template.

### 3.3. `tools/v3_baseline/build_baseline_lock.ps1` — PASS

Records file existence, SHA256, and metrics from JSON. Marks missing entries as "missing" not "fail." Verdict field controlled by explicit `-SponzaVerdict` param (not auto-derived). Conservative lock builder — correct design.

### 3.4. `tools/v3_baseline/analyze_baselines.py` — CONCERN

The Sponza metrics in `baseline_lock.json` show `valid: 693` pixels. For a likely 1280×720 Sponza render (~921k pixels), this is 0.075% coverage. The "valid mask" is computed as `max(pt_full - pt_direct, 0)` filtering — meaning only pixels where PT records indirect contribution above some threshold are counted.

**Impact:** A metric computed on 693 pixels is not representative of whole-scene quality. The cascade could be producing completely wrong GI on the other 99.9% of the image and these metrics wouldn't catch it. The v3 scope's |p95|≤0.50 retirement criterion evaluates against this valid mask — if the mask itself is fragile, the gate is fragile.

**Reproducer:** The valid count drops as N increases: N=128: 6665, N=256: 3236, N=512: 1560, N=1024: 891, N=2048: 693. As PT converges, the noise floor drops, and fewer pixels cross the threshold. This is expected behavior for a threshold-based mask but means the metric headroom shrinks with convergence.

### 3.5. Sponza Baseline Metrics — FATAL FOR PATH A

```
Sponza cascade-OFF at N=2048:
  ratio_self = 4.7148  — cascade is 4.7× brighter than PT indirect
  bright_pct = 100.0%  — all valid pixels are above the bright threshold
  dim_pct    = 0.0%    — no dim pixels

Cornell cascade-OFF at N=2048:
  ratio_self = 0.977   — cascade matches PT within 2.3%
  bright_pct = 5.4%
  dim_pct    = 28.6%
```

The cascade pipeline is producing ~5× excess GI on Sponza vs ~2% deficit on Cornell. The M1 deltas target ~10-30% reductions in bright% and |p95| — they cannot close a 5× gap.

**This means:** Even if Deltas #3 and #6 are STRONG on Cornell, they will be DEAD on Sponza (no measurable impact on a 5× gap). The M1 cumulative gate requires BOTH scenes to pass. Path A is structurally blocked by Sponza unless the baseline itself is a measurement artifact.

**Possible explanations for the 5× Sponza gap (requiring investigation):**
1. The valid mask (693 pixels) targets only lit-floor pixels where cascade over-brightens, missing the dark regions where cascade under-contributes. The aggregate ratio is dominated by a few bright pixels.
2. The PT reference might be improperly configured for Sponza geometry (different SDF resolution, camera placement, light strength normalization).
3. The cascade pipeline's volumetric probes are fundamentally broken for open-atrium geometry (Sponza) while working for enclosed-box geometry (Cornell).

---

## 4. Timeline Inconsistency

| Date | Event | Evidence |
|------|-------|----------|
| 2026-05-26 | v3 scope doc LOCKED (Path A→conditional Path B) | `v3_shadertoy_adoption_scope.md` §7 |
| 2026-05-26 | M0 Stage 0 deliverables SHIPPED | `v3_m0_stage0_impl.md` |
| 2026-05-26 | M0 Stage 0 closeout applied (P1-P8 patches) | `v3_m0_stage0_closeout_impl.md` |
| 2026-05-27 | M0 Stage 1 Sponza ladder SHIPPED | `v3_m0_stage1_sponza_ladder_impl.md` |
| **Unknown** | **M1 Delta #3 and #6 flag code written** | Present in `radiance_3d.comp`, `demo3d.h`, `demo3d.cpp` |
| **MISSING** | **M1 per-delta impl docs for #3, #6, #4** | No doc on disk matching `v3_m1_stage*_delta3*` under the ShaderToy-adoption numbering |

The fact that the M1 flag code EXISTS but no impl doc records its creation suggests the flags were either:
- (a) written BEFORE the v3 pivot scope was locked (leftover from pre-v3 experimentation)
- (b) written during M0 without proper documentation
- (c) cherry-picked from another branch

The member names (`m1Delta3GatedTrilinear`, `m1Delta6GeometricCone`) match the v3 ShaderToy-adoption delta numbering (#3, #6), suggesting they were added WITH the pivot in mind, not before. This means implementation happened without the gated process.

---

## 5. Recommendations

### Immediate Actions

1. **DO NOT toggle m1Delta3GatedTrilinear or m1Delta6GeometricCone in the GUI expecting ShaderToy parity.** These flags modify bake behavior but have not been validated. Toggling them + re-running the app produces renders whose metric impact is unknown.

2. **Run the M1 A/B for Deltas #3+#6 against M0 baselines:**
   ```powershell
   # Cornell:
   #   Config: m1Delta3GatedTrilinear=1, m1Delta6GeometricCone=0
   #   Compare PT metrics vs cornell_cam0_cascade_off in baseline_lock.json
   #   Config: m1Delta3GatedTrilinear=0, m1Delta6GeometricCone=1  
   #   Config: m1Delta3GatedTrilinear=1, m1Delta6GeometricCone=1
   ```
   This is the 2×2 matrix the v3 scope doc already describes. The code is already written — the measurement is what's missing.

3. **Audit Sponza baseline before treating it as a gate.** The 4.7× ratio with 693 valid pixels needs investigation:
   - Expand the valid mask (lower threshold, or per-region masks)
   - Compare Sponza cascade-OFF visual output vs PT at N=2048 — does it look 5× too bright?
   - Check if hybrid-ON Sponza (ratio=0.83, bright%=0%) is actually a better baseline

4. **Fix Delta #6 per-cascade cone variation.** Pass the cascade level or upperProbeSize to the cone computation:
   ```cpp
   // Per-cascade: sin(theta) = sin((probeSize/2 - 0.5)/(probeSize/2) * π/2)
   // where probeSize = 2^(cascadeLevel + 2) for the UPPER cascade
   float upperProbeSize = pow(2.0f, cascadeLevel + 2);
   float theta = (upperProbeSize*0.5f - 0.5f) / (upperProbeSize*0.5f) * kPi * 0.5f;
   sinT = std::sin(theta);
   ```

5. **Create the missing M1 per-delta impl docs.** Before any A/B code lands, each delta needs an impl doc front-loaded with:
   - The v20 diff entry it addresses
   - The volumetric analog definition (especially #3's 8-corner formulation)
   - The pre-committed A/B harness config
   - STRONG/MARGINAL/DEAD bands per scope §3

### Longer-term

6. **If Sponza cascade-OFF 4.7× gap is real (not a measurement artifact), Path A is dead before it starts.** M1 deltas cannot close a 5× gap. The options would be:
   - Path B (surface-attached topology) — might fix the open-atrium over-brightening
   - Accept hybrid-ON as permanent (ratio=0.83, already better than Path A's ceiling)
   - Re-derive the Sponza camera/light setup to get a saner baseline

7. **The current state is salvageable.** The M1 flags are implemented but unevaluated — NOT implemented-and-wrong. The code and the process just need to be aligned: run the A/B, measure the impact, apply the gates. If #3 is STRONG on Cornell, it ships. If #6 adds nothing or makes things worse, it gets dropped. This is exactly the workflow the v3 scope doc prescribes.

---

## 6. ShaderToy Reference Reference (for future porting)

The canonical ShaderToy reference in this repo:
- **`shader_toy/CubeA.glsl`** — Surface-attached probe bake + merge (cascaded irradiance baking into cubemap)
- **`shader_toy/Image.glsl`** — Identical to CubeA.glsl (duplicate for ShaderToy multipass)
- **`shader_toy/Common.glsl`** — SDF, ray tracing, BRDF, geometry, math
- **`shader_toy_pt/BufferA.glsl`** — PT reference renderer (MIS path tracer, NOT radiance cascades)
- **`shader_toy_pt/Common.glsl`** — Sampling utilities for PT reference
- **`shader_toy_pt/Image.glsl`** — PT display pass (sqrt tonemap)

**Key architectural differences from current impl:**

| Property | ShaderToy | Current |
|----------|-----------|---------|
| Probe topology | Surface-attached (gTan/gBit/gNor/gPos) | Volumetric 3D grid |
| Bake integral | Hemisphere above gNor | Full sphere |
| Bake storage | `L·cos(θ)·ΔΩ` (pre-integrated irradiance per bin) | Raw `L` per bin + binary α |
| Consumer | Cubemap fetch (no integration) | Hemispheric Riemann sum: `(4/D²)·Σ(L·cos⁺)` |
| Merge between cascades | Per-corner bilinear (4 corners, 2D spatial) | Per-corner trilinear (8 corners, 3D spatial) |
| Visibility gating | WeightedSample per-corner cone test | WeightedSample per-corner cone test (same concept) |
| Multi-bounce | Deterministic 4-tap spatial average from cubemap at hit surface | Stochastic 1-sample MC with cosine PDF |

---

## 7. File Inventory

Shipped deliverables under v3:
```
doc/7/v3_shadertoy_adoption_scope.md          (scope, gates, milestones)
doc/7/v20_shadertoy_diff_impl.md              (Deliverable A: per-delta diff)
doc/7/v25_z_mbrc_correction_failure_learnings.md  (v2.x program closeout)
doc/7/v3_m0_stage0_impl.md                    (Stage 0 pre-work summary)
doc/7/v3_m0_stage1_impl.md                    (Stage 1 partial — Cornell hybrid)
doc/7/v3_m0_stage1_sponza_ladder_impl.md      (Stage 1 Sponza ladder)
tools/v3_baseline/baseline_lock.json           (M0 lock)
tools/v3_baseline/sponza_default_metrics.json  (Sponza cascade metrics)
tools/v3_baseline/sponza_hybon_metrics.json   (Sponza hybrid metrics)
tools/v3_baseline/delta3_alpha_audit.md        (Deliverable C+)
tools/v3_baseline/delta5_ceiling_estimate.md   (Deliverable B)
tools/v3_baseline/delta7_offset_audit.md       (Deliverable C)
```

Code with M1 flags (unevaluated):
```
src/demo3d.h:1429-1430          (m1Delta3GatedTrilinear, m1Delta6GeometricCone members)
src/demo3d.h:654-676            (setter methods with cascadeRebuild triggers)
src/demo3d.cpp:2475-2485        (shader uniform setup + cone computation)
res/shaders/radiance_3d.comp:673-675  (Delta #3: gated trilinear vs WS.rgb)
res/shaders/radiance_3d.comp:768      (Delta #3: aFactor suppression)
res/shaders/radiance_3d.comp:300-350  (sampleUpperDirWeighted — per-corner gating)
```

MISSING (must exist before M1 lands):
```
doc/7/v3_m1_delta3_gated_trilinear_impl.md   (per-corner 8-corner gated trilinear)
doc/7/v3_m1_delta6_geometric_cone_impl.md    (ShaderToy-like cone refactor)
doc/7/v3_m1_delta4_mb_formulation_impl.md    (multi-bounce formulation comparison)
```

---

## 8. Cross-References

- v3 scope: `doc/7/v3_shadertoy_adoption_scope.md`
- v20 delta diff: `doc/7/v20_shadertoy_diff_impl.md`
- Failure learnings: `doc/7/v25_z_mbrc_correction_failure_learnings.md`
- ShaderToy reference: `shader_toy/CubeA.glsl`
- Current bake shader: `res/shaders/radiance_3d.comp`
- Current consumer: `res/shaders/raymarch.frag`
- Baselines: `tools/v3_baseline/baseline_lock.json`