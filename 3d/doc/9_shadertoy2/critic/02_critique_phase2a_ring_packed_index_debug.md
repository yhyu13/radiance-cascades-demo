# Critique 02 — Phase 2A Plan + Impl: Ring-Packed Atlas Index Debug

**Sources reviewed:**
- [doc/9_shadertoy2/phase2_plan_ring_packed_index_debug.md](../phase2_plan_ring_packed_index_debug.md) (plan)
- [doc/9_shadertoy2/phase2_impl_ring_packed_index_debug.md](../phase2_impl_ring_packed_index_debug.md) (impl)
- [doc/9_shadertoy2/critic/reply/01_reply_to_critique_surface_attached_refactor_plan.md](reply/01_reply_to_critique_surface_attached_refactor_plan.md) (reply to critique 01)
- [res/shaders/surface_ring_debug.comp](../../../res/shaders/surface_ring_debug.comp) (new shader)
- [src/surface_rc.cpp](../../../src/surface_rc.cpp) / [src/surface_rc.h](../../../src/surface_rc.h) (updated C++)
- [shader_toy/CubeA.glsl](../../../shader_toy/CubeA.glsl) (reference)

**Date:** 2026-05-29
**Scope:** Phase 2A only — index/coordinate debug, no radiance.

This is a non-blocking critique. The phase is small, well-scoped, and correctly defers tracing/NEE/feedback per critique 01's binding deferrals. Most findings are localized bugs and process gaps, not architectural mistakes.

---

## 0. Headline

| ID | Severity | Theme |
|---|---|---|
| H1 | **HIGH** | TBN axis swap vs ShaderToy on all 4 wall charts — silent ray-direction drift in Phase 2B |
| M1 | MED | Visual contract gates declared in plan §4 are not actually verified — phase reported done from compile+exit, not pixels |
| M2 | MED | Mode 3 `ringStripe` is dead code — `probeThetai` is always half-integer, never near integer |
| M3 | MED | Chart 6 (front wall) still unconditionally allocated — wastes ~12% of ring atlas for 5-wall Cornell |
| L1 | LOW | Ring overlay aspect ratio breaks at small viewports — `std::min` clamps axes independently |
| L2 | LOW | Reply checklist `[x]` ambiguity — "plan amended" conflated with "code added" |
| L3 | LOW | Mode 1 probe-coordinate normalization overshoots `[0,1]` on the wide-chart axis at upper cascades |
| L4 | LOW | Spatial probe count at C5 on 128-wide walls is only 2×4=8 — not flagged as a quality-floor risk |

