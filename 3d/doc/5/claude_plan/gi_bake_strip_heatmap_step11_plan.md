# Plan: Step 11 — GI Bake Strip + Heatmap Diagnostic Modes (revised after codex 07)

## Changelog (post codex `07_gi_bake_strip_heatmap_step11_plan_review.md`)

All 11 findings accepted. Plan revised before any code lands:

- **F1 (low) doc fix.** `forceCascadeRebuild` reuse reference was
  `demo3d.cpp:4687` (RenderDoc capture prep) — corrected to
  `:623-627` (Step 8 dynamic-sphere ENABLE branch, the canonical
  example).
- **F2 (low) doc fix.** Heatmap palette line refs were stale from
  pre-Step-10 source. Mode 5: `:576-582` → `:588-594`. Mode 7:
  `:486-495` → `:489-495`. (Step 10 added ~12 lines around the
  alpha-composite.)
- **F3 (medium) doc fix.** "Insert as early-returns matching mode
  4/6/7 pattern" was structurally misleading: modes 4 and 6 compute
  their own lighting BEFORE the main-path `directColor`/
  `indirectColor`; the new heatmap modes 11/12/13 must consume the
  main-path values + the hoisted `indirect`. Reworded to "early-
  returns AFTER the main-path lighting computation (after line 544),
  BEFORE the Step 10 uSeparateGI gate at line 549; CONSUMES
  main-path values, unlike modes 4/6 which compute their own".
- **F4 (medium) plan addition.** Added a Scenario A/B/C decision
  framework for the `mode 6 vs. mode 6 + strip` comparison — without
  it the captures are ambiguous on what Step 12 should do.
- **F5 (low) plan addition.** Added mode 0 + strip capture +
  documentation of the "mixed state" (local floor still active in
  direct, bake floor stripped from indirect).
- **F6 (medium) plan revision.** ImGui checkbox now wrapped in
  `BeginDisabled(!useCascadeGI)`; tooltips on heatmap RadioButtons
  (modes 11/12/13) noting cascade dependency.
- **F7 (low) doc fix.** Heatmap divisors changed from `/0.5` to
  `/0.1` (mode 11 visible-GI) and `/0.05` (mode 12 raw-GI), justified
  by Step 10 mode-6 magnitudes (~0.04 body, ~0.34 rim). Mode 13
  unchanged (naturally [0,1]). Verification step added: "retune if
  heatmap saturates".
- **F8 (low) acknowledged.** Mode 13's `total > 0.001` threshold is
  safe for current Sponza/Cornell albedos (~0.5); only fails for
  near-zero albedo which doesn't exist in current assets. Documented.
- **F9 (low) doc fix.** `uStripAmbientFloor` uniform declaration
  location pinned: after `uLightColor` at line 39 in
  `radiance_3d.comp` (same lighting-uniforms group).
- **F10 (low) doc note.** First frame after toggling runs ALL 4
  cascades (`renderFrameIndex=0` bypasses stagger) — ~25 ms vs.
  ~7 ms steady-state. One-frame spike acceptable for diagnostic.
- **F11 (low) doc note.** Toggle persists across scene/OBJ changes;
  documented as intentional (user diagnosing across multiple scenes
  wants stable toggle).

## Context

