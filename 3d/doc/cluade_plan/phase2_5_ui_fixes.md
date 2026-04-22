# Phase 2.5 — UI Bug Fixes Plan

**Date:** 2026-04-22  
**Source:** Screenshot analysis (RadianceCascades3D_xLL8BKvI8z.png)  
**Branch:** 3d

---

## Issues Identified

Four bugs observed in the running application:

1. Scene Selection UI is redundant / confusing
2. SDF Debug visualization looks wrong
3. RC Debug Render Mode radio buttons have no effect
4. Show Performance Metrics checkbox does nothing

---

## Issue 1 — Scene Selection Redundancy

### Root Cause

The left tutorial panel (`renderTutorialPanel()`, `demo3d.cpp:1699-1733`) contains a "Scene Selection" block with three preset buttons **and** an "Advanced Scenes" block with "Load Cornell Box OBJ". The OBJ loader duplicates the Cornell Box preset. Additionally, having scene selection split across two headers ("Scene Selection" / "Advanced Scenes") with a dangling `Current Scene: 0` label creates visual noise.

### Fix

- Remove the "Advanced Scenes" sub-section and merge "Load Cornell Box OBJ" into the main scene list as a fourth entry named "Cornell Box (OBJ)" so the user understands the distinction (OBJ mesh vs. analytic SDF).
- Replace the bare `ImGui::Text("Current Scene: %d", currentScene)` line with the scene name string (e.g. "Empty Room") so the label is self-explanatory.
- Optionally move scene selection to the right settings panel (`renderSettingsPanel()`) under a collapsible header to free left-panel space.

### Files

| File | Function | Lines |
|------|----------|-------|
| `src/demo3d.cpp` | `renderTutorialPanel()` | 1699–1733 |

---

## Issue 2 — SDF Debug Visualization Looks Wrong

### Root Cause (most likely)

`renderSDFDebug()` (`demo3d.cpp:490-541`) binds the SDF texture and draws a fullscreen quad using `sdf_debug.frag`. Two sub-causes are likely:

**a) SDF texture may be uninitialized / placeholder**  
`Current Features` in the left panel explicitly says `?SDF generation (placeholder)`. If the SDF compute pass (`sdf_3d.comp` / `sdf_analytic.comp`) is not actually dispatched each frame, the texture contains stale data or zeroes, which will show as a flat or noisy image.

**b) The debug quad overwrites the 3D viewport**  
`renderSDFDebug()` sets its own viewport (`glViewport(0, 0, 400, 400)`, lines 506-511) in the bottom-left. If the ordering of render calls is wrong, this can be overdrawn by the main pass or can overwrite part of the main viewport incorrectly.

**c) `visualizeMode` default**  
Default `visualizeMode = 0` is grayscale distance. If the SDF contains mostly large values, the scene shows as near-white. Mode 1 (surface iso-surface) is more informative.

### Fix

1. **Verify SDF dispatch is active:** In `demo3d.cpp` `render()`, confirm `sdfPass()` is called unconditionally (or at least when `sceneDirty` is true). Add a `sdfReady` flag and display it in the debug UI.
2. **Fix viewport restore:** After `renderSDFDebug()`, call `glViewport(0, 0, screenW, screenH)` to restore the main viewport so subsequent passes are not affected.
3. **Change default `visualizeMode` to 1** (surface / iso-contour) for more readable first-time display. Add keyboard shortcut to cycle modes.
4. **Clamp slice position** to ensure it starts at the scene center (0.5) rather than 0.0, which may be outside the populated SDF region.

### Files

| File | Function | Lines |
|------|----------|-------|
| `src/demo3d.cpp` | `renderSDFDebug()` | 490–541 |
| `src/demo3d.cpp` | `renderSDFDebugUI()` | 543–581 |
| `res/shaders/sdf_debug.frag` | `main()` | 25–96 |
| `src/demo3d.h` | state variables | 538 |

---

## Issue 3 — RC Debug Render Mode Radio Buttons Have No Effect

### Root Cause

The radio buttons write to `raymarchRenderMode` (`demo3d.h:705`). The value is passed to the shader via `glUniform1i(glGetUniformLocation(prog, "uRenderMode"), raymarchRenderMode)` in `raymarchPass()` (`demo3d.cpp:879`).

**The shader (`raymarch.frag`) has the correct branch logic (lines 235-276) but the pass itself may be conditional.** If `raymarchPass()` is gated behind a dirty flag or skipped when Cascade GI is disabled, changing the radio button does not trigger a re-render, so the viewport stays on the last-rendered result.

Additionally, `uRenderMode` is declared in the shader (`raymarch.frag:67`) but if the shader is cached from a previous compile that did not include the mode switch, the old binary will be used.

### Fix

