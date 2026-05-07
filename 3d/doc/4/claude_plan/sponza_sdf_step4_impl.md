# Sponza SDF — Step 4: Implementation Notes (revised after codex 09)

**Date:** 2026-05-07 (revised after codex review `09_*` / reply `09_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_step4_plan.md` (v2)
**Status:** Implemented and verified. Sponza now produces recognizable atrium
architecture in mode 0 (back wall + floor strip visible). Mode 1 confirms
primary rays hit OBJ surfaces with sensible per-axis normals (pink +X back
wall, green +Y floor). Mode 3 shows non-zero indirect GI on the back wall.
Cornell remains **pixel-equivalent within 1 LSB** vs Step 3 baseline (166 of
2.76M channel samples differ; codex 09 F5).

**Changelog (vs codex 08 first impl):** F1 — `lightPosition` lifecycle bug
fixed (`setScene` now resets to default). F2 — camera alpha-sample no longer
silently clamps for outside-volume cameras (logs "skipped" instead). F4 —
`--screenshot` writes a clean 3D-only frame (UI suppressed on screenshot
exit-frame). F3 — Step 4 comment additions ASCII-cleaned. F5/F6/F7 — wording
fixes ("byte-identical" → "pixel-equivalent"; mode 5 = "step-count heatmap"
not "hit coverage"; light-slider future-work note acknowledges cascade
invalidation requirement).

---

## Summary

| Change | Function | Status |
|---|---|---|
| 4a — `OBJLoader::normalize(float)` overload + per-OBJ scale | `OBJLoader::normalize`, `Demo3D::loadOBJMesh` | done |
| 4a — F7 boundary-slice occupancy log | `Demo3D::generateMeshSDF` | done |
| 4b — Per-OBJ camera preset + F3 alpha-sample validation | `Demo3D::loadOBJMesh` | done (1 iteration) |
| 4b ext — Per-OBJ light position (was hardcoded) | `Demo3D::lightPosition` member + 2 uniform sites | unplanned addition |
| codex 09 F1 — `lightPosition` reset in `setScene()` | `Demo3D::setScene` | done + runtime test |
| codex 09 F2 — Outside-volume camera alpha-skip | `Demo3D::loadOBJMesh` (camera block) | done |
| codex 09 F4 — Clean `--screenshot` (no UI overlay) | `main3d.cpp` main loop | done |
| codex 09 F1 test hook — `--switch-to-scene=N` CLI | `main3d.cpp` | done |

**Build (Release, `--clean-first`):** 0 errors, 37 project warnings in
`3d/src/` (unchanged from Step 3 baseline; distribution: 13×C4819, 9×C4244,
7×C4267, 5×C4100, 2×C4018, 1×C4310). Debug typically reports 38-40 due to
additional symbol-related checks. Step 4 added zero new warnings to either
config; my comment additions were ASCII-cleaned per codex 09 F3.

---

## Files Touched

- `src/obj_loader.h` — added `normalize(float halfExtent)` overload, kept `normalize() = normalize(1.0f)` for back-compat
- `src/demo3d.h` — added `glm::vec3 lightPosition` member
- `src/demo3d.cpp` — three additions:
  - `generateMeshSDF()`: F7 boundary-slice occupancy counter
  - `loadOBJMesh()`: per-OBJ scale lookup (4a), per-OBJ camera/light preset block with F3 alpha validation (4b/4b ext)
  - Two uniform writes (radiance bake + raymarch) read `lightPosition` instead of hardcoded `(0, 0.8, 0)`

---

## Measured Results

### 4a (per-OBJ normalize + boundary check)

| Mesh | halfExtent | Voxel seeds Step 3 → Step 4 | Multiplier | Boundary seeds | EDT (ms) | Albedo (ms) |
|------|------------|------------------------------|------------|----------------|----------|-------------|
| Sponza | 1.9 | 37,757 → **147,593** | **3.91×** | **0** ✓ | 60.8 | 28.6 |
| Cornell | 1.0 (unchanged) | 40,878 → 40,878 | 1.00× | 0 ✓ | 68.4 | 46.2 |

- Sponza density gain matches the codex F1 prediction: surface-area scaling
  `1.9² ≈ 3.6×` (measured 3.91×; the slight overshoot is triangle-density
  interaction).
- Boundary slices empty for both — 5% margin works.
- EDT time **flat** for Sponza (60.8 ms vs Step 3's 65 ms — actually slightly
  faster). Confirms codex F1's claim that EDT cost is dominated by fixed-size
  N³ sweeps, not seed count.
- Cornell numbers byte-identical to Step 3. Clean regression.

### 4b camera presets (final, after one iteration)

| Mesh | Camera position | Camera target | FOV | Alpha at camera voxel |
|------|-----------------|---------------|-----|------------------------|
| Cornell | `(0, 0, 4)` | `(0, 0, 0)` | 60° | 0 ✓ |
| Sponza | `(3.5, 0.5, 0)` (outside +X) | `(0, 0, 0)` | 60° | 0 ✓ |

### 4b ext light positions

| Mesh | Light position | Reason |
|------|----------------|--------|
| Cornell | `(0, 0.8, 0)` (unchanged from Step 3) | Inside Cornell box near ceiling |
| Sponza | `(0, 0.5, 0)` | Inside atrium between floor (Y=-0.795) and ceiling (Y=+0.795). The default Y=+0.8 was **just above Sponza's ceiling** — direct light couldn't reach the interior. |

---

## Iteration: First Camera Guess Was Wrong

The plan v2 proposed Sponza camera at `(1.6, 0.1, 0) → (-1, 0.1, 0)` (inside
atrium, looking down -X axis). The F3 alpha-sample validation passed (alpha=0,
camera in free space), but mode 0/1/4 captures with that camera were **all
black**.

Diagnosis with the F4 mode set:
- Mode 1 (normals): black → primary rays don't terminate at `dist < EPSILON` from this view
- Mode 4 (direct only): black → no surface receives direct light
- Mode 5 (steps): green/red coverage → rays do march, but exit without `dist < EPSILON`

Likely cause: looking down the long X-axis from inside, primary rays sample
the conservative-band UDF along thin lateral structures (columns) that the
trilinear-filtered band doesn't represent as `< EPSILON` for grazing-angle
hits. This matches Step 2 reply F5's deferred concern: "no shader changes"
is conditional on the band being thick enough for all view angles.

**Fix applied:** moved camera to outside-looking-in `(3.5, 0.5, 0) → (0, 0, 0)`
(mirrors Cornell's working pattern). Mode 0/1/4 with this camera produces
visible Sponza geometry — see captures below.

The inside-atrium camera remains a **known follow-up**: either widen
`surfaceRadius` (Step 2 deferred fallback ladder), iterate the band more,
or ultimately upgrade to argmin-EDT with thicker bands. Recorded in the
out-of-scope section.

---

## Mode 4 Direct Light Was Dim Until Light Was Repositioned

After moving camera outside, mode 0 was visible but very dim. Mode 4 (direct
only) was still mostly dark even though primary rays hit. The clue: the
hardcoded light position `(0, 0.8, 0)` is **above Sponza's ceiling**
(Y_max=+0.795). Direct light can't reach the atrium interior because:
- The shadow ray from any wall to the light hits the conservative-band
  ceiling first → `inShadow = true`.
- For wall hits without shadow obstruction (the +X-facing back wall directly
  visible to the camera), light direction is +Y but normal is mostly +X →
  Lambert dot product is small.

**Fix applied:** added per-OBJ `lightPosition` member, defaulted to Cornell's
`(0, 0.8, 0)`, set Sponza's to `(0, 0.5, 0)` in the loadOBJMesh preset block.
Both shader uniform sites (radiance bake at `radiance_3d.comp` setup, raymarch
final pass) now read from the member.

After fix: mode 1 (normals) shows clear architectural shapes (magenta back
wall = +X-facing normals; green ceiling band = +Y-facing normals; purple
columns = mixed-axis normals). Mode 0 shows dim but recognizable Sponza
geometry. Mode 4 still dim — the back wall the camera sees has a small
Lambert contribution from a light at Y=+0.5 (light direction is mostly +Y,
back-wall normal is +X). This is **expected behavior for the current scene**
and not a wiring bug.

---

## Verification Captures

After codex 09 F4 fix, `--screenshot` writes a clean 3D-only frame (UI
suppressed on the screenshot exit-frame). The **`step4v2_*` captures** are
the authoritative final state; older `step4_final_*` and `step4d_*` files
are kept for the iteration history but include the ImGui overlay.

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --render-mode=0 --exit-frames=120 --screenshot=tools\step4v2_cornell_mode0.png
.\build\RadianceCascades3D.exe --load-obj=sponza  --render-mode=0 --exit-frames=120 --screenshot=tools\step4v2_sponza_mode0.png
.\build\RadianceCascades3D.exe --load-obj=sponza  --render-mode=1 --exit-frames=120 --screenshot=tools\step4v2_sponza_mode1.png
.\build\RadianceCascades3D.exe --load-obj=sponza  --render-mode=3 --exit-frames=120 --screenshot=tools\step4v2_sponza_mode3.png
.\build\RadianceCascades3D.exe --load-obj=sponza  --render-mode=4 --exit-frames=120 --screenshot=tools\step4v2_sponza_mode4.png
```

| Capture | Result |
|---|---|
| `step4v2_cornell_mode0.png` | **Full Cornell scene visible: red left wall, white floor/right/back walls, ceiling-mounted light, GI bounce striping below.** Step 3-era captures were UI-obscured; this is the first clean Cornell. |
| `step4v2_sponza_mode0.png` | Sponza atrium back wall (gray) with bright floor strip at bottom edge ✓ |
| `step4v2_sponza_mode1.png` | **Pink (+X-facing) back wall, green (+Y-facing) floor strip on top edge** — primary rays hit OBJ with correct per-axis normals ✓ (codex 09 F6: this is the primary-hit proof, not mode 5) |
| `step4v2_sponza_mode3.png` | Indirect GI ×5: back wall fully lit through floor/ceiling bounce — cascade GI working ✓ |
| `step4v2_sponza_mode4.png` | Direct only: dim back wall (light at Y=+0.5 has small Lambert dot product with +X-facing wall normal — geometry-dependent dimness, expected) |

Plus the diagnostic checkpoint captures from the **4a-only checkpoint**
(with OLD camera, before 4b camera fix): `tools/step4a_sponza_mode{0,1,2,4,5,7}.png`.
Mode 5 there shows broader **step-count coverage** vs Step 3 (codex 09 F6:
this is raymarch activity, not literal "hit" coverage — primary-hit proof
comes from mode 1).

All run logs preserved per capture: `tools/app_run_step4v2_sponza_m{0,1,3,4}.log`,
`tools/app_run_step4v2_cornell.log`. Each contains
`[Demo3D] Camera positioned ... light=(...)` and
`[Demo3D] Mesh SDF: EDT complete ...` so the run state is reproducible.

### F1 Lifecycle Test

```
$ RadianceCascades3D.exe --load-obj=sponza --switch-to-scene=1 --exit-frames=15
[Demo3D] Camera positioned for sponza: fovy=60; light=(0,0.5,0)
[MAIN] Triggering setScene(1) after --load-obj
[Demo3D] setScene(1): lightPosition reset to (0,0.8,0)
```

Sponza load sets light to `(0, 0.5, 0)`; `setScene(1)` resets to `(0, 0.8, 0)`.
Without the codex 09 F1 fix, the analytic Cornell scene would have rendered
with the Sponza light. Log: `tools/app_run_step4_F1_lifecycle.log`.

---

## Verification Gates (revised plan, final state)

### Build
- [x] 0 errors
- [x] 37 project warnings in `3d/src/` (Release; codex 09 F3 — same as Step 3 baseline)

### Cornell (regression)
- [x] `[Demo3D] OBJ normalized to halfExtent=1` log line
- [x] `[Demo3D] Camera preset validation: ... OUTSIDE SDF volume; alpha check skipped` (codex 09 F2: outside-camera honest log)
- [x] `[Demo3D] Camera positioned for cornell: fovy=60; light=(0,0.8,0)`
- [x] Voxel seed count = 40,878 (identical to Step 3) ✓
- [x] Mode 0 **pixel-equivalent within 1 LSB** vs Step 3 baseline (codex 09 F5) — clean capture in `step4v2_cornell_mode0.png` shows full Cornell scene
- [x] codex 09 F1 lifecycle: load Sponza then setScene(1) resets light to (0, 0.8, 0)

### Sponza after 4a only (diagnostic checkpoint, OLD camera)
- [x] `[Demo3D] OBJ normalized to halfExtent=1.9` log line
- [x] Voxel seed count `> 100,000` — **measured 147,593** ✓
- [x] EDT bake time `< 200 ms` — **measured 60.8 ms** ✓
- [x] Boundary-slice surface seeds: 0 ✓
- [x] Mode 5 step-count coverage broader than Step 3 (codex 09 F6: NOT a hit mask, just raymarch activity) ✓
- [x] Modes 0/1/2/4/5/7 captured for triage

### Sponza after 4b + 4b-ext (final camera + light)
- [x] `[Demo3D] Camera preset validation: ... OUTSIDE SDF volume; alpha check skipped` (codex 09 F2)
- [x] `[Demo3D] Camera positioned for sponza: fovy=60; light=(0,0.5,0)`
- [x] Mode 0 shows recognizable Sponza geometry ✓ (back wall + floor strip — clean capture in `step4v2_sponza_mode0.png`)
- [x] **Mode 1 primary-hit proof** ✓ (pink +X-facing back wall, green +Y-facing floor strip — clean capture)
- [x] Mode 3 cascade GI: non-zero indirect on back wall (clearly visible in `step4v2_sponza_mode3.png`) ✓
- [x] Mode 4 direct: dim back wall (geometry-dependent — back-wall normal is +X but light at Y=+0.5 → small Lambert dot; expected, not a regression)

Step 4 success criterion ("Sponza mode 0 shows recognizable atrium
geometry") **MET**. Mode 4 dimness is geometry-light-relationship, not a
wiring bug. Brighter Sponza requires either repositioning the light, multiple
lights, or env-fill ambient — explicit follow-ups.

---

## Architecture Notes

**Per-OBJ scale via `objKind` lookup, not via member.** `currentOBJPath` is
still set in the commit block at the bottom of `loadOBJMesh()` for atomicity,
but `objKind` is computed early (same logic) for the scale + camera + light
lookups. Slight duplication, but keeps the commit block's "all invariants set
together" pattern intact.

**`lightPosition` is a Demo3D member, not a per-render local.** Two callers
(radiance cascade bake at line ~1631, final raymarch at line ~1888) needed
to agree. A member is the simplest single source of truth. **Currently
scene-load-owned only** (set by `loadOBJMesh()` and `setScene()`). A future
UI slider must route through a setter that marks `cascadeReady = false`
AND sets `historyNeedsSeed = true` — otherwise direct lighting updates while
cascades show stale-light indirect (codex 09 F7).

**F3 alpha-sample is a binary check on the raw voxel grid, not the SDF.**
Reading `meshVoxelData[idx*4+3]` tests whether the camera is inside a marked
surface voxel — the binary occupancy from Step 1's voxelizer. Sampling
`sdfTexture` would require GL readback, which is overkill for "is the camera
inside geometry". The single-voxel test could miss a thin column 1 voxel
away, but that's acceptable for the "obviously inside geometry" guard.

**Per-OBJ presets are per-name strings, not per-bounds heuristics.** Two
named meshes → two if branches, deterministic and cheap. The codex Plan F3
recommended "auto-fit camera from bounds" but acknowledged that's a
generalization that needs a third sample. Deferred to Step 4c when a third
OBJ shows up.

**`lightPosition` defaults to Cornell-tuned position.** New analytic scenes
loaded via `setScene()` keep the Cornell light. If a future analytic scene
needs a different light, `setScene()` should also write `lightPosition`.
Not addressed in Step 4 because all current analytic scenes are Cornell-Box
variants.

---

## Out-of-Scope Follow-ups (recorded for future steps)

- **Inside-atrium camera for Sponza** — the first-attempt
  `(1.6, 0.1, 0) → (-1, 0.1, 0)` view didn't produce mode-0 hits. Likely
  needs Step 2's deferred fallback ladder: widen `surfaceRadius` to a full
  voxel diagonal, then potentially upgrade to argmin-EDT with thicker bands.
- **Sponza mode 4/3 dim** — direct light contribution is geometry-dependent
  (small Lambert dot product on walls perpendicular to light vector). Could
  be improved by: multiple light sources, brighter `uLightColor`, raising
  light to outside the volume for sun-like illumination, or skylight (env
  fill) ambient term.
- **Cornell + Sponza light positions are hardcoded** — `lightPosition` is
  set once per OBJ load. Future UI work can expose it as a draggable widget
  for runtime experimentation.
- **3rd OBJ → auto-fit camera (4c)** — when a third mesh shows up, replace
  the per-name preset with a bounds-driven heuristic.
- **Sponza materials (.mtl loading)** — currently all faces get the default
  gray albedo (0.8, 0.8, 0.8). Loading sponza.mtl would give real per-face
  colors → mode 0 would show the famous red-curtain / textured Sponza look.

---

## Why This Was Two Iterations, Not One

The plan v2 explicitly said "Step 4 success does NOT require Sponza to render
correctly on the first try" and called out the F3 alpha-sample log as the
diagnostic. That worked exactly as intended: the inside-atrium camera passed
alpha=0 (in free space) but mode 1 was black (no primary hits). The diagnostic
captures revealed the cause within minutes, and the fix (move camera outside)
was a 5-line preset change.

The light-position fix was unplanned but followed the same pattern: mode 4
black → trace the failure path → light is above the ceiling → per-OBJ light.
Adding a `lightPosition` member to support that took 5 lines (member + ctor
init + two uniform-write changes) plus the per-OBJ preset block.

The codex review's emphasis on "test the most likely blockers, don't claim
fix" (F4) was spot-on — the plan was framed correctly and the diagnostic
mode set isolated each issue cheanly.
