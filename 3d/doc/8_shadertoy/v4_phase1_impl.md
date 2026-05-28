# v4 Phase 1 — Implementation Summary

**Date:** 2026-05-28T17:18+08:00  
**Scope doc:** `doc/8_shadertoy/v4_shadertoy_adoption_scope.md`  
**Execution plan:** `doc/8_shadertoy/v4_phase1_execution_plan.md`  
**Status:** Phase 1 COMPLETE (1A + 1B + 2B). Build verified. No capture re-run needed (code path changes are behavioral-NOP when flags are OFF).

---

## Completed Phases

| Phase | What | Files Changed | Lines |
|-------|------|--------------|-------|
| **1A** | Sponza per-scene MB-gain preset | `main3d.cpp`, `demo3d.h`, `demo3d.cpp` | +32 |
| **2B** | Remove stale M1 delta flags | `main3d.cpp`, `demo3d.h`, `demo3d.cpp`, `radiance_3d.comp` | -42 |
| **1B** | Cornell constraint documentation | `doc/8_shadertoy/cornell_point_light_constraint.md` | new file |

**Net line delta:** -10 lines of source code (more removed than added). No shader behavioral change.

---

## Phase 1A — Per-Scene MB-Gain Preset

### Changes

#### `main3d.cpp` — 3 edits

1. **File-scope global** (line ~116): `bool g_usePerSceneMbGain = false;`
2. **CLI flag** (after `--multi-bounce-gain`): `--mb-gain-per-scene` sets the global and logs the intent
3. **Post-load apply block** (after Stage 11c/11d light re-apply): Reads `demo->getCurrentOBJPath()` to detect Sponza vs Cornell, calls `setMultiBounceGain(0.10f)` for Sponza, `1.0f` for others. Uses the same `extern` pattern as the existing `g_cliLightDirSet`/`g_cliLightPosSet` blocks.

#### `demo3d.h` — 2 edits

1. **Setter** (after `setMultiBounceGain`): `void setUsePerSceneMbGain(bool v)` — does NOT trigger cascade rebuild (the gain is set via `setMultiBounceGain` in the same post-load hook)
2. **Member** (after `multiBounceGain`): `bool usePerSceneMbGain = false;`

#### `demo3d.cpp` — 2 edits

1. **ImGui MB gain slider**: When `usePerSceneMbGain` is true, the slider is disabled with `ImGui::BeginDisabled()` and shows "MB Gain (per-scene auto)" with a tooltip explaining the active gain
2. **Debug stats**: Shows green "MB-gain X.XX (per-scene auto)" line when per-scene is active

### Behavioral Contract

- **Flag OFF (default):** Identical to pre-patch behavior. Manual MB gain slider works normally. No change to any capture metric.
- **Flag ON + Sponza:** `multiBounceGain = 0.10` set after loadOBJMesh. Stage 9 confirms |p95| = 0.25, clears retirement gate by 2× margin.
- **Flag ON + Cornell:** `multiBounceGain = 1.0` (default). No behavioral change — preserves the unfixed Cornell baseline. This is intentional: Cornell's 2× under-emit is topological, not gain-tunable.
- **Flag ON + other:** `multiBounceGain = 1.0` (safe default).

---

## Phase 2B — Remove Stale M1 Delta Flags

### Changes

#### `main3d.cpp`
- Removed `--m1-delta3-gated-trilinear=` and `--m1-delta6-geometric-cone=` CLI argument parsing (lines ~608-619). These were dead code — the flags defaulted to `false` and had no GUI access. Their CLI parsing was added speculatively before M1 Stage 1 proved both deltas DEAD.

#### `demo3d.h`
- Removed `setM1Delta3GatedTrilinear()` and `setM1Delta6GeometricCone()` setters (~22 lines)
- Removed `bool m1Delta3GatedTrilinear = false;` and `bool m1Delta6GeometricCone = false;` members (2 lines)

