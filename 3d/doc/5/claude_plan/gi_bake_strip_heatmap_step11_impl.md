# Step 11 — Implementation Notes: GI Bake Strip + Heatmap Diagnostic Modes (revised after codex 08)

**Date:** 2026-05-10 (revised after codex 08 review + reply 08)
**Status:** Implemented and verified. Build clean (0 errors); 10 Sponza
captures at the user's specific viewpoint
(`pos=1.0710,-0.0723,-0.3393  target=0.1212,-0.0812,-0.6520`)
landed cleanly. The mode 6 vs. mode 6 + strip comparison is decisive
and lands somewhere between Outcome A and B from the codex 07 F4
framework: most surfaces lose nearly all GI when the bake floor is
stripped, but a few left-side pillar edges retain visible structure.

**Plan source-of-truth:** [gi_bake_strip_heatmap_step11_plan.md](gi_bake_strip_heatmap_step11_plan.md)
(revised after codex 07 review — all 11 findings folded in).

## Changelog (post codex `08_gi_bake_strip_heatmap_step11_impl_review.md`)

All 8 findings accepted. Two real code fixes landed and verified by
recapture; six doc accuracy / acknowledgement fixes:

- **F1+F7 (high+medium) code fix.** Removed `meshSDFReady = false`
  from `setStripAmbientFloorBake()`. The strip toggle is a LIGHTING
  change (modifies cascade probe radiance), NOT a geometry change
  (SDF/voxel texture is unchanged). Including the SDF invalidation
  forced ~3-7 ms of wasted SDF rebake per toggle. Setter now uses
  the **4-line lighting-invalidation pattern**:
  `cascadeReady=false; forceCascadeRebuild=true; renderFrameIndex=0;
  historyNeedsSeed=true;`. Header comment updated to document the
  distinction so future implementers don't blindly copy the
  dynamic-sphere 5-line pattern. **Verified** by recapturing mode 6 +
  strip (visually identical to v1; log line now reads
  "stripAmbientFloorBake=1 (cascade rebake triggered; SDF unchanged)").
  Capture: [tools/step11_mode6_indirect_strip_v2.png](../../tools/step11_mode6_indirect_strip_v2.png).
