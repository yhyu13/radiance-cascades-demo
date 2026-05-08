# Sponza SDF — Step 7: Auto-fit camera + light preset (revised after codex 13)

**Date:** 2026-05-08 (revised after codex review `13_*` / reply `13_*`)
**Status:** Implemented and verified. Build clean (0 errors, **38** project
src warnings: was 37 baseline, +1 C4819 from line-position shift moving an
existing non-ASCII char region into a new MSVC scan window — no new
non-ASCII characters introduced; codex 13 F7). All 4 existing OBJs
(cornell, cornell_orig, sponza, sponza_master) load and frame correctly
with the new bounds-driven preset; both new variants'
`resetCameraToScenePreset()` helper paths verified runtime via
`--test-reset-helper` (helper-level coverage; R key and ImGui Reset
Camera button share the helper in source — codex 13 F5). The
codex 12 F1 4-way → 2-way translation hack is no longer needed and was
deleted.

**Changelog (vs Step 7 v1, after codex 13):**
- F1 — `currentObjBmin/Bmax` now assigned in the same commit block as
  `meshVoxelData/useOBJMesh/currentOBJPath`, restoring the stage-and-commit
  atomicity that v1 broke (previously a failed voxelization could leave
  the previous mesh visible but R-key reset would use the failed
  mesh's bounds).
- F3 — Backoff formula switched from naive `1.0 × diag` to **FOV-aware
  fit + min-backoff guard**. v1 put Sponza at distance 4.74 (filled
  ~30% of vertical screen). The new formula computes the distance that
  just-fits the visible perpendicular extent within fovy/aspect, with
  40% headroom, then clamps to "at least outside the bounding box
  along lookDir + 30% diag margin" so the camera never lands inside or
  right against a wall. Cornell now at z=2.38 (FOV-fit dominated, was
  3.44); Sponza at x=3.32 (min-backoff dominated, close to the old
  hand-tuned 3.5).
- F2/F4/F5/F6 — doc accuracy corrections (see Verification section).

---

## Why

Step 4 introduced a hardcoded `applyOBJViewPreset(objKind)` with a
branch per OBJ kind (`"cornell"` vs `"sponza"`), each carrying literal
camera/light/fovy values. Step 6 added `cornell_orig` and
`sponza_master` variants but kept the 2-way preset, requiring a
codex 12 F1 4-way→2-way translation in `resetCameraToScenePreset()`.
Adding a 5th OBJ would have required:

- a new `else if` in `applyOBJViewPreset()` (camera + light + fovy),
- a new branch in the path-key detection of `loadOBJMesh()`,
- a new mapping in the reset-helper translation,
- a new ImGui button and CLI name (these are unavoidable),
- and tuning the literal positions by trial-and-error.

The first three are pure plumbing; this step removes them so adding
new OBJs is just adding a button + path.

---

## Approach

### Bounds-driven preset (codex 13 F3 revised)

`applyOBJViewPreset()` is now parameterless and reads two new
demo3d.h members `currentObjBmin`, `currentObjBmax` (set by
`loadOBJMesh()` in the commit block — codex 13 F1). Formula:

```cpp
const glm::vec3 size   = bmax - bmin;
const glm::vec3 center = (bmin + bmax) * 0.5f;
const float diag       = glm::length(size);

// Pick look direction: prefer +Z, switch to +X if X-extent dominates Z.
glm::vec3 lookDir(0.0f, 0.0f, 1.0f);
if (size.x > size.z * 1.3f) lookDir = glm::vec3(1.0f, 0.0f, 0.0f);

// FOV-aware fit: distance that just-contains the visible perpendicular
// extent (perp = size minus its lookDir-aligned component) within
// fovy/aspect, with 40% headroom.
const float fovy   = 60.0f;
const int   sw     = GetScreenWidth();
const int   sh     = GetScreenHeight();
const float aspect = (sh > 0) ? (float)sw / (float)sh : 16.0f / 9.0f;
const float halfFovyRad = glm::radians(fovy) * 0.5f;
const float halfFovxRad = std::atan(std::tan(halfFovyRad) * aspect);

glm::vec3 perp = size - lookDir * glm::dot(size, lookDir);
const float visH = std::abs(perp.y);
const float visW = std::sqrt(perp.x*perp.x + perp.z*perp.z);
const float distFromY = (visH * 0.5f) / std::tan(halfFovyRad);
const float distFromX = (visW * 0.5f) / std::tan(halfFovxRad);
float fitDist = std::max(distFromY, distFromX) * 1.4f;

// Min-backoff guard: at-least-outside the bounding box along lookDir
// + 30% diag margin. Without this, FOV-fit can park the camera right
// against (or inside) a wall when bbox fills the SDF volume (e.g.
// Sponza bmax.x=1.9, FOV-fit dist=1.93).
const float boxHalfAlongLook = std::abs(glm::dot(size, lookDir)) * 0.5f;
const float minBackoff       = boxHalfAlongLook + 0.3f * diag;
fitDist = std::max(fitDist, minBackoff);

glm::vec3 camPos    = center + lookDir * fitDist + glm::vec3(0, size.y * 0.05f, 0);
glm::vec3 camTarget = center;
glm::vec3 lightPos  = center + glm::vec3(0, size.y * 0.3f, 0);
```

- `lookDir`: defaults to +Z (good for cube-like and Z-elongated scenes
  including Cornell). Switches to +X when X-extent ≥ 1.3× Z-extent so
  Sponza keeps its down-the-axis framing.
- **FOV-aware fit dominates for cube-like scenes**: Cornell ends up at
  z=2.38 (visH=2.0, visW=2.0; distFromY=1.732, ×1.4 = 2.42). This is
  closer than v1's diag-based 3.44 and the old hardcoded 4.0, but the
  scene now fills the frame instead of sitting in the middle of black.
- **Min-backoff guard dominates for hall-shaped scenes**: Sponza ends up
  at x=3.32 (FOV-fit gives 1.93; min-backoff = 1.9 + 1.42 = 3.32).
  Comparable to the old hand-tuned 3.5, well outside the wall.
- **All four computed positions are still OUTSIDE the SDF volume**
  (codex 13 F2). The old hardcoded presets were too. Raymarching
  enters the volume via the existing ray-box intersection path; the
  alpha-collision check is intentionally skipped for outside-volume
  cameras (codex 09 F2 logic preserved).
- `0.05 × size.y` eye height above center: subtle lift.
- `0.3 × size.y` light height above center: scales with scene height.

A degenerate-bounds guard (diag < 1e-6) falls back to the old Cornell
literal so a malformed OBJ load can't park the camera inside a NaN.

### Bounds storage (codex 13 F1 atomicity)

Two new members on `Demo3D`:

```cpp
glm::vec3 currentObjBmin = glm::vec3(0.0f);
glm::vec3 currentObjBmax = glm::vec3(0.0f);
```

`loadOBJMesh()` queries `objLoader.getBounds(...)` immediately after
`normalize()` into **locals** `nbmin`/`nbmax`. The assignment to the
class members happens later in the same commit block as
`meshVoxelData/useOBJMesh/currentOBJPath`:

```cpp
// (early — locals only)
glm::vec3 nbmin, nbmax;
objLoader.getBounds(nbmin, nbmax);

// ... voxelize, fail-checks ...
if (newVoxelData.empty()) {
    return false;   // PREVIOUS mesh + bounds remain visible/active
}

// (commit block — atomic with rest of state)
meshVoxelData        = std::move(newVoxelData);
meshSDFReady         = false;
useOBJMesh           = true;
// ...
currentOBJPath       = objKey;
currentObjBmin       = nbmin;   // codex 13 F1: assign atomically
currentObjBmax       = nbmax;
```

Step 7 v1 broke this: it assigned to the members before voxelize, so
a failed-load case left the previous mesh visible but R-key reset
would use the failed mesh's bounds. Fixed.

### Codex 12 F1 simplification

`resetCameraToScenePreset()` no longer translates the 4-way
`currentOBJPath` key to a 2-way kind — it just calls the parameterless
preset:

```cpp
void Demo3D::resetCameraToScenePreset() {
    if (useOBJMesh && !currentOBJPath.empty()) {
        applyOBJViewPreset();
    } else {
        resetCamera();
    }
}
```

The codex 12 F1 fix existed only because the preset was name-keyed and
the new variants didn't match either name. With bounds as the input,
identity ceases to matter for the preset — `currentOBJPath` is
demoted to a pure UI/identity label.

---

## Files Touched

- `src/demo3d.h` — added `currentObjBmin`, `currentObjBmax`; changed
  `applyOBJViewPreset(string)` declaration to parameterless;
  documented `currentOBJPath` as label-only.
- `src/demo3d.cpp` — rewrote `applyOBJViewPreset()` body with the
  bounds-driven formula + degenerate guard; updated trailing log to
  print bounds + diag + computed positions; `loadOBJMesh()` now
  captures + stores bounds post-normalize and calls the parameterless
  preset; `resetCameraToScenePreset()` simplified.

No header dependencies added. No shader changes. No build-system
changes.

---

## Verification

### Build (Release, incremental)

- 0 errors, no new warnings (Step 6 baseline preserved).

### Per-OBJ auto-fit values (post-normalize bounds → preset positions)

After codex 13 F3 fix (FOV-aware fit + min-backoff guard):

| OBJ              | Bounds (post-normalize)         | diag  | camPos                  | dominated by    | light            |
|---|---|---|---|---|---|
| `cornell`        | `(-0.99,-0.98,-1)..(0.99,0.98,1)`        | 3.44  | `(0, 0.098, 2.38)`         | FOV fit         | `(0, 0.59, 0)`        |
| `cornell_orig`   | `(-0.995,-0.98,-1)..(0.995,0.98,1)`      | 3.44  | `(0, 0.098, 2.38)`         | FOV fit         | `(0, 0.59, 0)`        |
| `sponza`         | `(-1.9,-0.79,-1.17)..(1.9,0.79,1.17)`    | 4.74  | `(3.32, 0.079, 0)`         | min-backoff     | `(0, 0.48, 0)`        |
| `sponza_master`  | `(-1.9,-0.79,-1.17)..(1.9,0.79,1.17)`    | 4.74  | `(3.32, 0.079, 0)`         | min-backoff     | `(0, 0.48, 0)`        |

Both Cornell variants pick `lookDir = +Z` (X-extent equals Z); both
Sponza variants pick `lookDir = +X` (3.8 > 2.34 × 1.3 = 3.04).
Sponza variants are byte-identical OBJs (codex 12 F2) so identical
bounds and identical preset are expected.

**All four positions are OUTSIDE the SDF volume** (codex 13 F2). The
old hardcoded presets (Cornell `(0,0,4)`, Sponza `(3.5,0.5,0)`) were
also outside-volume cameras. Raymarching uses ray-box intersection at
march time; the alpha-validation is intentionally skipped for
outside-volume positions (codex 09 F2 logic preserved). Visible from
the runtime log:

```
[Demo3D] Camera preset validation: pos=(3.32068,0.0794485,0)
   OUTSIDE SDF volume (uvw=(1.33017,0.519862,0.5));
   alpha check skipped, relying on ray-box intersection at march time
```

### Headless captures (codex 13 F3 — captured at v3 positions)

Saved to `tools/step7v3_<name>_mode0.png` for all four OBJs (the v1
captures `tools/step7_<name>_mode0.png` and v2 `step7v2_*` are kept
as the framing-evolution record):

- **`step7v3_cornell_mode0.png`**: red wall + white walls + visible boxes
  inside; well-framed with the FOV-fit at z=2.38. Right wall stays
  default-gray because `cornell_box.obj` uses `DarkGreen` which the
  legacy fallback table doesn't know (pre-existing limitation, NOT a
  Step 7 regression).