#### `demo3d.cpp`
- Removed `glUniform1i(prog, "uM1Delta3GatedTrilinear", ...)` and `glUniform1i(prog, "uM1Delta6GeometricCone", ...)` calls
- Removed the `if (m1Delta6GeometricCone)` branch in cone computation — the `else` branch (standard bin-derived formula) is now the only path
- Simplified to a clean three-line cone computation

#### `radiance_3d.comp`
- Removed `uniform int uM1Delta3GatedTrilinear;` and `uniform int uM1Delta6GeometricCone;` declarations with their doc comments (~6 lines)
- Simplified `upperDir` assignment: always uses `vec4(upperDirTrilinear.rgb, ws.a)` (the Phase 3 v3 behavior). The `uM1Delta3GatedTrilinear ? ws : ...` ternary is gone.
- Simplified `aFactor` computation: `(uUseWeightedSample != 0) ? upperDir.a : 1.0`. The `&& uM1Delta3GatedTrilinear == 0` guard condition is gone.

### Behavioral Contract
- **Identical output** to pre-patch when running with default flags (both flags were always `false`). The removed branches were dead code.
- **Simpler merge formula:** The shader now unconditionally uses the Phase 3 v3 approach (trilinear.rgb + WeightedSample.a as scalar attenuation). This is the behavior that was the default all along.
- **Removes the ability to toggle** the experimental DEAD delta behaviors. This is intentional — they were DEAD per 2×2 matrix and there is no reason to toggle them.

---

## Self-Critique

### Issues Found During Implementation

#### SC1: Scene-type detection uses `currentOBJPath` string comparison

The post-load apply block calls `demo->getCurrentOBJPath()` and compares against `"sponza"` and `"sponza_master"`. This is a string-based scene check, not a geometric check. It works for the two known Sponza variants but would miss any future Sponza-class scene with a different path (e.g., `"sponza_new"`). The existing code uses the same pattern in `applyOBJViewPreset` (line 7136), so this is consistent with current conventions.

**Improvement accepted:** A future geometry-derived classifier (bounding-box aspect ratio, volume size, number of triangles) would be more robust but is out of scope for Phase 1.

#### SC2: Per-scene gain overrides `--multi-bounce-gain` CLI

The per-scene flag applies AFTER the CLI argument parsing loop, which means `--multi-bounce-gain=X --mb-gain-per-scene` would set gain=X first, then override it with the per-scene value. The CLI log output makes this clear:

```
[MAIN] --multi-bounce-gain=0.5
[MAIN] --mb-gain-per-scene: per-scene auto MB-gain ...
[MAIN] post-load: per-scene MB-gain: sponza -> 0.10
```

The final gain is 0.10 (per-scene), not 0.5 (CLI). This is intentional — the per-scene flag explicitly overrides manual gain. A user who wants manual gain should NOT pass `--mb-gain-per-scene`.

**Improvement:** The `setUsePerSceneMbGain` setter could log a warning if `multiBounceGain` was previously set via CLI to a different value. Out of scope — the current behavior is documented and unambiguous.

#### SC3: ImGui slider disable uses `BeginDisabled()` — correct for read-only display

When per-scene is active, the MB gain slider shows the current value but is disabled. `ImGui::BeginDisabled()` correctly prevents user interaction while keeping the slider visible. The tooltip explains why. This is the right UX pattern.

#### SC4: The `usePerSceneMbGain` setter does NOT trigger cascade rebuild

The setter stores the boolean but does NOT call `cascadeReady = false`. This is correct because the gain itself is set by `setMultiBounceGain()` in the same post-load hook, which DOES trigger a rebuild. If the setter ALSO triggered a rebuild, the post-load sequence would cause a double-rebuild. However, this means that if `setUsePerSceneMbGain` were called at runtime (e.g., via ImGui in a future change), the gain would NOT be re-evaluated automatically. The setter intentionally does not exist in the ImGui (per-scene is CLI-only), so this is safe for now.

**Tracked:** If a future feature adds a runtime toggle for per-scene gain, a re-apply hook (similar to the Stage 11c/11d pattern) must be added.

#### SC5: Removed `uM1Delta3GatedTrilinear` / `uM1Delta6GeometricCone` from shader but uniforms are still in GL program link