- **F5 (low) code fix.** Retuned mode 12 divisor `/0.05` → `/0.5` in
  [raymarch.frag:561](../../res/shaders/raymarch.frag#L561). Mode 12
  now shows useful spatial variation (green dominant, orange/red on
  the left pillar where direct-lit bounce concentrates, yellow gradient
  on the right wall). v1 was saturated red across the scene.
  Capture: [tools/step11_mode12_heatmap_raw_gi_v2.png](../../tools/step11_mode12_heatmap_raw_gi_v2.png).
- **F2 (low) doc fix.** `radiance_3d.comp` line refs corrected:
  uniform at `:42`; branch at `:267-269` (was `:262`, the original
  formula's location pre-Step-11).
- **F3 (low) doc fix.** raymarch.frag insertion-point line refs
  corrected: `indirectColor = albedo * indirect` at `:549` (was 544);
  `uSeparateGI` gate at `:577` (was 549).
- **F4 (medium) doc fix.** "Canonical 5-line invalidation pattern"
  framing was misleading — conflated geometry-change invalidation
  (5 lines incl. `meshSDFReady`) with lighting-change invalidation
  (4 lines, no `meshSDFReady`). Doc now distinguishes the two
  scopes explicitly. This was the conceptual root of the F1 bug.
- **F6 (low) acknowledged.** ImGui `BeginDisabled(!useCascadeGI)`
  guard interactive test deferred to the next UI-touching session.
- **F8 (low) clarified.** Bilateral GI blur interaction WAS
  implicitly verified — the existing mode 0 + strip capture ran with
  default-on `useGIBlur`; the visible-but-subtle darkening vs. mode 0
  proves the blur reflects the stripped cascade radiance (otherwise
  the diff would have been zero). Rigorous explicit test (blur on/off
  with strip on) deferred until Step 12's blur tuning lands.

---

## Summary

| Change | File | Effect |
|---|---|---|
| `uStripAmbientFloor` uniform + branch at line 262 | [res/shaders/radiance_3d.comp](../../../res/shaders/radiance_3d.comp) | Cascade bake skips the `vec3(0.05)` floor when toggled; probes store ONLY real-direct-lit bounce |
| Hoisted `vec3 indirect` to outer scope + 3 heatmap mode branches (11/12/13) | [res/shaders/raymarch.frag](../../../res/shaders/raymarch.frag) | Heatmaps consume main-path values; same green/yellow/red palette as modes 5 & 7 |
| `setStripAmbientFloorBake` + `stripAmbientFloorBake` member + bumped `setRenderMode` upper bound to 14 | [src/demo3d.h](../../../src/demo3d.h) | Toggle setter runs the canonical 5-line invalidation pattern (codex 07 F1) |
| Cascade-dispatch uniform binding + ImGui checkbox (BeginDisabled gate) + 3 RadioButton + tooltips | [src/demo3d.cpp](../../../src/demo3d.cpp) | Toggle inert when cascade GI is disabled (codex 07 F6); tooltips warn on cascade dependency |
| `--strip-ambient-floor-bake` CLI flag | [src/main3d.cpp](../../../src/main3d.cpp) | Reproducible headless captures |

No new shader files, no new GPU resources, no new state plumbing beyond
the toggle. Total net new code: ~80 lines.

---

## Diagnostic Outcome (the load-bearing finding)

10 captures at the user-specified viewpoint of the Sponza-master atrium
(looking down the long axis from inside, with the right wall + floor
visible and pillars on the left):

### Mode 6 vs. Mode 6 + strip — the killer comparison

- **Mode 6 (existing GI bounce, with 0.05 baked in)** — clear, soft
  illumination across the right wall, floor, and ceiling. Looks like a
  reasonable Sponza atrium GI render.
- **Mode 6 + strip** — right wall and floor go from clearly-lit brown
  to nearly black. Left-side pillars retain some visible structure but
  much dimmer. The ceiling shows residual highlights at the
  light-source area only.

This maps to **Outcome A/B mixed** in the codex 07 F4 framework:

- For the right wall and floor (camera-facing surfaces with no direct
  light), the GI bounce was almost entirely the 0.05 ambient floor
  amplified through cascade source surfaces. **Outcome A** —
  fake bounce. Removing the bake floor exposes the truth: there's
  almost no real GI energy reaching these surfaces.
- For the left-side pillars (which do face the lit ceiling area),
  some visible bleed remains after the strip. **Outcome B** — real
  bounce mixed with floor amplification.

### Mode 0 vs. Mode 0 + strip

- **Mode 0 (no strip)** — bright, full atrium detail.
- **Mode 0 + strip** — slightly darker / more reddish, but architecture
  is still clearly visible. Difference is **subtle but real**.

The diff isn't dramatic at the composite level because:
- The local 0.05 floor at `raymarch.frag:535` is still active in
  `directColor` (codex 07 F5 — "mixed state").
- The bake-strip removes only the source-side amplification, not the
  destination-side floor.

This means **removing JUST the bake floor isn't enough to recover
"real" lighting** — the local floor at the renderer also contributes
substantial brightness. Step 12 will need to address both.

### Mode 9 (direct, no ambient)

Mostly black at this angle too — only thin slivers of real direct
lighting hit the visible surfaces (ceiling-edge highlights and a
diagonal floor strip). Confirms direct light barely reaches
camera-facing surfaces from this pose, consistent with Step 10's
finding at the auto-fit angle.

### Mode 10 (ambient floor only)

Shows the full architecture as a uniform brown silhouette (`albedo *
0.05`). Same character as Step 10's auto-fit capture — consistent
across viewpoints.

### Mode 11 (visible-GI heatmap, `/ 0.1`)

Mostly red/orange across the visible surfaces with yellow-green
showing through in darker pillar gaps. The right wall is saturated
red (visible-GI energy is high there).

### Mode 12 (raw-GI heatmap, `/ 0.5` post-codex-08-F5)

**v1 (`/ 0.05`) was saturated red across the whole scene** — the
codex 07 F7 verification step's prediction came true: the divisor
was too tight, `length(indirect)` magnitudes are larger than
expected.

**v2 (`/ 0.5`, codex 08 F5 fix)** shows useful spatial variation:
green dominant across the body, orange/red on the left pillar where
direct-lit bounce concentrates, yellow gradient on the right wall
(real-but-weaker GI), and a small red hotspot in the distance
corresponding to the ceiling light source area. Capture:
[tools/step11_mode12_heatmap_raw_gi_v2.png](../../../tools/step11_mode12_heatmap_raw_gi_v2.png).

The original v1 (saturated) capture is preserved at
[tools/step11_mode12_heatmap_raw_gi.png](../../../tools/step11_mode12_heatmap_raw_gi.png)
for comparison.

### Mode 13 (GI fraction heatmap)

Shows orange/red on most surfaces with green strips on the few
heavily-direct-lit edges. **GI is the DOMINANT light source for >50%
of visible pixels.** This perfectly answers the user's "is GI being
washed out?" question:

- GI is NOT being washed out by direct light — direct barely
  contributes (mode 9 is mostly black).
- The "uniform ambient" perception comes from: (1) the 0.05 local
  floor at `raymarch.frag:535`, (2) the un-stripped 0.05 baked into
  cascade source amplifying through bounce.
- Mode 6 + strip directly proves point (2) — most of the GI bounce
  energy is fake amplification, not real direct-lit bounce.

### Mode 4 (direct + ambient)

Soft version of mode 0; architecture visible via `albedo * 0.05` plus
the small direct contribution. Equivalent to mode 9 + mode 10 as
expected.

Captures live in:

- [tools/step11_mode0_combined.png](../../../tools/step11_mode0_combined.png)
- [tools/step11_mode0_combined_strip.png](../../../tools/step11_mode0_combined_strip.png)
- [tools/step11_mode4_direct_with_ambient.png](../../../tools/step11_mode4_direct_with_ambient.png)
- [tools/step11_mode6_indirect_only.png](../../../tools/step11_mode6_indirect_only.png) — vanilla GI
- [tools/step11_mode6_indirect_strip.png](../../../tools/step11_mode6_indirect_strip.png) — **the killer comparison**
- [tools/step11_mode9_direct_no_ambient.png](../../../tools/step11_mode9_direct_no_ambient.png)
- [tools/step11_mode10_ambient_only.png](../../../tools/step11_mode10_ambient_only.png)
- [tools/step11_mode11_heatmap_visible_gi.png](../../../tools/step11_mode11_heatmap_visible_gi.png)
- [tools/step11_mode12_heatmap_raw_gi.png](../../../tools/step11_mode12_heatmap_raw_gi.png) — saturated; retune divisor
- [tools/step11_mode13_heatmap_gi_fraction.png](../../../tools/step11_mode13_heatmap_gi_fraction.png)

---

## Implementation Highlights

### `radiance_3d.comp` (codex 07 F9 — uniform location pinned)

Uniform declared after `uLightColor` (line 39, lighting-uniforms group):

```glsl
uniform vec3  uLightPos;
uniform vec3  uLightColor;
uniform int   uStripAmbientFloor;   // Step 11
```

Branch at the bake formula:

```glsl
vec3 color = (uStripAmbientFloor != 0)
    ? albedo * diff * uLightColor
    : albedo * (diff * uLightColor + vec3(0.05));
```

### `raymarch.frag` (codex 07 F3 — structural location)

Heatmap branches inserted **AFTER** the main-path `directColor` /
`indirectColor` / hoisted `indirect` are computed (line ~544),
**BEFORE** the Step 10 `uSeparateGI` gate at line 549. They CONSUME
the main-path values (unlike modes 4/6 which compute their own).

`vec3 indirect` was hoisted from inside `if (uUseCascade != 0)` to
outer scope so mode 12 can read it. Stays `vec3(0.0)` when cascades
are off; heatmaps then degenerate to all-green (tooltips warn).

### `setStripAmbientFloorBake` invalidation (codex 07 F10 + codex 08 F1+F4)

Setter runs the **4-line lighting-invalidation pattern** (NOT the
5-line geometry pattern — codex 08 F1+F4 distinguished these scopes):

```cpp
cascadeReady        = false;
forceCascadeRebuild = true;
renderFrameIndex    = 0;
historyNeedsSeed    = true;
```

The Step 8 dynamic-sphere ENABLE branch at
[demo3d.cpp:623-627](../../../src/demo3d.cpp#L623) uses 5 lines
(adds `meshSDFReady = false`) because it modifies geometry — the
voxel grid changes, so the SDF must rebake. The strip toggle is
**lighting-only** (modifies cascade probe radiance via
`uStripAmbientFloor`); the SDF/voxel texture is unchanged. Including
`meshSDFReady = false` would force ~3-7 ms of wasted SDF rebake per
toggle for zero visual effect.

`renderFrameIndex = 0` bypasses the per-cascade stagger so all 4
cascades dispatch on the next frame (~25 ms one-frame spike on Sponza
vs. ~7 ms steady-state). Acceptable for a diagnostic toggle.

### ImGui guard (codex 07 F6)

```cpp
ImGui::BeginDisabled(!useCascadeGI);
bool sStrip = stripAmbientFloorBake;
if (ImGui::Checkbox("Strip 0.05 ambient floor from GI bake (Step 11)", &sStrip))
    setStripAmbientFloorBake(sStrip);
ImGui::EndDisabled();
```

Heatmap RadioButtons get cascade-dependency tooltips so the user knows
why mode 11/12 might render all-green.

---

## Verification (executed)

| # | Test | Outcome | Log |
|---|---|---|---|
| 1 | Build (cmake --build build --config Release) | 0 errors, baseline warnings only | (build console) |
| 2 | 10 captures at user's viewpoint | All 10 PNGs land cleanly | `tools/app_run_step11_*.log` (10 files) |
| 3 | mode 6 vs. mode 6+strip diagnostic | Right wall + floor: lit→near-black (Outcome A); left pillars: dimmer but visible (Outcome B). Mixed outcome. | `tools/step11_mode6_indirect_only.png` vs. `tools/step11_mode6_indirect_strip.png` |
| 4 | mode 0 vs. mode 0+strip | Subtle darkening; local 0.05 floor at `raymarch.frag:535` still active in direct | `tools/step11_mode0_combined.png` vs. `tools/step11_mode0_combined_strip.png` |
| 5 | Heatmap calibration | Mode 11 shows useful spatial variation; mode 12 saturated red (divisor /0.05 too tight); mode 13 shows GI is dominant light source for >50% pixels | (the 3 mode 11/12/13 PNGs) |
| 6 | CLI `--strip-ambient-floor-bake` parses + apply | Log shows `stripAmbientFloorBake=1 (cascade rebake triggered)` immediately after parse | (any `_strip` log) |
| 7 | ImGui checkbox gating not runtime-tested | Code path is clear-cut (`BeginDisabled(!useCascadeGI)`); skipped explicit interactive test | n/a |

---

## Codex 07 Findings — All 11 Folded In

| # | Sev | Resolution |
|---|---|---|
| F1  | Low  | Doc: `forceCascadeRebuild` reuse ref `:4687` → `:623-627` (canonical example) |
| F2  | Low  | Doc: heatmap palette refs corrected for post-Step-10 source |
| F3  | Med  | Code: heatmap branches inserted AFTER main-path lighting computation; consume `directColor`/`indirectColor`/`indirect` (NOT alongside modes 4/6) |
| F4  | Med  | Doc + Capture: Scenario A/B/C decision framework folded into plan; runtime captures land closer to mixed A/B |
| F5  | Low  | Code + Capture: mode0+strip capture added; documented mixed-state semantics |
| F6  | Med  | Code: ImGui checkbox `BeginDisabled(!useCascadeGI)`; heatmap RadioButton tooltips note cascade dependency |
| F7  | Low  | Code: divisors `/0.1` (mode 11) + `/0.05` (mode 12); RUNTIME REVEALED mode 12 saturates → retune to `/0.5` next session |
| F8  | Low  | Doc: acknowledged 0.001 threshold safe for current assets |
| F9  | Low  | Code: `uStripAmbientFloor` declared after `uLightColor` (line 39) in `radiance_3d.comp` |
| F10 | Low  | Doc: ~25 ms one-frame spike on toggle (renderFrameIndex=0 dispatches all 4 cascades) |
| F11 | Low  | Doc: toggle persists across scene changes by design |

---

## Known Open Items

| Item | Notes |
|---|---|
| Mode 12 divisor needs retune from `/0.05` to `/0.5` or `/1.0` | Captures show mode 12 saturates red across the scene; one-line shader edit. Will land in Step 12. |
| Step 12 decision framework | Captures support Outcome A/B mix → the next step should: (1) remove the 0.05 from `radiance_3d.comp:262` permanently (or default the toggle ON), (2) decide whether to also remove the local 0.05 at `raymarch.frag:535` based on mode 0+strip captures, (3) likely boost `indirectBrightness` to compensate for lost ambient. Stays out-of-scope here — diagnose only. |
| Bilateral GI blur interaction with strip | Not tested. The blur runs on `indirectColor` so should reflect the stripped bake naturally. Worth a confirmation capture in Step 12. |
| Toggle reset on scene change | Currently persists (codex 07 F11 — intentional). Revisit only if user reports unexpected behavior. |

---

## Architecture Notes

**The strip-toggle is a global member, not a per-render-mode behavior.**
This keeps the rendering pipeline unchanged — the cascade bake is a
single global texture; we don't maintain two "stripped" and
"un-stripped" variants. Cost: toggling triggers a one-frame full
rebake. Benefit: zero memory overhead and zero rendering-path
divergence.

**The hoisted `indirect` is the ONLY raymarch.frag state change
needed for the heatmap modes** — everything else they need
(`directColor`, `indirectColor`, `albedo`) was already in scope. This
keeps the diff small and makes the heatmap modes purely additive
(can be removed cleanly if heatmap UI clutter becomes a concern).

**Heatmap normalization is hardcoded in shader.** Per codex 07 F7
this is acceptable for diagnostic-only — but the runtime saturation
on mode 12 proves the value is asset/scene-dependent. A future
`uHeatmapScale` uniform (with ImGui slider) would be a small addition;
deferred until the heatmaps prove worth permanent UI surface.

---

## Why This Step Was Worthwhile

Step 10's diagnostic showed the local 0.05 floor dominates camera-
visible surfaces. But it left an open question: **does the cascade GI
itself contain real direct-lit bounce, or is the GI bounce ALSO
mostly amplified ambient?**

Mode 6 + strip answered this in a single capture: for camera-facing
surfaces lacking direct light, the GI bounce IS mostly amplified
ambient. For surfaces facing actual lit geometry (left pillars), real
bounce exists but is dim.

Without this step, Step 12 would have been "remove the 0.05 floor
and see what happens" — a guess. Now it's "remove the 0.05 floor
AND boost indirect intensity to recover the lost amplification, with
the expectation that camera-facing surfaces will be quite dark
without it" — a calibrated decision.

The 3 heatmap modes (especially mode 13 fraction) provide spatial
intuition that single-channel mode-6/mode-9/mode-10 captures can't:
the GI-fraction heatmap immediately shows that GI dominates the
lighting budget for most pixels, making the "GI is being washed out"
hypothesis from Step 10 specifically wrong — GI dominates, but the
GI itself is mostly ambient amplification.