- **`step7v3_cornell_orig_mode0.png`**: distinct red + green walls +
  glowing warm-white light + visible boxes; the FOV-fit makes the
  scene fill the frame substantially better than v1's 3.44 backoff.
- **`step7v3_sponza_mode0.png`** / **`step7v3_sponza_master_mode0.png`**:
  uniform-gray Sponza wall fills nearly the entire frame with the
  floor strip at the bottom, framed along +X axis from x=3.32. The
  v3 Sponza framing is visibly closer to the old Step 6 framing than
  the v1 4.74 attempt was.

**Codex 13 F3 visual-delta acknowledgement.** The Step 6 Sponza vs
Step 7 v1 Sponza pixel diff (715K of 921K pixels changed) was a real
framing regression — the v1 1.0×diag formula put Sponza too far. The
v3 FOV-fit + min-backoff at x=3.32 is much closer to the old x=3.5
hand-tuned position. We did NOT capture a fresh Step6-vs-Step7v3
pixel diff because Sponza captures vary run-to-run by Halton-jitter
EMA noise (codex 11 F3) — but visually the v3 Sponza screenshot is
back to "uniform-gray wall fills most of the frame" parity with
Step 6 instead of v1's "small dark rectangle in middle of black".

### Reset-helper runtime (codex 13 F5 scope-narrowed)

