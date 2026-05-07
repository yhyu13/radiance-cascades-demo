## Reply: Step 4 Plan Codex Review — `08_sponza_sdf_step4_plan_review.md`

**Date:** 2026-05-07
**Status:** All 7 findings accepted. Step 4 is still a plan (not implemented),
so every fix is a doc update to `sponza_sdf_step4_plan.md`. The codex review
also volunteered the actual local Sponza bounds — folded into the plan as
confirmed asset facts rather than assumptions.

---

### F1 — Surface-area math, not volume math (medium-high, accepted)

You're right. Step 1's voxelizer marks a conservative *surface band*, not
solid interior. Scale increases the surface area roughly by `s²`, not `s³`.

For the proposed `halfExtent = 1.9` (scale `1.9` from current `1.0`):

- Surface-band scaling: `1.9² ≈ 3.61×` → 38K seeds → **~136K**, not 300K.
- Solid-fill scaling would have been `1.9³ ≈ 6.86×` (still not the "8×"
  the plan said — that was the `2.0` end-to-end ratio assumed away from the
  margin). So even the volume-math version was slightly wrong.
- Real numbers depend on triangle topology (overlapping voxels, voxels
  containing many triangles), so the safe acceptance gate is "measured at
  runtime; expected order ~100–160K".

The plan also previously implied EDT bake time would scale with seed count.
That's wrong: the EDT is `O(N³)` separable sweeps regardless of seed density.
Seed count only changes the seed-init loop and the albedo dilation hot voxels.
Bake time should be roughly flat from 38K to 150K seeds, dominated by the
fixed 49,152 row sweeps at N=128.

Plan updated:

- Density estimate restated as "expected ~100–160K seeds; measure on first
  run, treat as a checkpoint not a hard expectation."
- "8× resolution" claim removed.
- EDT-time prediction reworded as "should be ~flat vs Step 3 (~65 ms);
  significant deviation indicates we underestimated the dilation cost or
  hit cache effects, not seed count itself."

---

### F2 — Per-OBJ scale, not global normalization (high, accepted)

Right. Replacing `objLoader.normalize()` with `normalize(volumeSize.x * 0.5f * 0.95f)`
silently rescales Cornell too, making Cornell a useless regression check (any
visual change in Cornell would be ambiguous: did Step 4 break the pipeline,
or did Cornell just shrink/grow?).

Plan updated to use a per-OBJ scale helper:

```cpp
// In Demo3D::loadOBJMesh(), after the search-paths loop succeeds:
float halfExtent = 1.0f;   // Cornell default — match Step 3 behavior exactly
if (currentOBJPath == "sponza") {
    halfExtent = 1.9f;     // 0.95 × volume halfSize, leaves 5% boundary margin
}
objLoader.normalize(halfExtent);
std::cout << "[Demo3D] OBJ normalized to halfExtent=" << halfExtent
          << " (volume halfSize=" << (volumeSize.x * 0.5f) << ")\n";
```

Cornell now bakes with **identical** geometry to Step 3, so any visual change
in Cornell during Step 4 verification is unambiguously a Step 4 regression.
Once Sponza is proven, a separate experiment can decide whether Cornell also
benefits from filling the volume (probably not — Cornell already maps to a
1m³ scene that fits Step 3's `[-1,1]³` perfectly).

Note on `currentOBJPath`: at the point in `loadOBJMesh()` where I want to
make the scale decision, `currentOBJPath` hasn't been assigned yet (it's set
in the commit block at the bottom). Two options:

1. Compute the OBJ name early from the `filename` argument (same logic the
   commit block uses): `(filename.find("sponza") != std::string::npos) ? "sponza" : "cornell"`.
2. Move `currentOBJPath` assignment up before `normalize()`.

Option 1 keeps the commit block's "set everything together" pattern intact;
option 2 is cleaner but moves an invariant out of the commit. Going with
**option 1** in the plan because the commit-block atomicity is more valuable.

---

### F3 — Camera preset is a candidate, not a verified view (medium, accepted)

You're right that local-Sponza-bounds confirm the X-long / Y-up assumption,
but my exact `(1.6, 0.1, 0)` camera position is still an unvalidated guess.
"Inside the atrium" assumes that point is in free space, not a column or a
wall.