Step 10 confirmed via captures that the `vec3(0.05)` ambient floor at
[raymarch.frag:535](../../../res/shaders/raymarch.frag#L535) dominates camera-visible
surfaces in Sponza. But the floor is ALSO baked into cascade probe radiance at
[radiance_3d.comp:262](../../../res/shaders/radiance_3d.comp#L262)
(`color = albedo * (diff * uLightColor + vec3(0.05))`), so existing mode 6's
"GI bounce only" output still contains 0.05 contribution from every source
surface — meaning we cannot tell what the GI looks like for "real" direct
lighting bounces vs. ambient-floor-amplified-into-bounce.

This step adds:
1. **A toggle that strips the 0.05 floor from the cascade BAKE** (not just the
   final composite), with rebake on toggle. Mode 6 then becomes "GI bounce
   from real direct light only".
2. **3 GI heatmap render modes** (visible GI, raw GI, GI fraction) so the
   "where is GI doing real work?" question has a green/yellow/red answer
   matching the existing mode-5/7 step heatmap convention.
3. Captures from a specific user-supplied viewpoint
   (`pos=1.0710,-0.0723,-0.3393  target=0.1212,-0.0812,-0.6520`) so the
   diagnostic is reproducible.

---

## Implementation Order

### Step 1 — `res/shaders/radiance_3d.comp`: gate the 0.05 floor

**Codex 07 F9: uniform declaration goes after `uLightColor` (line 39)**,
in the lighting-uniforms group:

```glsl
uniform vec3  uLightPos;
uniform vec3  uLightColor;
uniform int   uStripAmbientFloor;   // Step 11: strip vec3(0.05) from probe bake
uniform int   uUseEnvFill;
```

Branch at the bake site (line 262):

```glsl
// line 262 (current):  vec3 color = albedo * (diff * uLightColor + vec3(0.05));
vec3 color = (uStripAmbientFloor != 0)
    ? albedo * diff * uLightColor
    : albedo * (diff * uLightColor + vec3(0.05));
```

When the toggle is on, cascade probes store ONLY real-direct-lit bounce.

### Step 2 — `src/demo3d.cpp`: bind the new uniform + invalidate on toggle

In the cascade-dispatch uniform block at
[demo3d.cpp:~2078](../../../src/demo3d.cpp#L2078) (right after `uLightColor`), add:

```cpp
glUniform1i(glGetUniformLocation(prog, "uStripAmbientFloor"),
            stripAmbientFloorBake ? 1 : 0);
```

Add a member `bool stripAmbientFloorBake = false;` to demo3d.h.

Add a public setter that triggers rebake:

```cpp
void setStripAmbientFloorBake(bool v) {
    if (stripAmbientFloorBake == v) return;
    stripAmbientFloorBake = v;
    forceCascadeRebuild = true;
    cascadeReady = false;
    historyNeedsSeed = true;
    renderFrameIndex = 0;
    std::cout << "[Demo3D] stripAmbientFloorBake=" << v << " (cascade rebake)\n";
}
```

Add an ImGui checkbox in `renderSettingsPanel` near the GI-blur controls
(or alongside the new GI-diagnostic mode buttons). **Codex 07 F6: gray
out when cascade GI is disabled** — toggle is meaningless without
cascades:

```cpp
ImGui::BeginDisabled(!useCascadeGI);
bool s = stripAmbientFloorBake;
if (ImGui::Checkbox("Strip 0.05 ambient floor from GI bake", &s))
    setStripAmbientFloorBake(s);
ImGui::EndDisabled();
if (!useCascadeGI && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Disabled — requires Cascade GI to be enabled.");
```

**Codex 07 F10**: the first frame after toggling will dispatch ALL 4
cascades (`renderFrameIndex=0` bypasses stagger). Expect a one-frame
~25 ms spike vs. ~7 ms steady-state on Sponza. Acceptable for a
diagnostic toggle.

**Codex 07 F11**: the toggle persists across `setScene()` /
`loadOBJMesh()` calls (it's a `Demo3D` member). Intentional — a user
diagnosing across multiple scenes wants the toggle stable.

### Step 3 — `res/shaders/raymarch.frag`: add 3 heatmap modes (11/12/13)

Hoist `vec3 indirect = vec3(0.0);` to outer scope (currently inside
`if (uUseCascade != 0)` at ~line 540) so the raw probe radiance is reachable
by mode 12. When cascades are off, `indirect` stays `vec3(0.0)` and
heatmaps degenerate to all-green (codex 07 F6 — covered by tooltips
on the RadioButtons; see Step 4 below).

**Codex 07 F3 — structural location.** Insert as **early-returns
AFTER the main-path lighting computation** (after line 544 where
`indirectColor = albedo * indirect` is computed), BEFORE the Step 10
`uSeparateGI` gate at line 549. The heatmap modes CONSUME the
main-path values — unlike modes 4/6 which compute their own lighting
terms BEFORE the main path. Inserting the heatmap branches near the
existing mode 4/6 branches (line ~498-526) would fail because
`indirect` doesn't exist yet at that point.

Same green→yellow→red palette as modes 5
([raymarch.frag:588-594](../../../res/shaders/raymarch.frag#L588))
and 7
([raymarch.frag:489-495](../../../res/shaders/raymarch.frag#L489))
(codex 07 F2: line refs corrected for post-Step-10 source).

```glsl
// Step 11 — GI heatmaps. Same green/yellow/red palette as modes 5 & 7.
if (uRenderMode == 11 || uRenderMode == 12 || uRenderMode == 13) {
    float v;
    // codex 07 F7: divisors picked from Step 10 mode-6 magnitudes
    // (Sponza body ~0.04, rim ~0.34). Retune via shader edit if saturated.
    if      (uRenderMode == 11) v = length(albedo * indirect) / 0.1;        // visible GI
    else if (uRenderMode == 12) v = length(indirect)          / 0.05;       // raw GI (smaller, no albedo modulation)
    else /* 13 */ {
        // codex 07 F8: 0.001 threshold safe for current assets (albedo ~0.5);
        // only fails for near-zero albedo which doesn't exist in current scenes.
        float total = length(directColor + indirectColor);
        v = (total > 0.001) ? length(indirectColor) / total : 0.0;          // already 0..1
    }
    float t8 = clamp(v, 0.0, 1.0);
    vec3 heatColor = (t8 < 0.5)
        ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t8 * 2.0)
        : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t8 - 0.5) * 2.0);
    fragColor = vec4(heatColor, 1.0);
    return;
}
```

Keep the existing Step 10 mode 9/10 + uSeparateGI gate untouched.

### Step 4 — `src/demo3d.cpp` + `.h`: ImGui dropdown + setRenderMode bound

Extend the render-mode RadioButton block at
[demo3d.cpp:3558-3576](../../../src/demo3d.cpp#L3558) with the 3 new entries.
**Codex 07 F6: tooltips note cascade dependency** (heatmap modes
degenerate to all-green when cascades are off):

- Mode 11 (visible GI heatmap): "length(albedo × indirect) / 0.1 →
  green/yellow/red. Requires Cascade GI — output is all-green when
  disabled."
- Mode 12 (raw GI heatmap): "length(indirect) / 0.05 → green/yellow/
  red. Requires Cascade GI — output is all-green when disabled."
- Mode 13 (GI fraction heatmap): "indirect / (direct + indirect) → 0
  to 1. Requires Cascade GI — fraction is 0 when indirect is 0."

Bump `setRenderMode` upper bound from 11 to 14 (with one slot of headroom):

```cpp
if (m < 0 || m > 14) { ... WARN ... }
```

### Step 5 — `src/main3d.cpp`: CLI flag

```
--strip-ambient-floor-bake
```

Apply via `demo->setStripAmbientFloorBake(true)`. Insertion point: with the
other CLI toggles BEFORE the apply block (so the flag is set before the
camera-pos overrides; the rebake will fire on the next frame regardless).

### Step 6 — Build + verify with the user's specific viewpoint

```powershell
cmake --build build --config Release
```

Run captures at the user's pose:
```
--load-obj=sponza-master --gpu-voxelize --gpu-sdf
--camera-pos=1.0710,-0.0723,-0.3393
--camera-target=0.1212,-0.0812,-0.6520
--render-mode=N --exit-frames=180 --screenshot=tools/step11_<...>.png
```

Modes captured (same viewpoint for all):
- `mode0_combined` (baseline)
- `mode0_combined_strip` — **NEW (codex 07 F5)**: same as mode 0 but
  with `--strip-ambient-floor-bake`. Mixed state: local 0.05 floor at
  `raymarch.frag:535` is **still active in directColor**, but the
  bake's 0.05 is **gone from indirectColor**. Diff vs. mode0_combined
  quantifies the bake-floor's contribution to the final composite.
- `mode4_direct_with_ambient` (direct + 0.05 floor)
- `mode6_indirect_only` (existing GI, with 0.05 baked in)
- `mode6_indirect_strip` — same camera + `--strip-ambient-floor-bake` (the
  load-bearing comparison: difference vs. above tells us how much of the
  GI bounce is real-direct-lit vs. ambient-floor-amplified)
- `mode9_direct_no_ambient`
- `mode10_ambient_only`
- `mode11_heatmap_visible_gi`
- `mode12_heatmap_raw_gi`
- `mode13_heatmap_gi_fraction`

Total: 10 captures (was 9 pre-codex-07). Logs to `tools/app_run_step11_*.log`.

**Codex 07 F7 — heatmap normalization sanity check.** If mode 11 or
mode 12 captures are all-green or all-red, the divisor (`0.1` for mode
11, `0.05` for mode 12) is wrong for this scene. Adjust the divisor in
the shader and recapture. Mode 13 (fraction) has no divisor and should
always have spatial variation when GI is non-trivial.

#### Decision framework — `mode 6 vs. mode 6 + strip` outcomes (codex 07 F4)

The captures in isolation are just pictures; we need a decision rule
for what each visual outcome implies about the next fix:

| Outcome | Visual | Interpretation | Implied Step 12 fix |
|---|---|---|---|
| **A** | Mode 6 + strip is **nearly black** everywhere | GI bounce signal is entirely driven by the 0.05 ambient floor amplified through cascade source surfaces. Cascade math works but the bounce energy is fake. | Remove the 0.05 floor from BOTH `radiance_3d.comp:262` AND `raymarch.frag:535`. Possibly boost `indirectBrightness` if real direct lighting alone is too dim. |
| **B** | Mode 6 + strip shows **visible colored bleed** (brick→floor, column→floor, etc.) | GI math produces real direct-lit bounce; the 0.05 in the bake is unwanted padding that washes it out. | Remove the 0.05 ONLY from the bake (`radiance_3d.comp:262`); keep or remove the local floor at `raymarch.frag:535` based on whether mode 0 + strip looks better with or without it. |
| **C** | Mode 6 ≈ mode 6 + strip | The 0.05 contribution to cascade probe radiance is dwarfed by real direct contribution. | Toggle is a no-op for this scene/light setup; revisit with darker scenes (e.g. reduced light intensity, or scenes with mostly-shadowed surfaces). |

Mode 0 + strip (codex 07 F5) provides the supplementary data: the
diff `mode 0 vs. mode 0 + strip` quantifies the bake-floor's
contribution at the final composite (where the local floor is still
present in direct). If that diff is small but Outcome A still
applies, the LOCAL floor is the bigger culprit at the composite even
though removing the bake-floor was necessary for correct GI.

### Step 7 — Update `.wolf/memory.md` + dump impl doc

`doc/5/claude_plan/gi_bake_strip_heatmap_step11_impl.md` following the Step 10
impl-doc template.

---

## Files Modified

- [res/shaders/radiance_3d.comp](../../../res/shaders/radiance_3d.comp) — uniform + branch at line 262 (~5 lines)
- [res/shaders/raymarch.frag](../../../res/shaders/raymarch.frag) — hoist `indirect` + 3 heatmap mode branches (~15 lines)
- [src/demo3d.h](../../../src/demo3d.h) — `stripAmbientFloorBake` member, `setStripAmbientFloorBake`, bump `setRenderMode` upper bound (~10 lines)
- [src/demo3d.cpp](../../../src/demo3d.cpp) — uniform binding, ImGui checkbox, 3 RadioButton + tooltip entries (~25 lines)
- [src/main3d.cpp](../../../src/main3d.cpp) — `--strip-ambient-floor-bake` flag (~6 lines)

No new shader files, no new GPU resources, no new state plumbing beyond the
toggle. Total net new code: ~60 lines.

---

## Reuse from existing code

- `forceCascadeRebuild + cascadeReady = false + historyNeedsSeed +
  renderFrameIndex = 0` — established invalidation pattern (codex 07
  F1: canonical example is the Step 8 dynamic-sphere ENABLE branch
  at [demo3d.cpp:623-627](../../../src/demo3d.cpp#L623); the DISABLE
  branch at [demo3d.cpp:636-640](../../../src/demo3d.cpp#L636) uses
  the same five lines. RenderDoc capture path uses the same pattern
  too.)
- Heatmap palette (green→yellow→red two-segment mix) — copy from existing
  mode 5 ([raymarch.frag:588-594](../../../res/shaders/raymarch.frag#L588)) and
  mode 7 ([raymarch.frag:489-495](../../../res/shaders/raymarch.frag#L489))
  (codex 07 F2: line refs corrected for post-Step-10 source.)
- Step 10 setters/UI/CLI scaffolding — same pattern reused for the new
  toggle and modes
- `--camera-pos` / `--camera-target` flags from Step 10 — the new captures
  use them directly

---

## Out of scope (deferred)

- Per-mode-switch automatic strip-and-rebake (toggle is an explicit user
  action; saves the rebake-on-mode-switch complexity)
- Boosting `indirectBrightness` to compensate for the lost 0.05 — wait for
  the strip captures to inform whether boost is needed
- Removing the `vec3(0.05)` floor from `raymarch.frag:535` (the LOCAL
  floor at the camera surface) — that's a separate decision after we see
  what mode 6 looks like with the bake-strip
- Heatmap normalization sliders (uniform `uHeatmapScale`) — hardcoded
  `/ 0.5` is fine for diagnostic-only this round