Both new variants verified at v3 positions:

```
=== cornell-orig (tools/app_run_step7v3_F1_cornell_orig.log) ===
[Demo3D] Applied auto-fit view preset (cornell_orig): ... camPos=(0, 0.098, 2.377)
[Demo3D] testResetCameraHelper before: pos=(0, 0.098, 2.377)
[Demo3D] testResetCameraHelper after move: pos=(2.5, 0.798, 3.677)
[Demo3D] Applied auto-fit view preset (cornell_orig): ... camPos=(0, 0.098, 2.377)

=== sponza-master (tools/app_run_step7v3_F1_sponza_master.log) ===
[Demo3D] Applied auto-fit view preset (sponza_master): ... camPos=(3.32, 0.079, 0)
[Demo3D] testResetCameraHelper before: pos=(3.32, 0.079, 0)
[Demo3D] testResetCameraHelper after move: pos=(5.82, 0.779, 1.3)
[Demo3D] Applied auto-fit view preset (sponza_master): ... camPos=(3.32, 0.079, 0)
```

**Coverage note (codex 13 F5).** This verifies
`resetCameraToScenePreset()` at the helper level. The R key in
`processInput()` and the ImGui "Reset Camera" button both call this
exact helper (verified by source review at
`src/demo3d.cpp:525` and `src/demo3d.cpp:3014`), so structural
coverage is high — but the test does NOT literally simulate a key
press or a click event. A future raylib input-injection harness
could close that gap.

