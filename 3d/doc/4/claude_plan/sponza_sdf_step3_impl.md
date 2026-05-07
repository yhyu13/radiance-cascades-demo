# Sponza SDF — Step 3 v2: Implementation Notes (revised)

**Date:** 2026-05-07 (revised after codex review `07_*` / reply `07_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_step3_plan.md` (v2)
**Status:** Step 3 wiring **verified for both Cornell and Sponza** at the
log/SDF level. Cornell **renders correctly** in mode 0 (primary raymarch) and
mode 3 (cascade GI). Sponza visual rendering in mode 0 is **dark and remains
unresolved** — mode 5 confirms a non-trivial SDF, so the failure is not in the
Step 3 wiring; mode 0 visibility belongs to Step 4 (camera/scene-bounds).

**Changelog (vs original):** F1 — `sdfGenerationPass()` now returns `bool`
and the render loop honors it (failure → retry next frame, no stale-state
lock); runtime test via `--inject-bake-failures=N`. F2 — added Sponza mode
5/2 captures, restated visibility status. F3 — added Cornell mode 3 capture
to prove cascade-hit path. F4 — actual warning count cited. F5 — interactive
gates downgraded from "guaranteed" to "static wiring present". F6 — line
numbers refreshed (also switched to function-name citations going forward).

---

## Summary

Cite by function name; line numbers are point-in-time.

| Change | Function | File |
|---|---|---|
| 3a — `loadOBJMesh()` stage-and-commit + scene-switch invariants | `Demo3D::loadOBJMesh` | `src/demo3d.cpp` |
| 3b — `sdfGenerationPass()` mesh branch + `bool` return + failure honor | `Demo3D::sdfGenerationPass` | `src/demo3d.cpp` |
| 3b' (codex 07 F1) — render-loop honors `sdfGenerationPass()` return | `Demo3D::render` (SDF section) | `src/demo3d.cpp` |
| 3c — `setScene()` clears mesh data + reseeds temporal | `Demo3D::setScene` | `src/demo3d.cpp` |
| 3d — UI gate on `useAnalyticRaymarch` checkbox | `Demo3D::renderSettingsPanel` (analytic checkbox block) | `src/demo3d.cpp` |
| Test scaffold — `--screenshot=PATH`, `--render-mode=N`, `--inject-bake-failures=N` | CLI parsing + main loop | `src/main3d.cpp` |
| Test hook — `Demo3D::injectBakeFailures` member + setter | `Demo3D` class | `src/demo3d.h` |

**Build:** 0 errors, 37 warnings in `3d/src/` (baseline unchanged from Step 2).
Distribution: 13 × C4819 (UTF-8 source files in CP-936 environment), 9 × C4244
(int→float), 7 × C4267 (size_t→int), 5 × C4100 (unused parameters), 2 × C4018
(signed/unsigned), 1 × C4310 (cast truncation). My Step 3 changes contributed
zero new warnings to this distribution.

**Runtime baseline note:** every run log still reports the pre-existing
`res/shaders/sdf_3d.comp` GLSL compile failure (`imageLoad(struct
image3D_bindless, ivec3)` overload mismatch). That shader is unused by the
CPU EDT path. It's documented in `.wolf/cerebrum.md` as broken/unrelated, not
a Step 3 regression.

---

## Change 3a — `loadOBJMesh` stage-and-commit

```cpp
// Stage in a local vector — previous mesh state untouched until commit.
std::vector<uint8_t> newVoxelData;
objLoader.voxelize(volumeResolution, newVoxelData, volumeOrigin, volumeSize);

if (newVoxelData.empty()) {
    std::cerr << "[ERROR] Empty voxelization for " << successfulPath
              << "; keeping previous mesh state\n";
    return false;
}

// Debug-display upload uses new data directly (only side effect before commit).
glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
glTexSubImage3D(...newVoxelData.data());
glBindTexture(GL_TEXTURE_3D, 0);

// Commit — all invariants set together.
meshVoxelData        = std::move(newVoxelData);
meshSDFReady         = false;          // sdfGenerationPass will rebake
useOBJMesh           = true;
useAnalyticRaymarch  = false;          // 3d (F3): force grid raymarch
historyNeedsSeed     = true;           // 3a (F2): temporal reseed
renderFrameIndex     = 0;
temporalRebuildCount = 0;
sceneDirty           = true;
currentOBJPath       = ...;
```

Failure preserves prior scene. Verified by renaming `res/scene/sponza.obj`:
clean error, analytic Cornell stays up, no torn invariants.

---

## Change 3b — `sdfGenerationPass` mesh branch + `bool` return (codex 07 F1)

```cpp
bool Demo3D::sdfGenerationPass() {
    // Step 3 (3b, plan F1): OBJ mesh branch
    if (useOBJMesh && !meshVoxelData.empty()) {
        if (!meshSDFReady) {
            if (!generateMeshSDF()) {
                std::cerr << "[ERROR] sdfGenerationPass: mesh SDF bake failed; "
                             "render loop keeps sdfReady=false and retries next frame\n";
                return false;   // codex 07 F1
            }
            meshSDFReady = true;
        }
        return true;
    }

    if (analyticSDFEnabled) {
        // ... existing analytic compute dispatch ...
        if (it == shaders.end()) return false;   // shader-not-found is also a real failure
        // ... dispatch ...
    } else {
        // ... clear texture ...
    }
    return true;
}
```

The render loop is the only caller and now honors the return:

```cpp
// Demo3D::render() — SDF section
if (!sdfReady) {
    bool ok = sdfGenerationPass();
    sdfTimeMs = (GetTime() - t0) * 1000.0;
    if (ok) {
        sdfReady     = true;
        cascadeReady = false;   // SDF changed → cascade stale
    }
    // Failure: sdfReady stays false → next frame retries the bake.
    // cascadeReady is NOT invalidated — keeps prior frame's lighting on screen.
}
```

**Why this matters.** Without this, a `generateMeshSDF()` failure (size
mismatch, zero seeds, GL upload error mid-bake) would leave `sdfReady = true`
with `useOBJMesh = true` and `useAnalyticRaymarch = false` — the renderer
would believe it had a fresh mesh SDF when it didn't, with no path back to
analytic. The `bool` propagation makes the failure observable to the loop and
the loop's response is "try again next frame".

---

## Change 3c — `setScene` clears mesh + reseeds temporal

```cpp
currentScene = sceneType;
sceneDirty = true;
useOBJMesh = false;
currentOBJPath.clear();

// 3c: explicit clear (no implied cache); reclaim ~8 MB.
meshVoxelData.clear();
meshVoxelData.shrink_to_fit();
meshSDFReady = false;

// 3c (plan F2): scene-switch temporal reseed.
historyNeedsSeed     = true;
renderFrameIndex     = 0;
temporalRebuildCount = 0;
```

Symmetric with 3a's invariant block — both directions (OBJ→analytic and
analytic→OBJ) carry the same temporal-reseed.