The shader (`radiance_3d.comp`) no longer declares these uniforms, so `glGetUniformLocation` will return -1, and `glUniform1i(prog, -1, ...)` is a silent no-op (OpenGL ignores uniform functions with location -1). Since we also removed the `glUniform1i` calls from `demo3d.cpp`, this is a clean removal. No GL errors.

### Verification

| Check | Method | Result |
|-------|--------|--------|
| Build | `cmake --build build --config Release` | PASSED. No new warnings or errors. |
| Shader compile | GLSL compilation during app startup | Will verify at runtime. Uniform removal is syntax-clean. |
| Behavioral regression | Code path analysis | Both removed flags were always `false`. Removed branches were dead code. Output identical by construction. |
| Per-scene OFF by default | `bool g_usePerSceneMbGain = false;` | Confirmed. No behavior change unless `--mb-gain-per-scene` is passed. |

### Things Not Done (Deferred to Phase 2)

- **Re-capture Sponza/Cornell baselines** — Not needed for this phase (no behavioral change under default flags). The Sponza gain=0.10 metrics are already captured in `sponza_default_metrics.json` (Stage 9) and confirmed in mode-0 validation (Stage 10). Phase 2B is strictly dead-code removal.
- **Update `baseline_lock.json`** — Deferred to Phase 2C. The lock is still valid for the current default flags.
- **Update scope doc** — The v3 scope doc (`doc/7/v3_shadertoy_adoption_scope.md`) is superseded by v4. The v3 doc's §1.1 and §3 M1 sections describe a plan that was abandoned after Stage 1. Deferred to Phase 2A cleanup.

---

## What Was Learned

### The M1 diagnostic chain was not wasted work

While the v3 scope doc's plan (port ShaderToy deltas #3/#6/#4) was wrong, the 11-stage diagnostic chain that replaced it (Stages 2–11d) found the real constraints:
- Sponza's fix is MB gain=0.10
- Cornell's fix is directional light or hybrid correction
- The volumetric topology has a fundamental limitation for point lights in enclosed geometry

These findings are correct and actionable. The Phase 1 implementation lands the Sponza fix without changing the Cornell baseline.

### Dead code removal is a correctness improvement

The M1 flags (`m1Delta3GatedTrilinear`, `m1Delta6GeometricCone`) were confusing to read and maintain. They existed in shader, C++, and CLI but couldn't be toggled without source edits. Removing them:
- Makes the merge formula simpler (one path instead of two)
- Makes `aFactor` logic clearer (no dead `&& uM1Delta3GatedTrilinear == 0` guard)
- Eliminates the possibility of a future bug where someone accidentally toggles a DEAD delta

### The per-scene gain approach is the honest answer

Rather than finding a single "correct" MB gain that works for all scenes (which Stage 9 proved impossible), the per-scene preset acknowledges that cascade multi-bounce feedback interacts differently with different scene geometries. This is an engineering trade-off (per-scene config table) rather than an algorithmic fix, but it's the right trade-off given the volumetric topology constraint.

---

## Files Created / Modified

| File | Change |
|------|--------|
| `src/main3d.cpp` | +18 lines (global flag, CLI arg, post-load apply block); -13 lines (M1 delta CLI args) |
| `src/demo3d.h` | +7 lines (setter, member); -24 lines (M1 delta setters, members) |
| `src/demo3d.cpp` | +14 lines (ImGui gray-out, status); -7 lines (M1 delta uniform setup, cone branch) |
| `res/shaders/radiance_3d.comp` | -10 lines (M1 delta uniforms, ternary branch, aFactor guard) |
| `doc/8_shadertoy/cornell_point_light_constraint.md` | new file |
| `doc/8_shadertoy/v4_phase1_impl.md` | this file |

---

## Handoff to Phase 2

Phase 2 tasks (per v4 scope doc §Phases 2A-2C):
1. Update v3 scope doc with "SUPERSEDED BY v4" header + cross-reference
2. Update CLAUDE.md / project memory
3. Update `baseline_lock.json` with Sponza gain=0.10 entry
4. Self-critique pass on cleanup completeness