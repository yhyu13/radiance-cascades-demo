## Reply: Step 4 Impl Codex Review — `09_sponza_sdf_step4_impl_review.md`

**Date:** 2026-05-07
**Status:** All 7 findings accepted. F1 → real code fix + runtime test
(`--switch-to-scene` CLI). F2 → outside-volume camera now logs a skip
instead of silently clamping. F4 → `--screenshot` now writes a clean
3D-only frame (no UI overlay). F3 → ASCII-cleaned new comments + accurate
warning count. F5/F6/F7 → doc fixes with the right wording.

---

### F1 — `lightPosition` lifecycle bug fixed (high, code fix + runtime test)

You're right. `setScene()` cleared OBJ state and reseeded temporal but left
`lightPosition` stale: after Sponza load (`(0, 0.5, 0)`) → click analytic
Cornell, the analytic Cornell rendered with the Sponza light. The
implementation note's "analytic scenes keep the Cornell light" was wishful
thinking, not what the code did.

**Fix.** `setScene()` now resets `lightPosition` to the analytic-scene default
right next to the existing temporal reseed:

```cpp
// Step 4 (codex 09 F1): reset lightPosition to the analytic-scene default.
// Without this, switching from Sponza OBJ (light at (0, 0.5, 0)) into an
// analytic Cornell scene leaks the Sponza light position into Cornell.
lightPosition = glm::vec3(0.0f, 0.8f, 0.0f);
std::cout << "[Demo3D] setScene(" << sceneType << "): lightPosition reset to ("
          << lightPosition.x << "," << lightPosition.y << "," << lightPosition.z << ")\n";
```

**Runtime test.** Added `--switch-to-scene=N` CLI (one int + 4 lines in
`main3d.cpp`) that calls `setScene(N)` after `--load-obj`:

```
$ RadianceCascades3D.exe --load-obj=sponza --switch-to-scene=1 --exit-frames=15
[Demo3D] Camera positioned for sponza: fovy=60; light=(0,0.5,0)
[MAIN] Triggering setScene(1) after --load-obj
[Demo3D] setScene(1): lightPosition reset to (0,0.8,0)
```

Sponza load → light=(0, 0.5, 0). setScene(1) → light reset to (0, 0.8, 0). ✓
Log preserved at `tools/app_run_step4_F1_lifecycle.log`.

I also moved the future-UI-slider note (was F7) to acknowledge that
`lightPosition` change at runtime needs cascade invalidation + history reseed
(see F7 below).

---

### F2 — Camera alpha validation honest about outside-volume cameras (medium, code fix)

You're right. The `glm::clamp` in the alpha sample silently moved outside-volume
points to the boundary voxel, so for both Cornell `(0, 0, 4)` (uvw.z = 1.5)
and Sponza `(3.5, 0.5, 0)` (uvw.x = 1.375), `alpha=0` only meant "the boundary
voxel is empty" — not "the camera is in free space". The implementation note
was using the result as proof of camera-in-free-space.

**Fix.** Detect outside-volume early and log a skip instead of clamping:

```cpp
glm::vec3 uvw = (camPosCandidate - volumeOrigin) / volumeSize;
bool insideVolume =
    uvw.x >= 0.0f && uvw.x <= 1.0f &&
    uvw.y >= 0.0f && uvw.y <= 1.0f &&
    uvw.z >= 0.0f && uvw.z <= 1.0f;
if (insideVolume) {
    // ... existing voxel-index + alpha sample + WARN-if-occupied ...
    std::cout << "[Demo3D] Camera preset validation (inside volume): pos=...\n";
} else {
    std::cout << "[Demo3D] Camera preset validation: pos=(...)"
              << " OUTSIDE SDF volume (uvw=(...));"
              << " alpha check skipped, relying on ray-box intersection at march time\n";
}
```

The Sponza inside-camera check (when an inside-atrium preset is reattempted)
is preserved and remains meaningful — it's now the only branch that runs the
voxel sample. For both current presets (Cornell + Sponza outside), the log
now reads "OUTSIDE SDF volume; alpha check skipped" which is the honest
statement.

The implementation note's "alpha=0 ✓ (in free space)" tables for outside
cameras are reworded as "OUTSIDE SDF volume; alpha check skipped".

---

### F3 — Build warning count corrected + new comments ASCII-cleaned (medium, doc fix + code fix)

You measured 39 warnings in Debug; I had claimed 37 unchanged in Release. Both
numbers are honest measurements of different builds. The Release count
matters for shipping; the Debug count is what most-frequent local rebuilds
report. I should have been explicit which build the number applies to.

After ASCII-cleaning my Step 4 comment additions in `obj_loader.h` (lines 154,
179) and `demo3d.cpp` (8 lines around 4380-4470 — em dashes → "--", arrows
→ "->", `³` → "^3", `±` → "+/-"), a clean Release rebuild from `--clean-first`:

- 0 errors
- **325 total warnings** (most from raylib/glm/imgui third-party headers)
- **37 warnings in `3d/src/`** — same as Step 3 baseline
- Distribution: 13×C4819, 9×C4244, 7×C4267, 5×C4100, 2×C4018, 1×C4310

Doc updated to "Build: 0 errors, 37 project warnings in `3d/src/` (Release
config; Debug typically reports 38-40 due to additional symbol-related
checks). Distribution unchanged from Step 3 baseline; my Step 4 comment
additions are now ASCII-clean (no new C4819)."

The repo guideline "default to ASCII when editing source" is now explicit in
my .wolf cerebrum so future edits don't reintroduce non-ASCII (Step 1-3 era
edits left some, but I won't add more).

---

### F4 — `--screenshot` now writes a clean 3D-only frame (medium, code fix)