---

## Change 3d — UI gate on Analytic SDF checkbox

```cpp
ImGui::BeginDisabled(useOBJMesh);
ImGui::Checkbox("Analytic SDF (smooth, no grid)", &useAnalyticRaymarch);
if (useOBJMesh) {
    ImGui::SameLine();
    ImGui::TextDisabled("(OBJ mode — uses grid SDF)");
}
ImGui::EndDisabled();
```

Tooltip updated to mention the OBJ-mode disable. Re-enables on switch back to
analytic via `setScene()`.

---

## Headless Verification

### Test 1: clean OBJ load — log-level wiring

```powershell
.\build\RadianceCascades3D.exe --load-obj=sponza  --exit-frames=120 --screenshot=tools\step3_sponza_mode0.png
.\build\RadianceCascades3D.exe --load-obj=cornell --exit-frames=120 --screenshot=tools\step3_cornell_mode0.png
```

Each log shows:

```
[OBJLoader] Voxelize complete: NNNN voxels filled
[Demo3D] OBJ committed (sponza|cornell); SDF will be baked next frame
[Demo3D] Mesh SDF: EDT complete N=128 voxelSz=0.03125m surfaceRadius=0.027063m seeds=NNNNN edt=Xms albedo=Yms
[MAIN] --exit-frames reached (120), quitting.
```

**Wiring success criterion (vs Step 2):** the post-load
`[Demo3D] Generating analytic SDF...` line is **absent** in both logs.
Step 2's logs always had it; Step 3b's mesh branch correctly bypasses the
analytic dispatch.

### Test 2: load-failure path — renamed `sponza.obj`