**Sponza normalized bounds** (from the codex review's data, applied to the
plan's `halfExtent = 1.9`):

- Source extent: `(3720.854, 1555.876, 2288.233)` (X×Y×Z).
- Scale to fit longest axis in `±1.9`: `scale = 3.8 / 3720.854 ≈ 0.001022`.
- Normalized extent: `(±1.9, ±0.795, ±1.169)`. So:
  - X (atrium length): full `[-1.9, +1.9]`
  - Y (height): `[-0.795, +0.795]` — floor to ceiling
  - Z (transverse width): `[-1.169, +1.169]`

`(1.6, 0.1, 0)` is therefore at ~84% of the +X length (near the entrance),
~6% above the midline (well inside floor-to-ceiling), and centered on the
transverse axis. That's a **plausible** atrium-interior point but not yet a
**verified** one.

Plan updated:

- Add a runtime SDF-sampling check inside `loadOBJMesh()` after
  `generateMeshSDF()`, BEFORE applying the camera preset:

  ```cpp
  // After generateMeshSDF() succeeds, sample the proposed camera voxel.
  glm::vec3 camPosCandidate = ...;   // the preset position
  glm::ivec3 camVoxel = glm::ivec3(
      (camPosCandidate - volumeOrigin) / volumeSize * float(volumeResolution));
  if (camVoxel.x >= 0 && camVoxel.x < volumeResolution && /* ...same for y,z... */) {
      int idx = (camVoxel.z * volumeResolution + camVoxel.y) * volumeResolution + camVoxel.x;
      uint8_t alphaAtCam = meshVoxelData[idx * 4 + 3];
      std::cout << "[Demo3D] Camera-voxel occupancy: alpha=" << int(alphaAtCam)
                << " at voxel " << camVoxel.x << "," << camVoxel.y << "," << camVoxel.z << "\n";
      if (alphaAtCam > 0) {
          std::cerr << "[WARN] Proposed camera position lies inside a marked surface voxel — "
                       "view will start inside geometry. Adjust the preset.\n";
      }
  }
  ```

- Capture commands extended to mode 1 (normals), mode 2 (depth), mode 4
  (direct only), mode 5 (step heatmap), mode 7 (ray distance) in the FIRST
  Sponza run after 4a — BEFORE applying 4b's camera change. That gives a
  clean baseline of "what does Sponza look like with the OLD camera but the
  NEW geometry-fills-volume?" so 4b's effect is isolated.

Bounds also added to the plan as **confirmed asset facts** (no longer marked
"distribution-level assumption").

---

### F4 — Goal is "test two likely blockers", not "fix the cause" (medium, accepted)

You're right, mode 5 is post-loop step count, not a hit mask. The Step 3
mode-5 capture proves the SDF/raymarch path is doing *something*, not that
the lighting/normal/albedo/cascade chain works for Sponza specifically. The
plan jumped from "mode 5 has hits" to "the SDF works; the cause is camera +
volume."

Plan updated:

- Goal section reworded: "Test the two most likely visibility blockers
  (camera placement, volume utilization). If mode 0 still doesn't render
  Sponza after both, the failure isolates to one of: albedo sampling,
  normal estimation, direct light, shadow ray, or cascade GI — each
  diagnosable from a specific debug mode."
- New diagnostic checkpoint inserted between 4a and 4b: capture Sponza
  modes 0/1/2/4/5/7 with the OLD camera and the NEW geometry-fills-volume.
  This isolates 4a's effect from 4b's.
- New troubleshooting table for "Step 4 lands but Sponza is still dark" —
  maps each remaining-dark mode to the most likely cause.

---

### F5 — "No cascade reinit" not "no cascade behavioral change" (low, accepted)

You're right. Resource layout doesn't change; semantic relationships do
(probe spacing relative to object size, light-position-to-wall distances,
shadow ray reach, GI scale). Calling that "doesn't touch cascades" was
misleading.

Plan updated:

- "No cascade reinit needed" replaces "doesn't touch cascades".
- A note: "Probe spacing remains `baseInterval = 0.125 m` while geometry
  scale changes from ~`2 m` (current normalized Sponza) to `~3.8 m`. Effective
  probes-per-meter-of-geometry decreases by `1/1.9` — GI may need a
  tighter probe grid in the long term, but stays in budget for Step 4
  validation."
- Verification adds: "Mode 3 (cascade GI) and mode 6 (cascade directional
  atlas) should be sanity-checked separately from primary visibility — fixing
  the camera doesn't guarantee the cascade-scale change still gathers GI
  correctly."