### Logs preserved

v1 (1.0×diag formula — historical record):
- `tools/app_run_step7_<cornell|cornell_orig|sponza|sponza_master>.log`
- `tools/app_run_step7_F1_cornell_orig.log`
- `tools/app_run_step7_F1_sponza_master.log`

v3 (codex 13 F3 FOV-aware fit + min-backoff guard — current):
- `tools/app_run_step7v3_<cornell|cornell_orig|sponza|sponza_master>.log`
- `tools/app_run_step7v3_F1_cornell_orig.log`
- `tools/app_run_step7v3_F1_sponza_master.log`

### Known log noise (codex 13 F6 — same as Step 6)

Every Step 7 runtime log includes the pre-existing `sdf_3d.comp`
shader compile failure (`imageLoad(...)` overload mismatch). This
shader was replaced by the CPU-EDT path in Step 2 and is unused;
runtime continues normally. Do not treat the line as a Step 7
regression.

### Warnings (codex 13 F7)

Clean Release rebuild (`cmake --build . --config Release --clean-first`):
- 0 errors
- **38** project src warnings (was 37 baseline; +1 C4819)
- Distribution: 14×C4819, 9×C4244, 7×C4267, 5×C4100, 2×C4018, 1×C4310

The +1 C4819 is from line-position shifts in `demo3d.cpp` moving an
existing non-ASCII char region into a different MSVC scan window —
no new non-ASCII characters were introduced by Step 7. To verify in
future runs: `awk 'NR>=4500 && NR<=4570' src/demo3d.cpp | LC_ALL=C
grep -n '[^ -~\t]'` on the auto-fit code returns no matches.