```
[Demo3D] Loading OBJ mesh: res/scene/sponza.obj
[ERROR] Failed to load OBJ from any location!
[ERROR] Tried paths: ...
[MAIN] --load-obj failed for res/scene/sponza.obj
[Demo3D] Analytic SDF generation complete.    ← previous scene continues normally
[MAIN] --exit-frames reached (30), quitting.
```

`useOBJMesh` stayed false (loadOBJMesh never reached commit), so the analytic
path runs as expected. No crash, no torn state.
Log: `tools/app_run_step3_failpath.log`.

### Test 3 (codex 07 F1): bake-failure retry path — `--inject-bake-failures=3`

```
[MAIN] --inject-bake-failures=3 (codex 07 F1: forces N synthetic generateMeshSDF failures)
[Demo3D] OBJ committed (cornell); SDF will be baked next frame
[INJECT] generateMeshSDF: synthetic failure (2 remaining)
[ERROR] sdfGenerationPass: mesh SDF bake failed; render loop keeps sdfReady=false and retries next frame
[INJECT] generateMeshSDF: synthetic failure (1 remaining)
[ERROR] sdfGenerationPass: mesh SDF bake failed; render loop keeps sdfReady=false and retries next frame
[INJECT] generateMeshSDF: synthetic failure (0 remaining)
[ERROR] sdfGenerationPass: mesh SDF bake failed; render loop keeps sdfReady=false and retries next frame
[Demo3D] Mesh SDF: EDT complete N=128 ... seeds=40878 edt=66.773ms albedo=29.908ms
[MAIN] --exit-frames reached (15), quitting.
```

3 forced failures → 3 retry attempts → 4th call succeeds and bake completes.
Without F1's `bool` return + render-loop honor, the first failure would have
locked `sdfReady = true` and the next 14 frames would have idled with stale
SDF state. Log: `tools/app_run_step3_f1_inject.log`.

### Test 4: visual mode-0/3/5 (screenshots)

Captures via `--render-mode=N --screenshot=PATH`:

| Capture | What it shows | Result |
|---|---|---|
| `tools/step3_cornell_mode0.png` | Final raymarch primary | **Cornell back wall + side wall silhouettes visible** ✓ — primary `dist < EPSILON = 1e-6` hits OBJ surfaces |
| `tools/step3_cornell_mode3.png` | Indirect GI ×5 (cascade output) | **Cornell walls visible in mode 3** ✓ — cascade `dist < 0.002` also hits OBJ surfaces |
| `tools/step3_sponza_mode0.png` | Final raymarch primary | **Dark** — visibility unresolved (see Step 4 prerequisites below) |
| `tools/step3_sponza_mode2.png` | Depth (near=white) | **Dark** — primary rays mostly miss visible surfaces from default camera |
| `tools/step3_sponza_mode5.png` | SDF step-count heatmap | **Green band visible in upper viewport** ✓ — SDF is non-trivial; bake produced a usable field |

The Cornell mode 3 capture is the F3 evidence: primary raymarch could in
principle render walls via direct lighting alone, but indirect GI requires
`radiance_3d.comp` to actually hit surfaces with its `< 0.002` threshold. The
mode 3 walls confirm both shader thresholds work for OBJ mode without code
changes — Step 2's "no shader changes" hypothesis is **confirmed for Cornell**
at both layers.

The Sponza mode 5 capture is the F2 evidence: a green hit-band in the top of
the viewport means the EDT bake is non-trivial and primary rays do reach the
band somewhere in the field. Mode 0/2 darkness is therefore not "the SDF is
broken", it's "the camera doesn't show much of the lit Sponza geometry from
the default position".

---

## Verification Gates: Status

Per-OBJ split of the Step 3 plan checklist:

### Cornell

| Gate | Status |
|---|---|
| `OBJ committed → Mesh SDF: EDT complete` log sequence | ✅ |
| Mode 0: surfaces shaded | ✅ (`step3_cornell_mode0.png`) |
| Mode 3: cascade GI hits surfaces | ✅ (`step3_cornell_mode3.png`) |
| Analytic SDF dispatch absent post-load | ✅ |

### Sponza

| Gate | Status |
|---|---|
| `OBJ committed → Mesh SDF: EDT complete` log sequence | ✅ |
| Analytic SDF dispatch absent post-load | ✅ |
| Mode 5: non-trivial SDF (some primary rays terminate) | ✅ (`step3_sponza_mode5.png`) |
| Mode 0/2: surfaces visible | ❌ visibility unresolved — see Step 4 prerequisites |
| Mode 3 (cascade GI): not yet captured for Sponza | ⏭️ pending Sponza visibility fix |

