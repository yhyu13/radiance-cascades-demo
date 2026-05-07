# Sponza SDF — Step 2: Felzenszwalb EDT → `generateMeshSDF()` (revised)

**Date:** 2026-05-06 (revised 2026-05-07 per codex review `04_*` / reply `04_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_impl_plan_v2.md`
**Status:** Draft v2 — pending implementation
**Changelog:** v2 — accepted all six findings from `04_sponza_sdf_step2_plan_review.md`.
Field is now described as a conservative voxel UDF (not exact triangle SDF); a
half-voxel-diagonal radius is subtracted at upload time so the existing shader
hit thresholds (`EPSILON`, `0.002`) can land on the band; nearest-color albedo
flood-fill replaces "leave non-surface voxels black"; `generateMeshSDF()` returns
`bool` and validates inputs/outputs/uploads; `edt1d` chosen as a file-scope helper;
performance claim removed pending measurement.

---

## Goal

Convert the OBJ surface voxels stored in `meshVoxelData` (RGBA8, 128³) into a
**conservative unsigned distance field** plus a propagated-color albedo volume.
Upload to `sdfTexture` (R32F, world-space metres) and `albedoTexture` (RGBA8) so
the existing ray marcher and cascade bake can shade the OBJ scene without shader
edits.

---

## Why Felzenszwalb EDT over JFA or Dijkstra

| Method | Distance to seed set | Complexity | Useful for sphere tracing? |
|---|---|---|---|
| 6-conn Dijkstra (v1 plan) | L1 — overestimates diagonals by up to 73% | O(N³ log N) | **No** — overestimate lets ray jump through walls |
| GPU JFA (`sdf_3d.comp`) | Approximate Euclidean | O(N³ log N) | Yes, with safety factor |
| Felzenszwalb separable EDT | **Exact Euclidean to seed voxels** | O(N³) | **Yes, after radius subtraction (see §"Conservative Band")** |

Felzenszwalb EDT is exact relative to the seed set, but the seed set is
voxel **centers** marked by Step 1 — not the original triangles. See the next
section for what that means and how we correct for it.

---

## Conservative Band, Not Exact Triangle SDF

The EDT result is exact distance from each voxel to the nearest **occupied voxel
center**, not to the nearest **triangle surface**. Step 1 marks voxels whose
centers lie within a half-voxel diagonal of a triangle, so the EDT distance can
**overestimate** true triangle distance by up to that radius.

At 128³ in a 4 m volume: `voxelSz = 0.03125 m`, half-diagonal `≈ 0.0271 m`.
A primary ray approaching a wall would read a value that could be ~3 cm too
large — large enough to step **through** thin geometry.

**Fix:** subtract the half-diagonal radius at the final conversion step and
clamp at zero. The output is then a **conservative band**:

- Inside the band (within `surfaceRadius` of any seed): reads ≈ 0.
- Outside the band: reads exact-Euclidean-to-seed minus the radius (always
  ≤ true triangle distance — never overestimates after the subtraction).

This makes the field **safe for sphere tracing** (never overestimates) and
**hittable by the existing shaders** (`EPSILON = 1e-6` at `raymarch.frag:430`,
`0.002` at `radiance_3d.comp:243`) — the trilinear-filtered band region reads
near-zero across multiple texels, so primary and cascade rays land on it.

The result is **not** an exact triangle SDF. A true exact bake would require
preserving nearest **triangle** distance per voxel during the seed stage instead
of binary occupancy — logged as future work.

---

## Resolution Decision

The v2 plan proposed `meshSDFResolution = 64` (a separate constant from
`volumeResolution = 128`). **This plan keeps both at `volumeResolution = 128`.**

Reason: `sdfTexture` and `albedoTexture` are allocated at `volumeResolution = 128`
([demo3d.cpp:1976-1989]). Uploading 64³ via `glTexSubImage3D` to a 128³ texture
only updates the lower-left-front corner; the remaining 7/8 retains stale
analytic SDF values.

Keep both at 128. If the EDT proves slow (>50 ms measured), step down to 64 and
add a texture-resize path then.

---

## New Members — `demo3d.h` (private section)

```cpp
bool                 meshSDFReady  = false;
std::vector<uint8_t> meshVoxelData;   // RGBA8, volumeResolution³, set by loadOBJMesh
```

No `edt1d` declaration in the header — it lives at file scope in `demo3d.cpp`
(per F5: pure function, no class state, keeps `<vector>` out of the header).

```cpp
// Single new private declaration:
bool generateMeshSDF();
```

---

## Implementation

### `edt1d()` — 1D Felzenszwalb parabola sweep (file-scope `static` in `demo3d.cpp`)

Standard lower-envelope algorithm (Felzenszwalb & Huttenlocher, *TPAMI* 2012).

- Input `f[i]`: squared distance to nearest seed in voxel-index units.
  Seed voxels: `f[i] = 0`. Non-seed: `f[i] = INF`.
- Output (in-place): `f[i]` = squared distance from index `i` to nearest seed.
- Output `argmin[i]` (optional second buffer): index of the nearest seed on this
  axis. Used by the albedo propagation path (see below).

```cpp
namespace {
    constexpr float EDT_INF = 1e18f;

    // Pure file-scope helper. Optional argmin output preserved for albedo propagation.
    static void edt1d(std::vector<float>& f, std::vector<int>* argmin, int n) {
        std::vector<int>   v(n, 0);
        std::vector<float> z(n + 1, 0.f);
        std::vector<float> d(n);
        std::vector<int>   a;
        if (argmin) a.assign(n, 0);

        // All-INF row guard (no seed reachable on this axis yet — leave untouched
        // so the next-axis pass can compute a finite distance via a different row).
        bool anyFinite = false;
        for (int i = 0; i < n; ++i) if (f[i] < EDT_INF) { anyFinite = true; break; }
        if (!anyFinite) return;

        int k = 0;
        v[0] = 0;
        z[0] = -EDT_INF;
        z[1] =  EDT_INF;

        for (int q = 1; q < n; ++q) {
            float s;
            while (true) {
                int r = v[k];
                // q != r by construction, so denom is never zero.
                float s_num = (f[q] + float(q) * float(q)) - (f[r] + float(r) * float(r));
                s = s_num / (2.f * float(q - r));
                assert(std::isfinite(s));
                if (s > z[k]) break;
                if (--k < 0) { k = 0; break; }
            }
            ++k;
            v[k]     = q;
            z[k]     = s;
            z[k + 1] = EDT_INF;
        }

        k = 0;
        for (int q = 0; q < n; ++q) {
            while (z[k + 1] < float(q)) ++k;
            float diff = float(q - v[k]);
            d[q] = diff * diff + f[v[k]];
            if (argmin) a[q] = v[k];
        }
        f = std::move(d);
        if (argmin) *argmin = std::move(a);
    }
}
```

### `generateMeshSDF()` — 3-pass separable EDT + albedo flood-fill

```cpp
bool Demo3D::generateMeshSDF() {
    const int   N        = volumeResolution;             // 128
    const int   N2       = N * N;
    const int   N3       = N * N * N;
    const float voxelSz  = volumeSize.x / float(N);      // world-space metres per voxel

    // F4: validate inputs
    if (meshVoxelData.size() != size_t(N3) * 4) {
        std::cerr << "[ERROR] generateMeshSDF: meshVoxelData size mismatch ("
                  << meshVoxelData.size() << " vs " << (size_t(N3) * 4) << ")\n";
        return false;
    }

    // 1. Seed grid + seed count
    std::vector<float> sq(N3, EDT_INF);
    int seedCount = 0;
    for (int i = 0; i < N3; ++i) {
        if (meshVoxelData[i * 4 + 3] > 0) {
            sq[i] = 0.f;
            ++seedCount;
        }
    }
    if (seedCount == 0) {
        std::cerr << "[ERROR] generateMeshSDF: zero seeds — voxelization produced no surface voxels\n";
        return false;
    }

    // 2-4. X / Y / Z separable sweeps (no argmin needed for distance — just for albedo path)
    std::vector<float> rowBuf(N);
    for (int z = 0; z < N; ++z)
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) rowBuf[x] = sq[z*N2 + y*N + x];
        edt1d(rowBuf, nullptr, N);
        for (int x = 0; x < N; ++x) sq[z*N2 + y*N + x] = rowBuf[x];
    }
    for (int z = 0; z < N; ++z)
    for (int x = 0; x < N; ++x) {
        for (int y = 0; y < N; ++y) rowBuf[y] = sq[z*N2 + y*N + x];
        edt1d(rowBuf, nullptr, N);
        for (int y = 0; y < N; ++y) sq[z*N2 + y*N + x] = rowBuf[y];
    }
    for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
        for (int z = 0; z < N; ++z) rowBuf[z] = sq[z*N2 + y*N + x];
        edt1d(rowBuf, nullptr, N);
        for (int z = 0; z < N; ++z) sq[z*N2 + y*N + x] = rowBuf[z];
    }

    // 5. Convert squared-voxel distances to conservative world-space metres (UDF)
    //    F1: subtract half-diagonal so the band is hittable and never overestimates.
    const float surfaceRadius = voxelSz * std::sqrt(3.0f) * 0.5f;
    std::vector<float> sdfData(N3);
    for (int i = 0; i < N3; ++i) {
        float d = std::sqrt(sq[i]) * voxelSz - surfaceRadius;
        sdfData[i] = std::max(0.0f, d);
    }
    // Spot-check finite (cheap)
    if (!std::isfinite(sdfData[0]) || !std::isfinite(sdfData[N3 / 2]) || !std::isfinite(sdfData[N3 - 1])) {
        std::cerr << "[ERROR] generateMeshSDF: non-finite SDF values\n";
        return false;
    }

    // 6. Albedo flood-fill (F3): for each voxel within ~1 voxel of the band,
    //    copy the closest occupied voxel's RGB. Far-interior voxels stay black —
    //    they're never sampled because rays terminate on the band first.
    //
    //    Cheap approach: 3-iteration 6-neighbor dilation of seed colors into
    //    voxels with non-zero alpha-mask but zero color. Bounded radius keeps
    //    cost O(3*6*N3) = O(18*N3).
    std::vector<uint8_t> albedoData = meshVoxelData;        // start with surface colors
    for (int iter = 0; iter < 3; ++iter) {
        std::vector<uint8_t> next = albedoData;
        for (int z = 1; z < N - 1; ++z)
        for (int y = 1; y < N - 1; ++y)
        for (int x = 1; x < N - 1; ++x) {
            int  i = (z*N2 + y*N + x) * 4;
            if (albedoData[i + 3] != 0) continue;            // already has color
            // Sample 6 neighbors — first one with alpha wins
            const int off[6] = { -4, +4, -N*4, +N*4, -N2*4, +N2*4 };
            for (int n = 0; n < 6; ++n) {
                int j = i + off[n];
                if (albedoData[j + 3] != 0) {
                    next[i + 0] = albedoData[j + 0];
                    next[i + 1] = albedoData[j + 1];
                    next[i + 2] = albedoData[j + 2];
                    next[i + 3] = 1;                          // mark as filled (not seed)
                    break;
                }
            }
        }
        albedoData = std::move(next);
    }

    // 7. Upload sdfTexture (R32F)
    glBindTexture(GL_TEXTURE_3D, sdfTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N,
                    GL_RED, GL_FLOAT, sdfData.data());
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[ERROR] generateMeshSDF: sdfTexture upload failed (GL " << err << ")\n";
        glBindTexture(GL_TEXTURE_3D, 0);
        return false;
    }

    // 8. Upload albedoTexture (RGBA8) with propagated colors
    glBindTexture(GL_TEXTURE_3D, albedoTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0, N,N,N,
                    GL_RGBA, GL_UNSIGNED_BYTE, albedoData.data());
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        std::cerr << "[ERROR] generateMeshSDF: albedoTexture upload failed (GL " << err << ")\n";
        glBindTexture(GL_TEXTURE_3D, 0);
        return false;
    }
    glBindTexture(GL_TEXTURE_3D, 0);

    // NOTE: meshSDFReady is set by the caller (Step 3) on success — not here.
    std::cout << "[Demo3D] Mesh SDF: EDT complete, " << N << "^3, "
              << "voxelSz=" << voxelSz << "m, surfaceRadius=" << surfaceRadius << "m, "
              << "seeds=" << seedCount << "\n";
    return true;
}
```

Notes:

- `rowBuf` is preallocated outside the loops (one allocation per axis pass instead
  of per row). Addresses F6's heap-allocation cost concern up front.
- The albedo flood-fill is 3 iterations of 6-neighbor dilation. That covers a
  Chebyshev radius of 3 voxels — enough to fill the conservative band and one
  ring beyond it. Far-interior voxels stay black because rays never reach them.
- If the simple flood-fill leaves visible banding at runtime, the follow-up is a
  full argmin-EDT (carry seed indices through the parabola sweeps), not making
  the flood-fill iterate further.

---

## Distance Type: Conservative UDF

The output is an **unsigned distance field** with a conservative band:

- Within `surfaceRadius` of any marked voxel: reads 0 (or near-0 after trilinear).
- Beyond the band: reads the EDT-to-seed distance minus `surfaceRadius`, never
  larger than true triangle distance.

`raymarch.frag` and `radiance_3d.comp` use the SDF as a sphere-trace step bound
with a safety factor (`* 0.7` and `* 0.9` respectively). A field that never
overestimates is safe.

A true **signed** SDF would need a watertight mesh and an inside/outside test —
deferred. Sponza and Cornell-Box OBJ are surface meshes, not solids.

---

## What Is Deliberately Skipped

| Skipped | Why |
|---|---|
| Full argmin-EDT for exact nearest-color albedo | The 3-iteration flood-fill is simpler and covers the conservative band. Upgrade only if visible banding appears. |
| Signed SDF | Needs watertight mesh + inside/outside test. Future work. |
| 64³ performance path | Adds texture mismatch complexity. Use 128³ and optimize later if measured time exceeds budget. |
| GPU EDT (`sdf_3d.comp`) | Broken internally. CPU EDT is exact and runs once per scene load. |

(Note vs v1: nearest-color albedo propagation is no longer skipped — F3.)

---

## Required Headers (add to `demo3d.cpp` if not present)

```cpp
#include <vector>
#include <cmath>
#include <cassert>
#include <iostream>
#include <utility>
```

---

## Performance — TBD

The asymptotic count is O(N³): 3 passes × 128² rows × O(128) per row ≈ 6.3 M
parabola operations. With preallocated `rowBuf` (no per-row heap allocation),
single-threaded CPU should be well under 50 ms — but this is **not measured yet**.

Verification gate: log `[Demo3D] Mesh SDF: EDT complete` with elapsed time. If
the elapsed time exceeds 50 ms, switch to flat indexed sweeps (no row-extraction
copy at all, work directly on a single `sq` buffer with strided indexing). If
that still exceeds budget, drop to 64³ + texture-resize path.

---

## Verification Checklist

- [ ] Build succeeds; no new warnings
- [ ] `[Demo3D] Mesh SDF: EDT complete, 128^3, voxelSz=0.03125m, surfaceRadius=0.0271m, seeds=NNNNN` in console
- [ ] `seeds` count > 0 for both Cornell Box and Sponza OBJ
- [ ] SDF debug view (press `D`): conservative band visible — zero/near-zero at marked surfaces, smooth gradient outward
- [ ] **Mode 0 (final render)**: OBJ surfaces actually shaded, not black blobs (validates F2 + F3 together)
- [ ] **`radiance_3d.comp`**: cascade output non-zero from OBJ surfaces (validates GI hits the band)
- [ ] No NaN in sdfTexture (mode 5 step counts finite, not zero everywhere)
- [ ] Failure path: temporarily set `seedCount = 0` — confirm `[ERROR]` log and `meshSDFReady` stays false
- [ ] Performance: EDT time logged < 50 ms (otherwise trigger optimization path)
- [ ] Regression: switch back to Cornell Box (analytic) — analytic SDF unchanged

---

## What Is Next (Step 3)

Wire `useOBJMesh` into `sdfGenerationPass()` so `generateMeshSDF()` is called
instead of the analytic shader when an OBJ mesh is loaded. Honor the `bool`
return value (don't set `meshSDFReady` on failure). See revised Step 3 plan.