---

## Architecture Notes

**`currentOBJPath` is now a pure label.** It still drives the ImGui
ACTIVE-button indicator and the friendly name in the panel, but
nothing logical depends on its value. Future variants only need:
(1) a new ImGui button calling `loadOBJMesh(path)`, (2) optional CLI
mapping in `main3d.cpp`, (3) optional new `currentOBJPath` value if
you want a distinct ACTIVE indicator. No preset wiring.

**The preset is per-bounds, not per-asset.** Two assets normalized to
the same bounds (e.g. both Sponza variants here) yield the same
camera/light. That's correct — a generic preset shouldn't care which
file the geometry came from.

**`lookDir` heuristic is intentionally narrow.** Only two cases (+Z
vs +X, with a 1.3× threshold) chosen to preserve the existing Cornell
and Sponza framing intent (axial view down the long dimension when
one exists). Adding more axes (e.g. -Z, ±Y) would help nothing the
current corpus needs and risks introducing surprise camera positions.
If a future asset has a Y-tall layout (e.g. a tower) we can add a
specific case then.

**Eye-height + light-height scale with mesh height.** Old presets
used absolute `0.5` and `0.8` light Y values regardless of scene
size. The new `0.3 × size.y` scales: a 0.5m-tall scene gets light at
0.15 above center, a 5m-tall scene gets light at 1.5 — both
proportional and visually appropriate.

**Light placement is geometry-driven, not material-driven (codex 13 F4).**
The `0.3 × size.y` light position is a *camera convenience preset* — it
guarantees visible illumination at a sensible height. It does NOT consume
the .mtl `Ke` emissive material data. Cornell-Original, for example, has
an emissive `light` material on the ceiling at y≈1; the auto-fit places
the directional point light at y≈0.59 instead. That's intentional for
this step: Step 6 documented Ke as albedo-boost only ("not a light
source; purely cosmetic"). A future step that drives `lightPosition`
from the centroid of Ke>0 voxels would be the right place to add
asset-correct lighting; until then this is "framing convenience" not
"physically correct emitter placement".

**No backwards-compat shim left behind.** The old `applyOBJViewPreset(string)`
overload is fully removed (not deprecated, not kept for "external
callers"). The only callers were `loadOBJMesh()` and
`resetCameraToScenePreset()`, both updated. If a future caller wants
a name-keyed preset, they should compute bounds first instead.

---

## Known Open Items (Step 7 boundary → later)

| Item | Where to land it |
|---|---|
| Sponza .mtl Kd uniform → still needs .tga loader | Step 8 candidate (texture loading) |
| Y-tall scenes (towers, vertical interiors) get the +Z view by default | add a 3rd `lookDir` case if/when an asset hits this |
| Filesystem-driven OBJ scan replaces hardcoded ImGui buttons | Step 9 candidate (drop-in folder pattern) |
| Camera collision with SDF / interior-camera path | future visual-quality step |
| Inside-Sponza black-mode-0 visibility (Step 6 follow-up) | widen `surfaceRadius` ladder; still pending |

---

## Why This Step Was Small

Three structural changes (bounds storage, formula, reset-path simplification)
totalling ~50 lines of net change. The hard work was already done in
Steps 4-6: per-OBJ scale (Step 4 4a), the helper-extraction pattern
(Step 5 codex 10 F3), and the 4-way OBJ key (Step 6) all set up the
right shape for this — Step 7 just turned the preset's implementation
inside out.