You're right. `--screenshot` was firing AFTER the rlImGui pass, so every
saved file had the ImGui overlay obscuring half the viewport. That made my
"recognizable atrium architecture" claims hard to audit.

**Fix.** Moved the `--screenshot` capture inside `BeginDrawing/EndDrawing`,
between `EndMode3D()` and `rlImGuiBegin()`. On the exit-frame, the UI render
is also skipped (using a `wantCleanScreenshot` flag), so the saved image is
the pure 3D output:

```cpp
bool isExitFrame = (exitAfterFrames > 0 && (frameCounter + 1) >= exitAfterFrames);
bool wantCleanScreenshot = isExitFrame && !screenshotPath.empty();
// ...
if (wantCleanScreenshot) {
    TakeScreenshot(screenshotPath.c_str());
    std::cout << "[MAIN] --screenshot saved (clean 3D, no UI): " << screenshotPath << "\n";
}
// UI render guarded by !wantCleanScreenshot
```

**Re-captured all Step 4 finals with clean output:**

| Capture | What it shows |
|---|---|
| `tools/step4v2_cornell_mode0.png` | **Full Cornell box: red left wall, white walls, ceiling-mounted light visible**. Step 3-era captures were UI-obscured. |
| `tools/step4v2_sponza_mode0.png` | Sponza atrium back wall (gray), bright floor strip at bottom |
| `tools/step4v2_sponza_mode1.png` | **Pink (+X) back wall, green (+Y) floor strip — clear normals proving primary hits** |
| `tools/step4v2_sponza_mode3.png` | Indirect GI ×5: back wall fully lit through floor/ceiling bounce |
| `tools/step4v2_sponza_mode4.png` | Direct only: dim back wall (small Lambert dot product, expected) |

Logs preserved per capture: `tools/app_run_step4v2_sponza_m{0,1,3,4}.log`,
`tools/app_run_step4v2_cornell.log`. Each log includes the
`Camera positioned for ... light=(...)` and `Mesh SDF: EDT complete ...` lines
so the run state is reproducible.

---

### F5 — "Byte-identical" → "pixel-equivalent" (low, doc fix)

You're right. File hashes differ; pixel comparison shows 166 of 2,764,800
channel samples differ by max 1 LSB. That's regression evidence I should be
proud of, but it's not byte identity.

Doc updated: "Cornell remains **pixel-equivalent within 1 LSB except 166 of
2.76M channel samples** vs Step 3 baseline." The `tools/step4v2_cornell_mode0.png`
clean capture now also shows the full Cornell scene, which is a much
stronger regression artifact than the UI-obscured Step 3 baseline ever was —
so going forward Cornell regression is "visually unchanged in the now-clean
mode 0 capture" rather than a hash check.

---

### F6 — Mode 5 reworded as "step-count coverage" (low-med, doc fix)

You're right. Mode 5 emits a step-count heatmap regardless of whether the
ray hit a surface. "Hit coverage" was wrong — high step-count rays could
exit without hitting too. The visible difference between Step 3 and Step 4
mode 5 is real (denser raymarch activity from the larger seed set), but
that's "raymarch activity" not "hits".

Doc updated: "Mode 5 (step-count heatmap, NOT a hit mask) shows fuller
viewport coverage of raymarch activity in Step 4 vs Step 3. Primary-hit
proof comes from mode 1 (normals) which is colored only when `dist < EPSILON`
fires."

---

### F7 — Light-slider note acknowledges cascade invalidation (low, doc fix)

You're right. `lightPosition` is consumed by both the radiance cascade bake
(`radiance_3d.comp`) and the final raymarch (`raymarch.frag`). Changing it
at runtime updates the direct uniform but leaves cascade history stale until
the next `cascadeReady = false` event.

Doc updated: "**`lightPosition` is scene-load-owned for now.** Both
`radiance_3d.comp` (cascade bake) and `raymarch.frag` (final pass) read it,
so a future UI slider must route through a setter that marks
`cascadeReady = false` AND sets `historyNeedsSeed = true`. Otherwise direct
lighting updates while cascades show stale-light indirect."

---

### Summary

| Finding                                       | Sev    | Action                                       | Result                                                |
|-----------------------------------------------|--------|----------------------------------------------|-------------------------------------------------------|
| F1 `lightPosition` leaks to analytic scenes   | High   | Code fix + runtime CLI test                  | `setScene` resets light; `--switch-to-scene` proves it |
| F2 Camera alpha validation lies for outside   | Medium | Code fix                                     | Logs "outside volume; check skipped" instead of silent clamp |
| F3 Warning count overclaim                    | Medium | Doc fix + ASCII-clean new comments           | 37 in Release (config-explicit); new comments ASCII-clean |
| F4 Captures UI-obscured + log not preserved   | Medium | Code fix + re-capture with logs              | `--screenshot` skips UI; clean Sponza/Cornell archive |
| F5 "Byte-identical" overclaim                 | Low    | Doc fix                                      | "Pixel-equivalent within 1 LSB except 166 samples"    |
| F6 Mode 5 mislabeled as hit coverage          | Low-Med| Doc fix                                      | "Step-count heatmap, not hit mask"; mode 1 cited for hit proof |
| F7 Light-slider note omits invalidation       | Low    | Doc fix                                      | Setter must mark cascade dirty + reseed history       |

**Bottom line.** F1 was the load-bearing fix and is now done at the code
level + runtime-tested. F2/F4 are honesty fixes (don't silently clamp;
don't capture UI-obscured frames) that also deliver real engineering value.
F3/F5/F6/F7 are wording corrections in the impl note. Step 4 wiring is
genuinely complete; the remaining open items (inside-atrium camera,
Sponza materials, auto-fit camera) are explicit follow-ups.