**Things done well** (not in the issues table because they're correct):

1. Plan binds Critique-01-C1 (ring-packed layout) into code: `cascadeCount=6`, `bandHeight=256`, `1024×1536` matches CubeA.glsl line 127.
2. Phase 2A scope correctly excludes radiance/NEE/feedback per Critique-01-C2/C3 deferrals.
3. Phase 1 chart atlas preserved alongside ring atlas — prior verification not overwritten.
4. World-position fix in impl SC2 (`(probeCoord + 0.5) * probeSize / c.gRes`) correctly generalizes ShaderToy's hardcoded `/256.` ([CubeA.glsl:130-131](../../../shader_toy/CubeA.glsl#L130)).
5. Both `probeCoord` and `dirCoord` exposed as separate modes — catches the SC4 (plan) reversal concern.
6. CLI flags `--surface-debug-target` / `--surface-ring-debug-mode` enable headless screenshot sweep — addresses the L4 critique-01 diagnostics-pipeline concern.
7. Build green, smoke test exits clean, two textures allocated.

---

## 1. HIGH — TBN axis swap on all 4 wall charts vs ShaderToy

### Evidence

**ShaderToy [CubeA.glsl:82-106]** — left, right, back, front walls:
```glsl
// left wall
gTan = vec3(0., 1., 0.);   gBit = vec3(0., 0., 1.);   gNor = vec3(1., 0., 0.);   gRes = vec2(128., 256.);
// right wall
gTan = vec3(0., 1., 0.);   gBit = vec3(0., 0., 1.);   gNor = vec3(-1., 0., 0.);  gRes = vec2(128., 256.);
// back wall
gTan = vec3(0., 1., 0.);   gBit = vec3(1., 0., 0.);   gNor = vec3(0., 0., 1.);   gRes = vec2(128., 256.);
// front wall
gTan = vec3(0., 1., 0.);   gBit = vec3(1., 0., 0.);   gNor = vec3(0., 0., -1.);  gRes = vec2(128., 256.);
```
All 4 walls have `gTan = (0, 1, 0)` — the **vertical Y axis** is the chart's U direction.

**Impl [surface_ring_debug.comp:72-111]** — same 4 walls:
```glsl
// left wall   id=3
c.tangent = vec3(0.0, 0.0, 1.0);  c.bitangent = vec3(0.0, 1.0, 0.0);  c.normal = vec3(1.0, 0.0, 0.0);   c.gRes = vec2(128.0, 256.0);
// right wall  id=4
c.tangent = vec3(0.0, 0.0, 1.0);  c.bitangent = vec3(0.0, 1.0, 0.0);  c.normal = vec3(-1.0, 0.0, 0.0);  c.gRes = vec2(128.0, 256.0);
// back wall   id=5
c.tangent = vec3(1.0, 0.0, 0.0);  c.bitangent = vec3(0.0, 1.0, 0.0);  c.normal = vec3(0.0, 0.0, 1.0);   c.gRes = vec2(128.0, 256.0);
// front wall  id=6
c.tangent = vec3(1.0, 0.0, 0.0);  c.bitangent = vec3(0.0, 1.0, 0.0);  c.normal = vec3(0.0, 0.0, -1.0);  c.gRes = vec2(128.0, 256.0);
```
Impl uses tangent ∈ {(0,0,1), (1,0,0)} — the **horizontal axes** — with bitangent = Y. Tangent and bitangent are systematically swapped relative to ShaderToy for all 4 walls.

The two are *not* equivalent under "well, the chart UV grid is still orthogonal." `gRes = (128, 256)` in ShaderToy means the chart has 128 texels along its **U (tangent / vertical Y)** and 256 along its **V (bitangent / horizontal Z or X)**. In the impl, `gRes = (128, 256)` means 128 along **U (tangent / horizontal Z or X)** and 256 along **V (bitangent / vertical Y)**. So the chart is also visually transposed: a vertical strip in ShaderToy becomes a horizontal one in the impl, with all per-texel indexing flipped.

### Why this matters

Phase 2A doesn't yet construct ray directions, so the TBN swap is invisible in the current debug output. But the moment Phase 2B builds `probeDir = dx*tangent + dy*bitangent + dz*normal` (ShaderToy [CubeA.glsl:148](../../../shader_toy/CubeA.glsl#L148)), the ray for a given atlas texel will point in a **different 3D direction** than ShaderToy. The merge between cascades will still be self-consistent (because the swap is uniform within the impl), but:

1. Side-by-side comparison with the ShaderToy reference (i.e. "does this bin's ray hit the same surface point as ShaderToy's bin?") becomes impossible without first applying the transpose.
2. Any future port of ShaderToy code that assumes `gTan = Y` will silently sample the wrong axis.
3. The chart UV → world-position interpolation in mode 4 already encodes the impl's convention; if a later phase tries to "match ShaderToy" by copying the [CubeA.glsl:167-169](../../../shader_toy/CubeA.glsl#L167) `rayHit.uv*128.` lookup, the U/V coordinates will be flipped.

This is the precise "ShaderToy-looking but indexing differently" failure mode that Critique 01 C1 made binding to avoid. The reply doc states:

> "If we use a conventional per-probe directional tile atlas, the ShaderToy merge math no longer indexes the same semantic texels. That would reproduce the v3 mistake: porting formulas into the wrong topology."

The TBN swap is the same class of mistake on a different axis: ring-packed layout was honored, but the per-chart axis assignment within that layout drifted.

### Required action

Pick one:

1. **Restore ShaderToy TBN** in [surface_ring_debug.comp:72-111](../../../res/shaders/surface_ring_debug.comp#L72): all 4 wall charts get `tangent = (0,1,0)`, swap to ShaderToy's bitangent. Mode 4 world-position interpolation in [surface_ring_debug.comp:167-170](../../../res/shaders/surface_ring_debug.comp#L167) needs corresponding update so `probeUVChart.x` runs along Y and `probeUVChart.y` along Z (or X).
2. **Declare divergence** in the impl doc with an explicit "we transpose wall charts because horizontal-tangent is conventional for raster UVs; ShaderToy ports require a 90° rotation" note, and add a runtime assertion / debug-mode comparison that fires if Phase 2B ports ShaderToy logic without the transpose.

Either is fine; silently diverging is not.

---

## 2. MEDIUM — Visual contract gates not actually verified

The plan §4 declares four visual gates:

| Gate | Pass condition |
|---|---|
| Visual contract | ring-packed overlay shows six cascade bands |
| Probe contract | probe coordinate density halves per cascade |
| Direction contract | direction/ring pattern grows with `probeSize` |
| Safety | final mode-0 volumetric path unchanged |

The impl §6 reports:

> - shader compiles and loads
> - ring-packed atlas allocates
> - CLI debug target/mode setters work
> - Cornell OBJ remains recognized
> - app exits cleanly

**None of the four visual gates are verified by what was actually executed.** The smoke test proves the dispatch path runs; it does not produce a pixel that a human eye (or pixel-pattern check) could compare against the expected six bands / halving density / growing direction tile.

The impl SC1 acknowledges this explicitly:

> "Visual contract still not captured to disk. ... CLI now supports --surface-debug-target=ring and --surface-ring-debug-mode=N, so a screenshot sweep can be run next without UI interaction."

…and provides a 5-line screenshot sweep. But the sweep is **provided as a recommendation, not executed**, and the phase is reported "Implemented + self-critiqued" with a "Verification" section that conspicuously omits the gates declared in the plan.

This is the same "tuning blind" / "EXR or it didn't happen" pattern that locked memories ([[feedback_measurement_before_features]], [[project_mbrc_correction_failed_pivot_shadertoy]]) call out. Even for a phase that produces no radiance, the verification standard for a coordinate-debug phase is **pixel inspection of the coordinate debug**, not "the dispatch did not crash." A wrong index decode would compile fine and produce nonsense pixels that the smoke test would never notice.

### Required action

Before declaring Phase 2A done:

1. Run the 5-mode screenshot sweep in impl §7 SC1.
2. Add a §7.A "Visual gate evidence" subsection with one paragraph per mode confirming:
   - **Mode 0:** 6 horizontal bands visible, each with the 6-chart vertical pattern.
   - **Mode 1:** probe-coordinate density visibly halves moving from C0 band (top) to C5 (bottom). Concretely, at C0 the gradient has fine grain (128 steps), at C5 coarse grain (2–4 steps depending on chart).
   - **Mode 2:** direction-tile size visibly grows from 2×2 at C0 to 64×64 at C5.
   - **Mode 3:** concentric ring pattern visible inside each direction tile (see M2 below — current ringStripe is broken so this gate will fail visually until fixed).
   - **Mode 4:** smooth world-position gradient per chart, with each chart spanning the correct AABB extent.
3. Either inline the screenshots (small JPEGs) or commit them under `tools/phase2a_visual/`.

The plan was explicit that the visual gates are the acceptance criteria. The phase isn't done until they're checked.

---

## 3. MEDIUM — `ringStripe` in mode 3 is dead code

[surface_ring_debug.comp:155-158](../../../res/shaders/surface_ring_debug.comp#L155):
```glsl
} else if (uDebugMode == 3) {
    float ringNorm = probeThetai / max(probeSize * 0.5, 1.0);
    float ringStripe = fract(probeThetai + 0.05) < 0.1 ? 1.0 : 0.0;
    rgb = mix(vec3(ringNorm, 1.0 - ringNorm, float(cascade) / 5.0), vec3(1.0), ringStripe * 0.35);
}
```

`probeThetai = max(abs(probeRel.x), abs(probeRel.y))` where `probeRel = probeUV - probeSize*0.5` and `probeUV = dirCoord + 0.5`. Therefore `probeRel ∈ {±0.5, ±1.5, ±2.5, ...}` (half-integers), and `probeThetai ∈ {0.5, 1.5, 2.5, ...}` — **always exactly half-integer**.

`fract(probeThetai + 0.05) = fract({0.5, 1.5, ...} + 0.05) = fract({0.55, 1.55, ...}) = 0.55` for every texel. `0.55 < 0.1` is always false. `ringStripe ≡ 0`. The stripe overlay never fires.

The plan's mode-3 acceptance:
> mode 3: ring pattern visible inside each probe tile

…will visually show only the `ringNorm` red→green gradient, not the discrete ring boundaries the stripe was meant to highlight. Visually, this still passes "you can see a ring-like pattern," but the explicit ring-boundary markers are absent. A reviewer eyeballing it might call it pass or fail depending on expectation.

### Required action

Replace the stripe condition with one that fires at half-integer transitions:
```glsl
float ringStripe = fract(probeThetai + 0.5) < 0.15 ? 1.0 : 0.0;  // fires near integer boundaries
```
or, more cleanly, mark the boundaries between successive `floor(probeThetai)` rings:
```glsl
float ring = floor(probeThetai);              // 0, 1, 2, ... discrete ring index
float ringFrac = probeThetai - ring;          // always 0.5 — use ring instead
float stripe = step(0.99, fract(ring * 0.5)); // every other ring
```
Either way, validate visually after the fix.

---

## 4. MEDIUM — Chart 6 (front wall) unconditionally allocated

Carried over from Phase 1 SC3 but not progressed:

> "The chart table includes a front wall even if some Cornell variants are open
> ... front wall should be maskable per scene variant
> surface radiance should not treat non-existent front wall as a bounce source"

Phase 2A reserves 128×256 × 6 cascades = 196608 ring atlas texels (~12% of the 1024×1536 atlas) for a chart that may not exist in the loaded geometry. For Phase 2A debug-only this is purely cosmetic; for Phase 2B with persistent ping-pong feedback, sampling a non-existent chart will either produce zeros (best case) or feed garbage radiance back into the merge (worst case).

The reply doc accepts L1 of critique 01 ("Cornell chart dimensions bound to actual scene") but the implementation still hardcodes 6 charts including one that may be invalid. The standard 5-wall Cornell has the open side at -Z, not +Z; without inspecting the actual cornell.obj geometry, the impl can't know which side (if any) is open.

### Required action (before Phase 2B)

1. Decide if Cornell variant being targeted is 5-wall (open) or 6-wall (closed box). If 5-wall, identify the open axis.
2. Either:
   - Compress the chart table to 5 entries when the loaded variant is open, recovering the atlas region, or
   - Add a `chartActive[6]` mask uniform that Phase 2B's radiance shader reads to skip dead charts.
3. Add a runtime check that the loaded geometry actually has a wall at each declared chart plane (e.g., raycast from chart center in chart normal direction and confirm a hit at expected distance).

For Phase 2A specifically, no action needed — flag for Phase 2B planning.

---

## 5. LOW — Ring overlay aspect ratio breaks at small viewports

[surface_rc.cpp:239-241](../../../src/surface_rc.cpp#L239):
```cpp
const int debugW = (debugTarget == 1) ? std::min(384, viewport[2]) : std::min(512, viewport[2]);
const int debugH = (debugTarget == 1) ? std::min(576, viewport[3]) : std::min(256, viewport[3]);
glViewport(0, 0, debugW, debugH);
```

At the smoke-test viewport (320×240), the ring overlay becomes `min(384, 320) × min(576, 240) = 320 × 240`, aspect 1.33. The atlas aspect is `1024/1536 = 0.667`. So the overlay is stretched 2× horizontally. This is visible-correct (you still see the bands) but inflates the visual size of artifacts and may mask narrow ring patterns.

At larger viewports (≥384×576) it works correctly. The smoke-test command in the impl §6 uses 320×240, so the overlay produced by that command is stretched.

### Suggested fix

Constrain to atlas aspect:
```cpp
if (debugTarget == 1) {
    const float aspect = 1024.0f / 1536.0f;
    int targetH = std::min(576, viewport[3]);
    int targetW = std::min(384, std::min(viewport[2], int(targetH * aspect)));
    targetH = int(targetW / aspect);
    glViewport(0, 0, targetW, targetH);
}
```

Not blocking; the screenshot sweep at a larger viewport will work.

---

## 6. LOW — Reply checklist `[x]` ambiguity

[critic/reply/01_reply_to_critique_surface_attached_refactor_plan.md:333-344](reply/01_reply_to_critique_surface_attached_refactor_plan.md#L333):
```
- [x] Commit to ShaderToy ring-packed cascade-band layout.
- [x] Reject separate `dirRes` outer-product atlas for first wave.
- [x] Declare persistent ping-pong atlas semantics.
- [x] Schedule recursive self-feedback before quality metrics.
- [x] Add point-light NEE at probe-ray hit points.
```

These `[x]` items mean "the plan now reflects this," not "the code now does this." `Declare persistent ping-pong atlas semantics` is checked but no code declares them; `Add point-light NEE` is checked but no code computes NEE. A future reader could be misled into thinking the work is done when only the plan amendment is.

### Suggested fix

Either split the checklist into "Plan amendments locked" (`[x]`) and "Code obligations" (`[ ]`), or annotate inline: `- [x] (plan) Commit to ShaderToy ring-packed cascade-band layout. → (code Phase 2A) Done. → (code Phase 2B) Pending.`

---

## 7. LOW — Mode 1 probe-coordinate normalization overshoot

[surface_ring_debug.comp:152](../../../res/shaders/surface_ring_debug.comp#L152):
```glsl
rgb = vec3(probeCoord / max(probePositions - vec2(1.0), vec2(1.0)), float(cascade) / 5.0);
```

At C5 (probeSize=64) on a 256-wide chart (floor/ceiling), `probePositions.x = 256/64 = 4`, `max(4-1, 1) = 3`. `probeCoord.x ∈ [0, 4)`, so `probeCoord.x / 3 ∈ [0, 1.33)`. Red channel clamps at 1.0 → some texels show pure red instead of the gradient.

Numerically minor; the gradient is still readable. But if the visual gate "probe coordinate density halves per cascade" is checked by counting visible gradient steps, the clipping confounds the count at upper cascades on wide charts.

### Suggested fix

Use `probePositions` instead of `probePositions - 1`:
```glsl
rgb = vec3(probeCoord / max(probePositions, vec2(1.0)), float(cascade) / 5.0);
```
This gives `[0, 1)` strictly. Or accept the overshoot and document it in mode-1 caption.

---

## 8. LOW — Spatial probe count floor at C5 on 128-wide walls

At C5 (probeSize=64) on a 128-wide wall chart, `probePositions = (128/64, 256/64) = (2, 4)` — only **8 spatial probes** cover the entire wall. At C4 (probeSize=32), `(4, 8) = 32 probes`. At C0 (probeSize=2), `(64, 128) = 8192 probes`.

ShaderToy works fine with this because (a) the sky/sun lighting is broad and 8 probes can approximate the upper-hemisphere far-field, and (b) the merge weights from C5 down to C0 reconstruct local detail. For Cornell point-light, the upper cascades have to capture the radiance the point light contributes to the wall — which is highly non-uniform (corner shadows, distance falloff). 8 probes per wall at C5 may be too few to resolve the floor's bright-patch contribution as seen from the wall.

Neither the plan nor impl flag this as a quality risk. It might not be one — Phase 2B's per-frame ping-pong propagates the bright patch through many frames — but it's worth measuring.

### Suggested action (Phase 2B planning, not Phase 2A blocking)

Add a metric to the Phase 3 measurement protocol: per-cascade RMS error vs PT reference, to identify which cascade level contributes most error. If C5 dominates, the answer is either more cascades (but ShaderToy says 6 is enough) or larger atlas resolution.

---

## 9. Process Observations

### P1 — `Build green + clean exit` is being treated as verification

Phase 1 impl had the same pattern: "shaders compile, atlas allocates, CLI works, app exits." Phase 2A repeats it. Critique 01 §10/M4 specifically called out that the v2.x failure was tuning without measurement; the parallel failure mode for a debug-only phase is *implementing without inspection*. The smoke test is the necessary precondition for visual inspection, not a substitute for it.

For Phase 2B onward (which adds tracing, NEE, feedback, then EXR-able output), "verification = build + exit" will become acutely insufficient very quickly. Recommend establishing now: every phase's "Verification" section must show at least one pixel-level artifact (screenshot, EXR delta, or readback summary) that matches the gates declared in the plan.

### P2 — Stop-loss honored implicitly

Critique 01 L2 requested a stop-loss policy. Reply doc adopted it ("2 implementation attempts + 1 diagnostic-only round per phase"). Phase 2A appears to have landed in 1 attempt — well within budget. Good. But the stop-loss currently only exists in the reply doc and isn't referenced in the per-phase plan. When a phase struggles, will future-author remember to check the stop-loss? Suggest copying the policy into the phase plan's acceptance-gate section so it's adjacent to the gate.

### P3 — Critique → reply → implement loop is working

The full loop now exists:
- `01_critique_surface_attached_refactor_plan.md` (15 findings)
- `reply/01_reply_to_critique_surface_attached_refactor_plan.md` (15 dispositions)
- `phase2_plan_ring_packed_index_debug.md` (binds C1 specifically)
- `phase2_impl_ring_packed_index_debug.md` (executes the binding)

This is a healthy pattern. Recommend continuing it: this critique should get its own reply doc before Phase 2B starts, and the reply should explicitly state which of H1/M1/M2/M3 are fixed before Phase 2B vs deferred.

---

## 10. Pre-Phase-2B Required Actions

Before Phase 2B implementation begins, in order:

| Action | Where | Why |
|---|---|---|
| Resolve H1: restore ShaderToy TBN or declare transpose | [surface_ring_debug.comp:72-111](../../../res/shaders/surface_ring_debug.comp#L72) | Phase 2B will build ray directions from these axes; silent drift now becomes silent radiance drift later |
| Resolve M1: run screenshot sweep, confirm gates | impl §7 + tools/phase2a_visual/ | The phase isn't done until the visual gates pass |
| Resolve M2: fix mode-3 ringStripe so ring boundaries are visible | [surface_ring_debug.comp:157](../../../res/shaders/surface_ring_debug.comp#L157) | Plan §4 gate explicitly requires "direction/ring pattern" — currently only half-shown |
| Resolve M3: decide chart count for actual Cornell variant | [surface_ring_debug.comp:102-112](../../../res/shaders/surface_ring_debug.comp#L102) + scene inspection | Phase 2B persistent feedback will fail unpredictably on a chart that doesn't exist |
| Reply doc for this critique | doc/9_shadertoy2/critic/reply/02_*.md | Continue the critique→reply→implement discipline |

Low-severity items (L1-L4) and process observations (P1-P3) can be folded into Phase 2B's planning or deferred without blocking.

---

## 11. What This Critique Does Not Say

To preempt scope creep:

- **Not asking for radiance** — Phase 2A correctly excludes it.
- **Not asking for NEE / persistent feedback** — also correctly excluded.
- **Not asking for cross-chart merge** — out of scope per reply M3.
- **Not asking for Sponza support** — explicitly deferred.
- **Not flagging the per-frame debug dispatch** — Phase 1 SC4 already acknowledged it; cheap enough.
- **Not relitigating Critique 01** — the reply accepted everything; this critique only checks faithful execution of what was accepted.

---

## 12. Cross-References

- [doc/9_shadertoy2/critic/01_critique_surface_attached_refactor_plan.md](01_critique_surface_attached_refactor_plan.md)
- [doc/9_shadertoy2/critic/reply/01_reply_to_critique_surface_attached_refactor_plan.md](reply/01_reply_to_critique_surface_attached_refactor_plan.md)
- [doc/9_shadertoy2/surface_attached_shadertoy_refactor_plan.md](../surface_attached_shadertoy_refactor_plan.md)
- [doc/9_shadertoy2/phase1_impl_surface_rc_debug_atlas.md](../phase1_impl_surface_rc_debug_atlas.md)
- [shader_toy/CubeA.glsl:82-106](../../../shader_toy/CubeA.glsl#L82) — ShaderToy wall TBN reference for H1
- [shader_toy/CubeA.glsl:127-148](../../../shader_toy/CubeA.glsl#L127) — ring-packed coordinate reference
- [shader_toy/CubeA.glsl:130-131](../../../shader_toy/CubeA.glsl#L130) — probe-position scaling reference (the SC2 fix matches this)
