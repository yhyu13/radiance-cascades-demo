# Sponza SDF — Step 2 v2: Implementation Notes (revised)

**Date:** 2026-05-07 (revised after codex review `06_*` / reply `06_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_step2_plan.md` (v2)
**Status:** **Implemented and bake-smoke-tested.** `generateMeshSDF()` bakes
and uploads for Sponza and Cornell OBJ without reporting upload errors.
Render-path validation (mode 0 hits, normals, GI injection) is **pending Step 3**.
**Changelog (vs original):** F1 lifecycle fix — scaffold no longer flips
`meshSDFReady`; F3 zero-heap-allocation EDT — scratch passed into `edt1d()`;
F6 CLI rejects unknown `--load-obj` names; F2/F4/F5 wording corrected.

---

## Summary

| Item | Value |
|---|---|
| New file-scope helper | `edt1d()` in `demo3d.cpp` (anonymous namespace) — takes scratch buffers by reference |
| New member function | `Demo3D::generateMeshSDF()` returns `bool` |
| New members (`demo3d.h`) | `std::vector<uint8_t> meshVoxelData`, `bool meshSDFReady` |
| New CLI flags (`main3d.cpp`) | `--load-obj=cornell\|sponza` (rejects others), `--exit-frames=N` |
| Test scaffold | `loadOBJMesh()` populates `meshVoxelData` and calls `generateMeshSDF()` once for smoke-test; **does NOT flip `meshSDFReady`** (Step 3 is the only allowed setter) |
| Build status | Clean, zero new warnings |
| Bake smoke test | Sponza 37,757 seeds, Cornell 40,878 seeds — both bake without `[ERROR]` from `generateMeshSDF()` |
| Render correctness | **Untested.** Step 3 wiring required. |

---

## Files Touched

- `src/demo3d.h:625-635` — added `meshVoxelData`, `meshSDFReady`, `generateMeshSDF()` declaration
- `src/demo3d.cpp:31-33` — added `<cassert> <utility> <limits>` includes
- `src/demo3d.cpp:48-99` — file-scope `edt1d()` (takes scratch v/z/d by reference, zero per-call allocations)
- `src/demo3d.cpp:1313-1442` — `Demo3D::generateMeshSDF()` (preallocates rowBuf + scratchV/Z/D once, then 3 separable sweeps + conservative-band conversion + 3-iter L1 albedo dilation + GL upload with error checks)
- `src/demo3d.cpp:4307-4315` — Step 2 test scaffold in `loadOBJMesh()` (populate `meshVoxelData`, smoke-call `generateMeshSDF()`, do not set `meshSDFReady`)
- `src/main3d.cpp:148-176` — `--load-obj=NAME` and `--exit-frames=N` flag parsing
- `src/main3d.cpp:178-191` — name validation (rejects unknown names, exit 1)
- `src/main3d.cpp:248-251` — frame-counter exit hook in main loop

---

## `edt1d()` — file-scope 1D Felzenszwalb sweep (zero-alloc)

Standard lower-envelope parabola algorithm (Felzenszwalb & Huttenlocher, *TPAMI* 2012).
Operates in-place on a row of squared distances (`0` at seeds, `EDT_INF` elsewhere).
Caller supplies scratch buffers (`v.size() >= n`, `z.size() >= n+1`, `d.size() >= n`)
so the helper makes **zero heap allocations per call** — important because
`generateMeshSDF()` calls it 49,152 times at N=128.

Notable choices:
- **All-INF row guard at the top** — leaves untouched rows for the next axis pass to populate.
- **No `denom == 0` guard** — `q != v[k]` by construction, so the denominator can't be zero.
  The previous draft's check was a misdiagnosis (per codex review F5 of the plan).
- **File-scope helper, not a class member** — pure function with no `Demo3D` state.
- **Explicit copy-back loop** at the end (cannot `std::move` because caller reuses `d` across rows).

```cpp
namespace {
    constexpr float EDT_INF = 1e18f;
    static void edt1d(std::vector<float>& f, int n,
                      std::vector<int>& v, std::vector<float>& z, std::vector<float>& d) {
        bool anyFinite = false;
        for (int i = 0; i < n; ++i) if (f[i] < EDT_INF) { anyFinite = true; break; }
        if (!anyFinite) return;
        // ... standard parabola sweep, writing into d ...
        for (int i = 0; i < n; ++i) f[i] = d[i];
    }
}
```

---

## `Demo3D::generateMeshSDF()` — 3-pass EDT + conservative band + albedo dilation

### Stages

1. **Validate** `meshVoxelData.size() == N3 * 4`. Return `false` on mismatch.
2. **Seed** `sq[N3]`: `0` where alpha > 0, `EDT_INF` elsewhere. Count seeds; return `false` if zero.
3. **Preallocate scratch** once: `rowBuf(N)`, `scratchV(N)`, `scratchZ(N+1)`, `scratchD(N)`.
4. **3 separable sweeps** (X, Y, Z) over `sq`. Each row is extracted into `rowBuf`, EDT'd in place, written back.
5. **Conservative band conversion**:
   ```cpp
   const float surfaceRadius = voxelSz * std::sqrt(3.0f) * 0.5f;
   sdfData[i] = std::max(0.0f, std::sqrt(sq[i]) * voxelSz - surfaceRadius);
   ```
   This is the load-bearing step. The EDT is exact only against voxel **centers**;
   without subtracting `surfaceRadius` it would overestimate true triangle distance
   by up to ~0.027 m (at 128³ in a 4 m volume) and the existing shader hit thresholds
   would never land on OBJ surfaces. **Whether they actually do land** is a Step 3 verification.
6. **Spot-check** finite-ness on `[0]`, `[N3/2]`, `[N3-1]`. Return `false` on non-finite.
7. **Albedo L1 dilation** — 3 iterations of strict 6-neighbor copy-from-first-occupied-neighbor.
   Reaches L1 (Manhattan) distance 3, **not Chebyshev radius 3**. Whichever neighbor is checked
   first wins the copy; this is **first-occupied-neighbor**, not nearest-color. Boundary voxels
   (`x|y|z == 0` or `N-1`) are not touched — fine for current normalized OBJ placement, but
   if a future scene puts geometry at a volume edge the dilation must extend to borders.
8. **Upload** `sdfTexture` (R32F) and `albedoTexture` (RGBA8). `glGetError()` after each upload;
   return `false` on GL error (with hex dump).
9. **Log** `[Demo3D] Mesh SDF: EDT complete N=128 voxelSz=0.03125m surfaceRadius=0.0270633m seeds=NNNNN edt=Xms albedo=Yms`.

`meshSDFReady` is **never set inside `generateMeshSDF()`**. Step 3's mesh branch in
`sdfGenerationPass()` is the only allowed setter. Lifecycle invariants:

| Trigger | `meshSDFReady` |
|---|---|
| `loadOBJMesh()` succeeds | `false` (forces rebake on next frame) |
| `setScene()` (analytic) | `false` (mesh state discarded) |
| `generateMeshSDF()` returns `false` | not flipped — stays as-was |
| Step 3 mesh branch + `generateMeshSDF()` returns `true` | `true` (set by caller) |
| Step 2 test scaffold (current code) | always `false` (analytic overwrites SDF anyway) |

---

## Bake Smoke Test

Both OBJs verified with the headless CLI:

```powershell
.\build\RadianceCascades3D.exe --load-obj=sponza  --exit-frames=60
.\build\RadianceCascades3D.exe --load-obj=cornell --exit-frames=60
```

| Mesh    | Vertices | Faces   | Voxel seeds | EDT (ms) | Albedo (ms) | `generateMeshSDF()` GL/upload errors |
|---------|----------|---------|-------------|----------|-------------|--------------------------------------|
| Sponza  | 145,185  | 262,267 | 37,757      | 66.32    | 29.11       | none                                 |
| Cornell | 64       | 32      | 40,878      | 64.68    | 28.30       | none                                 |

**Math check:** `voxelSz = 4 / 128 = 0.03125 m`. `surfaceRadius = 0.03125 × √3 / 2 = 0.0270633 m`. ✓

**What the smoke test does NOT prove:**

- The bake **runs**, but `sdfTexture` is overwritten by the analytic SDF compute pass on the
  next frame — visible in both logs as `[Demo3D] Analytic SDF generation complete.` after
  the bake line. That's expected; Step 3's `sdfGenerationPass()` mesh branch is what stops it.
- Both logs contain the pre-existing `[GL Error] Compute shader compilation failed` for
  `res/shaders/sdf_3d.comp`. This is **not a Step 2 regression** — the file was already
  broken; the CPU EDT path doesn't use it. Documented in `.wolf/cerebrum.md`.
- No primary-ray hit, no normal sample, no GI injection from the OBJ surface has been
  observed yet. All three are Step 3 verification gates.

Logs preserved at `tools/app_run_step2_sponza_v2.log` and `tools/app_run_step2_cornell_v2.log`
(UTF-16 LE — read with `[System.IO.File]::ReadAllText(path, [System.Text.Encoding]::Unicode)`).

---

## Performance Notes

EDT ~65 ms at N=128. Codex F3 surfaced that my original "no per-row heap allocation"
claim was wrong: `rowBuf` was preallocated but `edt1d()` itself allocated three vectors
per call (49,152 × 3 = ~147K allocations per bake). After F3 fix (scratch passed in,
zero allocations in the EDT inner loop), the saving is ~3-6 ms per bake. The remaining
cost is the row extract / sweep / writeback, dominated by the EDT inner parabola loop.

Still over the 50 ms budget. Optimization path (deferred until Step 3 lands and total
scene-load latency is measured against an actual user-visible budget):

1. Flat indexed sweeps — drop `rowBuf` extraction, work directly on `sq` with strided indexing.
2. SIMD the parabola sweep (probably not worth it for a one-shot bake).
3. Multi-thread the X/Y/Z sweeps independently.

---

## Step 3 Verification Gates (deferred)

The "no shader changes" claim is **conditional**. These gates remain to be checked
once the Step 3 mesh branch + UI gate land:

- **Mode 0 (final raymarch):** OBJ surfaces actually shaded, not skipped/black.
  This is the threshold check (`dist < EPSILON = 1e-6` at `raymarch.frag:430`).
- **Mode 1 (normals):** outward-facing sensible normals on the band — finite-difference
  normal estimation must be stable on the conservative-band UDF.
- **Cascade GI (`radiance_3d.comp:243`):** non-zero radiance from OBJ surfaces.
- **Albedo:** band samples don't trilinear-blend to black. If they do, upgrade
  the dilation to nearest-seed propagation (carry argmin through the EDT sweeps).

Fallback ladder if any gate fails: widen `surfaceRadius` (currently `voxelSz × √3 / 2`)
to a full diagonal; more albedo dilation iterations; only as a last resort, change
shader thresholds.

---

## Architecture Decisions

**File-scope `edt1d` over class member.** Pure function, no `Demo3D` state, no header
exposure of `<vector>`. Codex review F5 of the plan surfaced the inconsistency between
v1's header declaration and the file-scope snippet; chose file-scope.

**`generateMeshSDF()` returns `bool`.** All failure paths (size mismatch, zero seeds,
non-finite, GL error) return `false`. The caller — Step 3's mesh branch — owns the
`meshSDFReady` lifecycle. This keeps the success/failure invariant in one place
instead of split between bake and render loop.

**Scratch buffers passed into `edt1d`.** Codex F3. Allocating once per `generateMeshSDF()`
call instead of per row eliminates ~147K small heap allocations per bake. The minor cost
is that `edt1d()` cannot `std::move(d)` into `f` at the end (the caller still owns `d`),
so it does an explicit copy-back.

**Test scaffold does not set `meshSDFReady`.** Codex F1. The flag means "the current
mesh SDF is in `sdfTexture`". Setting it from the scaffold made it a lie because the
analytic SDF compute pass overwrites the texture on the next frame. The flag is now
write-once from one place: Step 3's mesh branch in `sdfGenerationPass()`.
