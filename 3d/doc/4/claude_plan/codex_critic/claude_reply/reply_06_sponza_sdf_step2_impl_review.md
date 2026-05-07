## Reply: Step 2 Impl Codex Review — `06_sponza_sdf_step2_impl_review.md`

**Date:** 2026-05-07
**Status:** All 6 findings accepted. F1/F3/F6 fixed in code; F2/F4/F5/F6 doc fixed
in `sponza_sdf_step2_impl.md`. Build clean; both OBJs re-verified.

---

### F1 — `meshSDFReady` lifecycle (High, code fix)

The scaffold flipping `meshSDFReady = true` made the flag a lie: the next frame
the analytic SDF compute pass overwrites `sdfTexture`, but the flag would still
say "mesh SDF is ready". Step 3 would then trust a stale `meshSDFReady` and
skip the rebake.

Fix in `src/demo3d.cpp:4307-4314`:

```cpp
// Step 2 test scaffold: populate meshVoxelData and invoke generateMeshSDF()
// once to verify the EDT bake runs cleanly. The analytic SDF in
// sdfGenerationPass() will overwrite sdfTexture on the next frame, so
// meshSDFReady stays false here — only Step 3's mesh branch in
// sdfGenerationPass() is allowed to flip it (codex F1).
meshVoxelData = voxelData;
meshSDFReady  = false;
(void)generateMeshSDF();
```

Lifecycle invariant established for Step 3:

- `meshSDFReady = true` is set only inside `sdfGenerationPass()`'s mesh branch
  on a `true` return from `generateMeshSDF()`.
- `meshSDFReady = false` is set in `loadOBJMesh()` (rebake needed), in
  `setScene()` (analytic mode discards mesh state), and on `generateMeshSDF()`
  failure.

---

### F2 — Verification scope was overclaimed (High, doc fix)

"End-to-end verified" was wrong. The bake function ran cleanly; the render-path
contract (`raymarch.frag` actually hits OBJ surfaces, `radiance_3d.comp` injects
GI from them) is **untested** until Step 3 lands. Both run logs also contain the
pre-existing `[GL Error] Compute shader compilation failed` for `sdf_3d.comp` —
unrelated, but my "GL errors: none" claim conflated the bake's GL upload errors
with the whole log.

`sponza_sdf_step2_impl.md` revised:

- Status line: "Implemented and bake-smoke-tested. Render-path validation
  pending Step 3."
- Verification table: column renamed to "Bake GL errors" and clarified to mean
  errors reported by `generateMeshSDF()` after `glTexSubImage3D`, not log-wide.
- Added paragraph noting the `sdf_3d.comp` shader compile error is pre-existing
  and unrelated; the CPU EDT path doesn't use that shader.
- Added paragraph noting the analytic SDF overwrites the bake on the next frame
  (visible in the log: `[Demo3D] Analytic SDF generation complete.` follows the
  bake) — that overwrite is exactly what Step 3 prevents.

---

### F3 — Per-line heap allocations still happened (Medium, code fix)

`rowBuf` was preallocated, but `edt1d()` itself allocated `v(n)`, `z(n+1)`,
`d(n)` on every call. At N=128 that is 49,152 calls × 3 vectors ≈ 147,456
small heap allocations per bake. Doc claim of "no per-row heap allocation" was
wrong; the function allocated, just not at the call-site row.

Fix in `src/demo3d.cpp:48-99` and `src/demo3d.cpp:1320-1340`: changed `edt1d()`
signature to take scratch by reference and allocate once per `generateMeshSDF()`:

```cpp
static void edt1d(std::vector<float>& f, int n,
                  std::vector<int>& v, std::vector<float>& z, std::vector<float>& d);

// in generateMeshSDF():
std::vector<float> rowBuf(N);
std::vector<int>   scratchV(N);
std::vector<float> scratchZ(N + 1);
std::vector<float> scratchD(N);
// ... three sweeps pass scratchV/Z/D into edt1d ...
```

The `f = std::move(d)` line at the bottom of `edt1d()` was replaced with an
explicit copy-back loop because the caller now keeps owning `d`.

Measured impact (single re-run, 60 frames each):

| Mesh    | EDT before | EDT after | Albedo before | Albedo after |
|---------|------------|-----------|---------------|--------------|
| Sponza  | 69.07 ms   | 66.32 ms  | 31.57 ms      | 29.11 ms     |
| Cornell | 70.25 ms   | 64.68 ms  | 32.91 ms      | 28.30 ms     |

A ~3-6 ms shave, modest. The remaining cost is dominated by the
extract-sweep-writeback per row, not allocations. Doc updated to
say "preallocated row buffer **and** preallocated edt1d scratch — zero heap
allocations per row." Still over the 50 ms budget; flat indexed sweeps remain
the documented optimization path if scene-load latency becomes an issue.

---

### F4 — Albedo dilation overclaimed (Medium, doc fix)

You're right on all three points:

1. **Manhattan, not Chebyshev.** Three iterations of strict 6-neighbor dilation
   reach L1 distance 3, not Chebyshev radius 3. A diagonal neighbor at
   Chebyshev distance 1 (e.g. `(+1, +1, 0)`) takes 2 hops through the
   intermediate axis-aligned voxels — and one at Chebyshev radius 3 takes up
   to 9 hops, well outside what 3 iterations can cover.
2. **First-occupied-neighbor, not nearest-color.** The inner loop breaks on the
   first neighbor with `alpha != 0`. There is no distance comparison; whichever
   neighbor happens to be checked first wins.
3. **Boundary skip.** The triple loop is `1..N-2`, so the outer 1-voxel shell
   is never written to.

Doc updated:

- Implementation Stages step 6 reworded to "3 iterations of 6-neighbor L1
  color dilation; the first occupied neighbor wins the copy. Reaches L1
  distance 3 — covers the conservative band for any voxel that lies on an
  axis-aligned path to a seed."
- Limitations noted explicitly: "boundary voxels (`x/y/z == 0` or `N-1`) are
  not touched by the dilation; for the current normalized OBJ placement this
  band is empty, but if a future scene places geometry at the volume edge the
  dilation must extend to the borders."
- Future-work bullet: "if Step 3 mode-0 validation shows visible black seams
  or color bleeding in the band, upgrade to a real nearest-seed propagation
  (carry argmin through the EDT sweeps); 'enough' is a runtime claim, not a
  static one."

---

### F5 — "No shader changes" is conditional (Medium, doc fix)

You're right that I kept the unconditional wording in two places. The
conservative-band design **should** make the existing thresholds hit, but
that's a hypothesis until Step 3 puts a primary ray on an OBJ surface.

`sponza_sdf_step2_impl.md` "Why no shader changes" section rewritten as:

> The conservative-band subtraction is a *design intent* to land on the
> existing thresholds (`EPSILON = 1e-6` in `raymarch.frag:430`,
> `0.002` in `radiance_3d.comp:243`). The bake itself proves nothing about
> rendering correctness. Step 3's verification gates this:
>
> 1. Mode 0 (final raymarch) actually shades OBJ surfaces (not skips/blacks).
> 2. Mode 1 (normals) produces sensible outward-facing normals on the band.
> 3. `radiance_3d.comp` reports non-zero radiance from OBJ surfaces.
>
> If any of those fail with `uUseAnalyticSDF = 0`, the fallback ladder is:
> widen `surfaceRadius`, more dilation iterations, and only as a last resort
> change shader thresholds.

Added an explicit verification block in the impl note for these three gates
(deferred until Step 3 lands).

---

### F6 — CLI + line numbers (Low, code + doc fix)

**CLI fix.** `src/main3d.cpp:178-191` now rejects unknown names with a
visible error and exit code 1, instead of silently mapping to Cornell:

```cpp
if (!loadObjName.empty()) {
    std::string path;
    if (loadObjName == "sponza")       path = "res/scene/sponza.obj";
    else if (loadObjName == "cornell") path = "res/scene/cornell_box.obj";
    else {
        std::cerr << "[MAIN] --load-obj=" << loadObjName
                  << ": unknown name (expected 'sponza' or 'cornell'). Aborting.\n";
        delete demo;
        CloseWindow();
        return 1;
    }
    if (!demo->loadOBJMesh(path)) {
        std::cerr << "[MAIN] --load-obj failed for " << path << "\n";
    }
}
```

Verified: `--load-obj=bogus` exits 1 with the expected stderr line.

**Line numbers.** Doc updated to cite the corrected positions:

- `--load-obj` validation now `src/main3d.cpp:178-191`
- `--exit-frames` exit hook now `src/main3d.cpp:248-251` (was previously cited
  at `218-222`; that range was the `--auto-analyze` block)
- `edt1d()` now `src/demo3d.cpp:48-99` (signature changed)
- `Demo3D::generateMeshSDF()` now `src/demo3d.cpp:1313-1442` (scratch added)
- Test scaffold now `src/demo3d.cpp:4307-4315`

---

### Summary

| Finding                                  | Sev    | Action          | Result                        |
|------------------------------------------|--------|-----------------|-------------------------------|
| F1 `meshSDFReady` stale                  | High   | Code fix        | Scaffold no longer flips flag |
| F2 Verification overclaimed              | High   | Doc fix         | Status reworded               |
| F3 Per-line heap alloc                   | Medium | Code + doc fix  | Scratch passed in; ~5ms shave |
| F4 Albedo dilation overclaimed           | Medium | Doc fix         | L1/first-neighbor wording     |
| F5 "No shader changes" hypothesis        | Medium | Doc fix         | Made conditional + gated      |
| F6 CLI + line numbers                    | Low    | Code + doc fix  | Reject unknown + cite fixed   |

Build clean, zero new warnings. Sponza/Cornell re-verified clean
(`tools/app_run_step2_sponza_v2.log`, `tools/app_run_step2_cornell_v2.log`).
The bake is now a smoke-tested checkpoint with an honest scope statement;
render-path correctness moves into Step 3's verification.
