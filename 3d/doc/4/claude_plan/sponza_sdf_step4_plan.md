# Sponza SDF — Step 4: OBJ Visibility (camera + volume utilization) — v2

**Date:** 2026-05-07 (revised after codex review `08_*` / reply `08_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_step3_impl.md` (Step 4 prerequisites)
**Status:** Draft v2 — pending implementation
**Changelog:** v2 — all 7 codex findings accepted: per-OBJ scale (Cornell stays
at 1.0 for clean regression), surface-area density math (~3.6× not 8×),
camera-position SDF-sampling validation, multi-mode diagnostic checkpoints,
"no cascade reinit" replaces "no cascade change", warning-baseline wording,
boundary-slice runtime check.

---

## Goal

**Test the two most likely blockers** for Sponza visibility in mode 0:
camera placement and SDF volume utilization. Step 3 proved the wiring works
and the EDT bake is non-trivial (mode 5 has hits), but mode 5 is a post-loop
step-count heatmap, not a hit mask — it doesn't isolate which downstream
component (primary, normals, albedo, direct, shadow, GI) actually breaks.

Step 4 lands the two cheapest experiments and adds a diagnostic mode set
(0/1/2/4/5/7) so any remaining-dark mode points to a specific cause.

---

## Confirmed Facts (from current source + codex review data)

- `obj_loader.h:155-172` `OBJLoader::normalize()` rescales mesh to fit `[-1, 1]³`.
- `demo3d.cpp:169-172` Volume = `[-2, 2]³` (4m), `baseInterval = 0.125`.
- `demo3d.cpp:4275-4290` `resetCamera()` puts camera at `(0,0,4) → (0,0,0)` FOV 60°.
- `demo3d.cpp:4320-4386` `loadOBJMesh()` calls `objLoader.normalize()` unconditionally.
- `main3d.cpp:179-182` `--render-mode=N` CLI is in place (Step 3).
- `raymarch.frag:407-414` intersects the SDF volume bounds first; an outside
  camera works as long as the view ray enters the volume.
- Step 3 logs: Sponza 37,757 seeds, Cornell 40,878 seeds.
- **Local Sponza bounds (codex 08 verified):** 145,185 vertices, min
  `(-1920.946, -126.443, -1182.807)`, max `(1799.908, 1429.433, 1105.426)`,
  extent `(3720.854, 1555.876, 2288.233)`. **X is the longest axis (atrium
  length); Y is up (height); Z is transverse width.** This is the standard
  Crytek Sponza orientation — the plan's axis assumption is now an asset
  fact, not a guess.

After `OBJLoader::normalize(halfExtent=1.9)` (Sponza scale = `3.8 / 3720.854 ≈
0.001022`), normalized Sponza bounds are:

| Axis | Source extent | Normalized range |
|---|---|---|
| X (length) | 3720.854 | `[-1.900, +1.900]` |
| Y (height) | 1555.876 | `[-0.795, +0.795]` |
| Z (width)  | 2288.233 | `[-1.169, +1.169]` |

Codex baseline acknowledged:

- `normalize(float)` overload preserving `normalize() = normalize(1.0f)` is
  a clean local API.
- Keeping volume bounds fixed avoids resource/cascade reinit (the F5 caveat
  about *behavior* is recorded separately below).
- Post-codex-07 bake-failure path is in place; Step 4 builds on a clean
  render-loop lifecycle.

---

## Three Changes (in dependency order)

### Change 4a — Per-OBJ normalize, with Sponza filling the volume

(F1, F2 corrections.)

```cpp
// obj_loader.h — new overload
void normalize(float halfExtent) {
    if (vertices.empty()) return;
    glm::vec3 min, max;
    getBounds(min, max);
    glm::vec3 center = (min + max) * 0.5f;
    float maxExt = std::max(max.x - min.x, std::max(max.y - min.y, max.z - min.z));
    if (maxExt <= 0.0f) return;
    float scale = (2.0f * halfExtent) / maxExt;
    for (auto& v : vertices) v.position = (v.position - center) * scale;
}

// Existing API preserved:
inline void normalize() { normalize(1.0f); }
```

In `Demo3D::loadOBJMesh()`, replace the unconditional `objLoader.normalize()`
call with a per-OBJ scale lookup. **Cornell stays at `1.0` (Step 3 baseline,
clean regression check).** Only Sponza changes:

```cpp
// codex 08 F2 — per-OBJ scale; Cornell unchanged so it remains a clean regression check.
const std::string objKind = (filename.find("sponza") != std::string::npos) ? "sponza" : "cornell";
float halfExtent = 1.0f;
if (objKind == "sponza") {
    halfExtent = 1.9f;   // 0.95 × volume halfSize, leaves 5% boundary margin (F7)
}
objLoader.normalize(halfExtent);
std::cout << "[Demo3D] OBJ normalized to halfExtent=" << halfExtent
          << " (volume halfSize=" << (volumeSize.x * 0.5f) << ")\n";
```

(Note: `objKind` is computed early from `filename`; `currentOBJPath` is still
assigned in the commit block at the bottom for atomicity.)

**Density expectation (F1).** Surface voxelizer marks a band, so seed count
scales as area not volume:

- Linear scale 1.0 → 1.9 → area scale `1.9² ≈ 3.6×`.
- 37,757 seeds × 3.6 ≈ **~136K** expected.
- Acceptance gate: `seedCount > 100,000` after 4a (the previous plan's
  150-300K range was overly optimistic).
- Real number depends on triangle-per-voxel overlap; treat as a checkpoint
  to log, not a hard expectation.

**EDT bake time expectation (F1).** EDT is `O(N³)` separable sweeps regardless
of seed density; the inner parabola loops dominate. Seed count only changes
the seed-init pass and the albedo dilation hot voxels. Expect bake time
**roughly flat** vs Step 3 (~65 ms). Significant deviation indicates dilation
cost (more occupied voxels = more iter-1 fills) or cache effects, not seed
count itself. Soft gate: `< 200 ms`.

### Change 4b — Camera preset (Sponza only) — diagnostic-driven

(F3, F4 corrections.)

After `generateMeshSDF()` succeeds (so `meshVoxelData` is populated), and
**before** committing `useOBJMesh`, validate the proposed camera position
against the SDF before applying it:

```cpp
// codex 08 F3 — validate the preset camera position is in free space.
glm::vec3 camPosCandidate, camTargetCandidate;
float     camFovyCandidate;
bool      hasPreset = false;

if (objKind == "sponza") {
    // Sponza atrium (X long, Y up, Z transverse — confirmed asset fact).
    // Position: ~84% along +X, slightly above midline, centered transversely.
    camPosCandidate    = glm::vec3( 1.6f, 0.1f, 0.0f);
    camTargetCandidate = glm::vec3(-1.0f, 0.1f, 0.0f);
    camFovyCandidate   = 75.0f;
    hasPreset = true;
} else if (objKind == "cornell") {
    // Cornell Box closed mesh — camera outside, Step 3 baseline.
    camPosCandidate    = glm::vec3(0.0f, 0.0f, 4.0f);
    camTargetCandidate = glm::vec3(0.0f, 0.0f, 0.0f);
    camFovyCandidate   = 60.0f;
    hasPreset = true;
}

if (hasPreset) {
    // Sample the SDF voxel at the proposed camera position to confirm it's
    // not inside marked geometry (column, wall, or the conservative band).
    glm::vec3 uvw      = (camPosCandidate - volumeOrigin) / volumeSize;
    glm::ivec3 voxel   = glm::ivec3(uvw * float(volumeResolution));
    voxel = glm::clamp(voxel, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
    int idx            = (voxel.z * volumeResolution + voxel.y) * volumeResolution + voxel.x;
    uint8_t alphaAtCam = meshVoxelData[idx * 4 + 3];
    std::cout << "[Demo3D] Camera preset validation: pos=(" << camPosCandidate.x
              << "," << camPosCandidate.y << "," << camPosCandidate.z
              << ") voxel=(" << voxel.x << "," << voxel.y << "," << voxel.z
              << ") alpha=" << int(alphaAtCam) << "\n";
    if (alphaAtCam > 0) {
        std::cerr << "[WARN] Proposed camera position lies inside a marked surface voxel — "
                     "view will start inside geometry. Adjust the preset.\n";
    }

    camera.position = camPosCandidate;
    camera.target   = camTargetCandidate;
    camera.up       = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fovy     = camFovyCandidate;
}
```

**Why the preset is a candidate, not a verified view.** After
`halfExtent=1.9`, the Sponza camera point `(1.6, 0.1, 0)` is at:

- X: 84% of the +X length (just inside the +X end, near "the entrance")
- Y: +0.1 in `[-0.795, +0.795]` floor-to-ceiling — slightly above midline
- Z: 0 in `[-1.169, +1.169]` — center of transverse width

That **should** be in the central walkway of the atrium. But "should" depends
on the actual mesh geometry — the alpha-sample log makes this verifiable.

**Step 4 success does NOT require Sponza to render correctly on the first
try.** It requires that:
1. The SDF-sample log shows whether the candidate point is in free space.
2. If `[WARN]` fires, we adjust the preset (one-line change to position).
3. After the preset is in free space, the diagnostic capture set
   (modes 0/1/2/4/5/7) tells us whether visibility is still blocked and by
   what — see Diagnostic Checkpoint section below.

### Change 4c (deferred) — auto-fit camera from bounds + atrium/box heuristic

Skipped. Two named OBJs → presets are deterministic and cheaper. Generalize
when a third OBJ shows up.

---

## Diagnostic Checkpoint Between 4a and 4b (F4 correction)

After 4a is implemented and **before** 4b, capture Sponza in modes 0/1/2/4/5/7
**with the OLD camera** (`(0,0,4) → (0,0,0)`). This isolates 4a's effect from
4b's:

```powershell
.\build\RadianceCascades3D.exe --load-obj=sponza --render-mode=0 --exit-frames=120 --screenshot=tools\step4a_sponza_mode0.png
.\build\RadianceCascades3D.exe --load-obj=sponza --render-mode=1 --exit-frames=120 --screenshot=tools\step4a_sponza_mode1.png
.\build\RadianceCascades3D.exe --load-obj=sponza --render-mode=2 --exit-frames=120 --screenshot=tools\step4a_sponza_mode2.png
.\build\RadianceCascades3D.exe --load-obj=sponza --render-mode=4 --exit-frames=120 --screenshot=tools\step4a_sponza_mode4.png
.\build\RadianceCascades3D.exe --load-obj=sponza --render-mode=5 --exit-frames=120 --screenshot=tools\step4a_sponza_mode5.png
.\build\RadianceCascades3D.exe --load-obj=sponza --render-mode=7 --exit-frames=120 --screenshot=tools\step4a_sponza_mode7.png
```

| Mode | What it shows | What "still dark" means |
|---|---|---|
| 0 | Final raymarch (primary + direct + indirect + tone-mapped) | Catch-all; needs the others to triage |
| 1 | Surface normals as RGB | Dark → primary rays don't hit (bake/threshold issue); colored → normals work, lighting is the issue |
| 2 | Depth (near=white) | Dark → no primary hits; bright → hits but later steps drop the value |
| 4 | Direct lighting only (no GI) | Dark → direct light placement / shadow ray issue; bright → GI is the dim part |
| 5 | Step-count heatmap (already captured in Step 3) | Sanity check that `sampleSDF` evaluates |
| 7 | Ray travel distance (continuous) | Dark → most rays exit volume; bright → rays terminate inside |

If 4a alone makes Sponza visible in mode 0 with the OLD camera, 4b is
optional — but still worth applying for ergonomics. If 4a alone leaves
Sponza dark with the OLD camera, the diagnostic set tells us where to look
next (mode 1 vs 4 vs 7 each pinpoint a different stage).

After 4b, repeat the captures with the new camera (`step4_sponza_mode*.png`)
to confirm the preset works.

---

## "No cascade reinit" — but cascades behavior changes (F5 correction)

Resource layout doesn't change — `volumeSize`, `baseInterval`, FBO sizes,
volume textures, cascade allocations all stay constant. **No cascade reinit
required.**

But the geometry/probe ratio shifts:

- Probe spacing: `baseInterval = 0.125 m` (unchanged)
- Sponza geometry scale: source 3.7 m → normalized 3.8 m (was 2.0 m)
- Effective probes per meter of geometry: drops by `1/1.9 ≈ 0.53×`

GI may need a tighter probe grid in the long term (more `cascadeC0Res`), but
stays well within budget for Step 4 validation. The check: mode 3 (cascade
GI) and mode 6 (cascade directional atlas) should still produce non-zero
indirect on Sponza walls. If indirect goes flat / unnaturally smooth, that's
the probe-spacing-vs-geometry issue and a follow-up to widen `cascadeC0Res`
in OBJ mode.

---

## Files Touched

| File | Function / area | Change |
|---|---|---|
| `src/obj_loader.h` | `normalize()` | Add `normalize(float halfExtent)` overload; keep `normalize() = normalize(1.0f)` |
| `src/demo3d.cpp` | `loadOBJMesh()` (early — before commit block) | Per-OBJ `objKind` + `halfExtent` lookup; call `normalize(halfExtent)` |
| `src/demo3d.cpp` | `loadOBJMesh()` (after `generateMeshSDF`) | Camera-preset + alpha-sample validation block |
| `src/demo3d.cpp` | `generateMeshSDF()` (after seed-count loop) | F7 boundary-slice occupancy log |

No header changes (`Demo3D::camera` already exists). No shader changes. No
cascade reinit.

---

## Boundary-Slice Margin Check (F7)

Add to `generateMeshSDF()` after the seed-count loop:

```cpp
// codex 08 F7 — confirm the halfExtent margin keeps the boundary slice empty.
int boundarySeeds = 0;
const int N1 = N - 1;
for (int z = 0; z < N; ++z)
for (int y = 0; y < N; ++y)
for (int x = 0; x < N; ++x) {
    if (x == 0 || x == N1 || y == 0 || y == N1 || z == 0 || z == N1) {
        if (meshVoxelData[(z*N2 + y*N + x) * 4 + 3] > 0) ++boundarySeeds;
    }
}
std::cout << "[Demo3D] Boundary-slice surface seeds: " << boundarySeeds
          << " (target=0; >0 means surface voxels touch volume edge)\n";
if (boundarySeeds > 0) {
    std::cerr << "[WARN] " << boundarySeeds << " surface seeds on volume boundary; "
                 "consider larger halfExtent margin\n";
}
```

Fallback ladder if `boundarySeeds > 0`:

1. Bump margin from 5% to 10% (`halfExtent = 1.8f`).
2. Make margin depend on `surfaceRadius` directly:
   `halfExtent = (volumeSize.x * 0.5f) - surfaceRadius * 2.0f`
   (where `surfaceRadius = voxelSz * sqrt(3) * 0.5`).
3. Investigate per-axis margins if Y vs X behave very differently.

---

## Verification Checklist (revised)

### Build

- [ ] Build succeeds with 0 errors; project-source warning count
      **does not increase** from the current baseline of **37 warnings in
      `3d/src/`** (distribution: 13×C4819 encoding, 9×C4244 int→float,
      7×C4267 size_t→int, 5×C4100 unused param, 2×C4018 sign, 1×C4310 cast).

### Cornell (regression — must be unchanged)

- [ ] `[Demo3D] OBJ normalized to halfExtent=1` log line
- [ ] `[Demo3D] Camera preset validation: ... alpha=0` log line
- [ ] Voxel seed count = 40,878 (identical to Step 3 — Cornell scale unchanged)
- [ ] Mode 0 capture identical to Step 3's `step3_cornell_mode0.png` (within EMA noise)
- [ ] Mode 3 capture identical to Step 3's `step3_cornell_mode3.png`

### Sponza after 4a only (diagnostic checkpoint, OLD camera)

- [ ] `[Demo3D] OBJ normalized to halfExtent=1.9` log line
- [ ] Voxel seed count `> 100,000` (expected ~136K; treat as checkpoint)
- [ ] EDT bake time `< 200 ms` (expected ~flat vs Step 3's 65 ms)
- [ ] **F7:** `[Demo3D] Boundary-slice surface seeds: 0` (else fallback ladder)
- [ ] Mode 5 capture: hit-band covers more of the viewport than Step 3's
- [ ] Modes 0/1/2/4/7: captured for triage; results noted in impl doc

### Sponza after 4b (camera preset)

- [ ] `[Demo3D] Camera preset validation: ... alpha=0` (NO `[WARN]`)
- [ ] If `[WARN]` fires: adjust preset position; rerun until alpha=0
- [ ] Mode 0: recognizable Sponza geometry (columns, atrium walls, arches)
- [ ] Mode 1 (normals): atrium walls show distinct R/G/B by axis
- [ ] Mode 3 (cascade GI): non-zero indirect on Sponza walls
- [ ] Mode 4 (direct only): direct lighting visible on at least some walls
- [ ] Switch Cornell ↔ Sponza twice — camera updates each load (no stale Cornell camera left)

### Step 4 succeeds when

- Sponza mode 0 shows recognizable atrium geometry, **AND**
- Cornell mode 0/3 are unchanged from Step 3.

If Sponza mode 0 is still dark after 4b, the diagnostic captures from the
checkpoint section identify the next subsystem to fix (Step 5 candidate).

---

## Risks (revised)

| Risk | Notes |
|---|---|
| Sponza camera preset still in occupied voxel after F3 SDF sample | Single-line tweak; rerun until `alpha=0` |
| Mode 0 still dark even with valid camera | Diagnostic captures (1/2/4/5/7) identify failing subsystem |
| Boundary-seeds > 0 (F7 fallback fires) | Documented ladder: 10% margin → surfaceRadius-derived margin |
| EDT bake time > 200 ms at higher seed count | Soft gate; if hit, switch to flat-indexed sweeps (Step 2 deferred optimization) |
| Cornell visual regression from F2 work | Cornell scale is intentionally unchanged in 4a; any visual delta is a 4b/F3-camera-block bug, easy to bisect |
| Probes-per-geometry-meter dropped 1.9× | Mode 3/6 sanity check; if GI looks wrong, widen `cascadeC0Res` in OBJ mode (deferred to Step 5) |
| Alpha-sample point-test is a single voxel — could miss a thin column 1 voxel away | Acceptable for "is the camera obviously inside geometry"; rays exiting the camera will sphere-trace the rest. If false positives become an issue, sample a small neighborhood. |

---

## What's Out of Scope for Step 4

- Multi-mesh / scene composition.
- Watertight-mesh detection / signed SDF.
- True nearest-color albedo (still using 3-iter L1 dilation from Step 2).
- General auto-fit camera (Change 4c, deferred).
- Volume-bounds change (rejected — would force cascade reinit).
- `cascadeC0Res` widening for OBJ mode (deferred to Step 5 if mode 3
  diagnostics show probe-spacing-vs-geometry issues).
- Sponza materials (the `.mtl` is referenced by the OBJ but never loaded).

---

## Implementation Order

1. **4a** — `OBJLoader::normalize(float)` overload + per-OBJ scale lookup +
   F7 boundary-slice log. Run Sponza only, capture diagnostic mode set
   0/1/2/4/5/7, note seed count and boundary-seed count.
2. **4b** — per-OBJ camera presets + F3 SDF-sample validation. Run Cornell
   first (regression — must be byte-identical to Step 3), then Sponza
   (capture mode set with new camera).
3. Update `sponza_sdf_step4_impl.md` with: measured seed count, EDT/albedo
   times, boundary-seed count, alpha-at-camera, and the full mode set
   captures from both checkpoints.
