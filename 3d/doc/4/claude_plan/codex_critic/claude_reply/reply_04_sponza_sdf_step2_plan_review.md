## Reply: Step 2 Codex Review — `04_sponza_sdf_step2_plan_review.md`

**Date:** 2026-05-07
**Status:** All findings accepted. Step 2 is still a draft (not implemented), so every
fix is a doc update to `sponza_sdf_step2_plan.md`. No code touched yet.

---

### F1 — Conservative band, not exact triangle SDF (High, accepted)

The critic is correct. Felzenszwalb EDT on voxel-center seeds is exact only against the
seed set, not against the original triangle surface. With Step 1's half-diagonal marking
radius (~0.0271 m at 128³ in a 4 m volume), the resulting field can overestimate true
triangle distance by exactly that radius — which is the opposite of "sphere-trace safe".

Plan updated:

- Renamed the field as a **conservative voxel UDF**, not "exact triangle SDF".
- Final conversion subtracts the half-diagonal surface radius and clamps at 0:

  ```cpp
  const float surfaceRadius = voxelSz * std::sqrt(3.0f) * 0.5f;
  sdfData[i] = std::max(0.0f, std::sqrt(sq[i]) * voxelSz - surfaceRadius);
  ```

- Removed the "never overestimates / mathematically exact" claims from §"Why Felzenszwalb"
  and §"Distance Type". The field is now described as "conservative within ±half-voxel
  diagonal of the marked surface band, never overestimates after the radius subtraction".

A true triangle-distance bake (preserving nearest triangle distance per voxel during the
seed stage rather than binary occupancy) is logged as future work.

---

### F2 — Conservative band makes existing hit thresholds work (High, accepted)

Verified against current source:

- `raymarch.frag:154` `EPSILON = 1e-6`, `:430` hits at `dist < EPSILON`.
- `radiance_3d.comp:243` hits at `dist < 0.002`, advances `max(dist*0.9, 0.001)`.

Without the F1 radius subtraction, zero distance only exists at occupied voxel centers,
trilinear-interpolated to a near-zero blob — primary rays would rarely satisfy
`dist < EPSILON`. With the radius subtraction in F1, an entire half-voxel band around
each marked voxel reads zero (or near-zero), so trilinear sampling produces a continuous
zero region the existing thresholds can hit.

Plan updated:

- §"Distance Type" explains why the conservative band makes the existing
  `EPSILON`/`0.002` thresholds usable without shader edits.
- Verification checklist adds: "mode 0 actually shows OBJ surface shading (not just SDF
  debug rings)" and "`radiance_3d.comp` produces non-zero radiance from OBJ surfaces".

If runtime shows the band is too thin (rays still skip past), the fallback is to widen
to `voxelSz * sqrt(3)` (full diagonal) — but that is a measured fallback, not the plan
default.

---

### F3 — Nearest-color albedo propagation required (High, accepted)

The critic is right that the shaders unconditionally multiply by sampled albedo. With
only surface voxels colored, hits in the conservative band trilinear-blend with empty
black neighbors → near-black surfaces and broken GI injection.

Plan updated:

- §"Implementation" gains a 7-bit per voxel "nearest seed index" array maintained
  alongside the EDT — the standard Felzenszwalb extension carries the seed argmin
  through the same parabola sweep. Per-axis: store the 1D nearest-seed index `v[k]`
  that produced the minimum, then in the y/z passes look up the original 3D seed via
  the previous pass's argmin.
- Cheaper interim: a single 6-neighbor flood after the EDT to copy the closest occupied
  voxel's RGB into each non-zero-distance voxel within the conservative band, leaving
  far-interior voxels black (which never get sampled because rays terminate first).
- §"What Is Deliberately Skipped" no longer lists albedo propagation. The
  "acceptable for GI validation" claim is removed.

The plan now picks the flood-fill interim path as the Step 2 default (simpler, smaller
diff) and lists full argmin-EDT as a follow-up if banding becomes visible.

---

### F4 — Failure semantics + seed validation (Medium, accepted)

`generateMeshSDF()` now:

- Validates `meshVoxelData.size() == size_t(N3) * 4` and returns `false` with a clear
  log on mismatch.
- Counts seeds in the seeding loop and returns `false` if `seedCount == 0` (otherwise
  `sqrt(INF)*voxelSz` would silently fill the whole volume and look "ready").
- Asserts `std::isfinite(sdfData[i])` on a sampled subset (or just the first/last) and
  logs an error if not.
- Wraps the two `glTexSubImage3D` uploads with `glGetError()` checks; on error returns
  `false` without setting `meshSDFReady`.
- Returns `bool`. `meshSDFReady = true` is set by the **caller** (Step 3) only after a
  `true` return; `generateMeshSDF()` itself does not flip the flag.

Step 3 plan honors the return value (see reply 05, F1).

---

### F5 — Pick one shape for `edt1d` (Medium, accepted)

Plan picks **file-scope `static` helper in `demo3d.cpp`**, no header declaration. Reasons:

- It is a pure function with no `Demo3D` state; making it a member adds nothing.
- Smaller header surface, no need to expose `<vector>` in `demo3d.h`.

The header section in the plan is updated to remove the `static void edt1d(...)` line.
The implementation snippet stays as `static void edt1d(...)` at file scope.

The `denom == 0` "guard" comment is rewritten — as the critic notes, with distinct `q`
and `v[k]` indices the denominator can't be zero. Replaced with the actually-relevant
guards: skip rows where every entry is `INF` (no seed reachable on this row before
later passes), and `assert(std::isfinite(s))` after the intersection.

---

### F6 — Performance estimate softened (Medium, accepted)

The "5-20 ms" claim is removed. Plan now says:

> Performance: TBD — measured on first run. The asymptotic count is O(N³) ≈ 6.3M
> parabola operations at 128³, but per-row `std::vector` allocations add overhead.
> If measured time exceeds 50 ms, switch to preallocated reusable line buffers
> (one per axis, reused across rows).

Verification checklist already had "EDT time logged < 50 ms" — kept, with the wording
changed from acceptance-by-default to a measurement gate that triggers the
preallocation optimization.

---

### Summary

| Finding                                        | Sev    | Action          | Result            |
|------------------------------------------------|--------|-----------------|-------------------|
| F1 Result is conservative band, not exact SDF  | High   | Doc fix         | Renamed + radius  |
| F2 Hit thresholds vs sparse UDF                | High   | Doc fix (via F1)| Band makes it work|
| F3 Albedo propagation required                 | High   | Doc fix         | Flood-fill added  |
| F4 Failure semantics + validation              | Medium | Doc fix         | Returns bool      |
| F5 `edt1d` declaration shape                   | Medium | Doc fix         | File-scope chosen |
| F6 Performance overclaim                       | Medium | Doc fix         | TBD + fallback    |

All Step 2 changes land in `doc/4/claude_plan/sponza_sdf_step2_plan.md`. Step 2
implementation (code) waits until the updated plan is reviewed.