### Cross-cutting

| Gate | Status |
|---|---|
| Build clean, baseline warning count | ✅ 37 project warnings, unchanged from Step 2 |
| `--inject-bake-failures=N` retry path | ✅ (codex 07 F1) |
| Load-failure path preserves prior scene | ✅ |
| Switch back to Cornell analytic; checkbox re-enables | ⏭️ static wiring present, runtime smoke-test pending |
| Temporal: Cornell ↔ Sponza twice, no ghost lighting | ⏭️ static wiring present (`historyNeedsSeed` reseed in both paths), runtime smoke-test pending |
| UI gate hover/tooltip behavior | ⏭️ static wiring present, one mouse-hover pass needed |
| GI regression: cascades update after switch | ⏭️ requires runtime smoke-test |

The interactive gates have static wiring in place but require ImGui-driven
verification — not "guaranteed by code review" as I previously claimed. The
EMA blend mechanics involve jitter, stagger, fused alpha, and history handle
swaps that the static reset paths set up but don't single-handedly verify.

---

## Step 4 Prerequisites (Sponza visibility)

The Sponza mode-0 darkness is not a Step 3 wiring failure (mode 5 proves the
SDF is non-trivial; analytic isn't being drawn either, so it's not a stale
overwrite). Likely causes, in priority order:

1. **Camera position outside Sponza's interior** — default camera fits Cornell
   Box well; Sponza is an atrium that wants the camera *inside* with a wider
   FOV. A scene-bounds-fit camera reset on `loadOBJMesh()` would help.
2. **Volume bounds mismatch** — `objLoader.normalize()` puts Sponza in
   `[-1, 1]³`, while `volumeOrigin/volumeSize` covers `[-2, 2]³` (4m cube).
   The outer 7/8 of the volume is empty for Sponza but only 1/2 for Cornell.
   This is consistent with mode 5 showing hits only in part of the screen.
3. **Voxelization sparseness** — 37,757 seeds for Sponza vs 40,878 for
   Cornell. Sponza is a much larger surface area that's getting under-sampled
   at 128³, so thin walls leave gaps the conservative band can't bridge.

Cause 1 is the cheapest experiment (scene-bounds-fit camera). Cause 2 is the
correctness fix (rescale volume to mesh bounds, or rescale mesh to volume).
Cause 3 needs a higher voxel resolution or thickness-aware voxelization.

These are Step 4 (visual quality) tasks, explicitly out of Step 3 scope.

---

## Architecture Notes

**Stage-and-commit pattern.** Mirrors the Step 1 file-open / clear-after-success
fix. Local `newVoxelData` is voxelized first; only after success is it `std::move`d
into `meshVoxelData`. Failure preserves previous state with no torn invariants
(no half-set `useOBJMesh = true`, no half-cleared `meshSDFReady`).

**`meshSDFReady` write-once.** Only `sdfGenerationPass()`'s mesh branch sets it
true (after `generateMeshSDF()` returns true). Three places set it false:
`loadOBJMesh()` commit (rebake required), `setScene()` (analytic discard),
`generateMeshSDF()` failure (left as caller's responsibility — branch logs and
returns without flipping).

**`sdfReady` is now bake-success-gated (codex 07 F1).** Before this fix the
render loop set `sdfReady = true` after `sdfGenerationPass()` regardless of
outcome. After: only on `true` return. A failed bake leaves `sdfReady = false`,
which makes the next frame re-enter `sdfGenerationPass()` and re-attempt the
bake. `cascadeReady` is also held untouched on bake failure so the previous
frame's lighting stays on screen instead of being invalidated. Repeated failures
spam `[ERROR]` lines once per frame; if that becomes a UX issue a backoff or
fall-back-to-analytic policy can land in a follow-up. The current behavior
matches "user clicked OBJ; keep trying".

**UI gate via `BeginDisabled` not by hiding.** The checkbox stays visible but
greyed; users see why it's unavailable rather than wonder where the option went.

**Runtime test hooks.** `Demo3D::injectBakeFailures` is a single int member
+ setter. The check at the top of `generateMeshSDF()` is 6 lines and only
triggers when the int is non-zero. CLI exposes it via
`--inject-bake-failures=N`. Cost: trivial; benefit: F1's render-loop retry
path can be exercised at runtime, not just inferred from code review.