1. **Make `raymarchPass()` unconditional per-frame.** Remove any early-return or dirty-check that prevents it from running every frame. The raymarcher is the primary output pass and must always execute.
2. **Force shader relink on reload:** when "Reload Shaders" is pressed, delete and recreate the program object so old binaries are not reused.
3. **Add a visible debug label:** When mode ≠ 0, overlay the mode name in the top-left corner of the 3D viewport using `ImGui::Text` so the user can confirm the mode is active.
4. **Verify `uRenderMode` naming:** grep for all `glUniform` calls in `raymarchPass()` to ensure `"uRenderMode"` matches the exact uniform name in the frag shader (currently `uRenderMode` on line 67 — these match, but worth double-checking after any rename).

### Files

| File | Function | Lines |
|------|----------|-------|
| `src/demo3d.cpp` | `raymarchPass()` | 846–910 |
| `src/demo3d.cpp` | `renderSettingsPanel()` (radio buttons) | 1646–1650 |
| `res/shaders/raymarch.frag` | mode branches | 235–276 |
| `src/demo3d.h` | `raymarchRenderMode` | 705 |

---

## Issue 4 — Show Performance Metrics Does Nothing

### Root Cause

Confirmed by code inspection: `showPerformanceMetrics` (`demo3d.h:717`) is toggled by the checkbox (`demo3d.cpp:1653`) but **there is no `if (showPerformanceMetrics)` display block anywhere in the codebase.** The metric variables (`voxelizationTimeMs`, `sdfTimeMs`, `cascadeTimeMs`, `raymarchTimeMs`, `activeVoxelCount`, `memoryUsageMB`) are declared and initialized to zero but never populated with real measurements, and never rendered.

### Fix

**Step A — Populate metrics with GPU timers.**  
Wrap each compute/render pass with OpenGL timer queries:
```cpp
// Before pass
GLuint timerQuery;
glGenQueries(1, &timerQuery);
glBeginQuery(GL_TIME_ELAPSED, timerQuery);

// ... pass ...

glEndQuery(GL_TIME_ELAPSED);
GLint available = 0;
while (!available) glGetQueryObjectiv(timerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
GLuint64 elapsed;
glGetQueryObjectui64v(timerQuery, GL_QUERY_RESULT, &elapsed);
voxelizationTimeMs = elapsed / 1e6;
glDeleteQueries(1, &timerQuery);
```
Do this for `voxelizePass()`, `sdfPass()`, `cascadePass()`, and `raymarchPass()`.

**Step B — Add display code after the checkbox.**  
In `renderSettingsPanel()` after line 1653:
```cpp
if (showPerformanceMetrics) {
    ImGui::Separator();
    ImGui::Text("--- Performance (ms/frame) ---");
    ImGui::Text("  Voxelize:  %.2f ms", voxelizationTimeMs);
    ImGui::Text("  SDF:       %.2f ms", sdfTimeMs);
    ImGui::Text("  Cascade:   %.2f ms", cascadeTimeMs);
    ImGui::Text("  Raymarch:  %.2f ms", raymarchTimeMs);
    ImGui::Text("  Frame:     %.2f ms", frameTimeMs);
    ImGui::Separator();
    ImGui::Text("  Voxels:    %d", activeVoxelCount);
    ImGui::Text("  Memory:    %.1f MB", memoryUsageMB);
}
```

**Step C — Populate `activeVoxelCount` and `memoryUsageMB`.**  
These should be calculated after `voxelizePass()` — `activeVoxelCount` from the atomic counter or voxel buffer element count, `memoryUsageMB` from the sum of allocated texture/buffer sizes.

### Files

| File | Function | Lines |
|------|----------|-------|
| `src/demo3d.cpp` | `renderSettingsPanel()` | 1653 (insert after) |
| `src/demo3d.cpp` | `voxelizePass()` | add timer |
| `src/demo3d.cpp` | `sdfPass()` | add timer |
| `src/demo3d.cpp` | `cascadePass()` | add timer |
| `src/demo3d.cpp` | `raymarchPass()` | add timer |
| `src/demo3d.h` | metric variables | 757–777 |

---

## Implementation Order

| Priority | Issue | Effort | Risk |
|----------|-------|--------|------|
| 1 | Issue 4 — Performance Metrics display | Low | None — pure UI add |
| 2 | Issue 3 — Render Mode not working | Low-Med | Need to verify pass scheduling |
| 3 | Issue 1 — Scene Selection cleanup | Low | UI-only refactor |
| 4 | Issue 2 — SDF Debug wrong | Med-High | Depends on SDF pass status |

Start with Issue 4 (highest confidence, purely additive) and Issue 3 (narrowly scoped). Issues 1 and 2 involve either understanding the placeholder state of the SDF system or taste decisions about UI layout, so they come after.