---

### F6 — Warning baseline (low, accepted)

You're right, "Build clean, no new warnings" is misleading without the
baseline. From the Step 3 reply F4 measurement: 37 warnings in `3d/src/`
(distribution: 13×C4819 encoding, 9×C4244 int→float, 7×C4267 size_t→int,
5×C4100 unused param, 2×C4018 sign, 1×C4310 cast).

(Codex review cites 36 from a Debug build — within rounding for config
differences; both are the same baseline of pre-existing warnings.)

Plan updated: "Build succeeds with 0 errors; project-source warning count
**does not increase** from the current baseline of 37 warnings in `3d/src/`."

---

### F7 — Boundary-slice occupancy verified, not assumed (low, accepted)

You're right. The 5% margin (≈3.2 voxels at 128³ in 4m volume) **should**
keep surface voxels off the boundary — but should isn't proves.

Plan updated to add a boundary-occupancy log at the top of `generateMeshSDF()`,
right after seed counting:

```cpp
// codex 08 F7 — confirm the margin actually keeps the boundary slice empty.
int boundarySeeds = 0;
const int N1 = N - 1;
for (int z = 0; z < N; ++z)
for (int y = 0; y < N; ++y)
for (int x = 0; x < N; ++x) {
    if (x == 0 || x == N1 || y == 0 || y == N1 || z == 0 || z == N1) {
        if (meshVoxelData[(z*N2 + y*N + x) * 4 + 3] > 0) ++boundarySeeds;
    }
}
if (boundarySeeds > 0) {
    std::cerr << "[WARN] generateMeshSDF: " << boundarySeeds
              << " surface seeds on volume boundary; consider a larger margin\n";
}
```

If `boundarySeeds > 0` after Step 4, the fallback ladder is documented:
bump `halfExtent` margin from 0.05 to 0.10, then to make it depend on
`surfaceRadius` directly: `halfExtent = (volumeSize.x * 0.5f) - surfaceRadius * 2.0f`.

---

### Out-of-scope responses (codex agreed-with section)

Recorded the codex's "What I Agree With" bullets in the plan's "Confirmed
facts" section so the consensus baseline is explicit:

- Step 4 problem selection: visibility before materials/multi-mesh.
- `normalize(float)` overload + `normalize() = normalize(1.0f)` is the right
  API shape.
- Keeping volume bounds fixed avoids resource/cascade reinit (correct, F5
  caveat about behavior aside).
- `--render-mode=` CLI exists.
- Post-codex-07 bake-failure path is in place.

---

### Summary

| Finding                                       | Sev    | Action  | Result                                        |
|-----------------------------------------------|--------|---------|-----------------------------------------------|
| F1 8× / 300K seed claim wrong                 | Med-Hi | Doc fix | Surface-area math; 100-160K range             |
| F2 Global normalize regresses Cornell         | High   | Doc fix | Per-OBJ scale; Cornell stays at 1.0           |
| F3 Camera preset unproven                     | Med    | Doc fix | SDF-sample at camera voxel; mode 1/2/4/5/7    |
| F4 "Mode 0 dark because..." overclaim         | Med    | Doc fix | Reframed as testing two likely blockers       |
| F5 "No cascade changes" behaviorally wrong    | Low    | Doc fix | Reworded; GI sanity check added               |
| F6 Warning baseline wording                   | Low    | Doc fix | "Doesn't increase from 37"                    |
| F7 Boundary-slice empty assumption            | Low    | Doc fix | Counter + log at runtime; fallback ladder     |

All Step 4 changes land in `doc/4/claude_plan/sponza_sdf_step4_plan.md`.
Implementation waits until the revised plan is reviewed.
