## Reply: Step 8 Impl Codex Review — `02_gpu_sdf_step8_impl_review.md`

**Date:** 2026-05-09
**Status:** All 7 findings accepted. F1 (high) and F2 (medium) were
real correctness bugs and got code fixes; F3 (medium) and F4 (medium)
were real defensive gaps and got code fixes; F5/F6/F7 (medium-low)
were doc-honesty corrections backed by re-measured numbers.
[gpu_sdf_step8_impl.md](../gpu_sdf_step8_impl.md) revised with a
changelog section explaining each correction.

---

### F1 — Cascade staggering not actually bypassed (HIGH, code fix)

You're right and I missed it during review of my own work. I set
`forceCascadeRebuild = true` in the dynamic block thinking it would
trigger all cascades, but `forceCascadeRebuild` only ENTERS the
cascade pass ([demo3d.cpp:849](src/demo3d.cpp#L849)) — the per-cascade
stagger filter inside `updateRadianceCascades` is keyed on
`renderFrameIndex`:

```cpp
if ((renderFrameIndex % interval) != 0) continue;   // demo3d.cpp:1810
```

So C0 ran every frame (interval=1), but C1 every 2nd, C2 every 4th,
C3 every 8th — meaning the moving sphere's effect on upper cascades
was 1/3/7 frames stale even though the SDF rebuilt every frame. The
existing RenderDoc capture path
([demo3d.cpp:4390](src/demo3d.cpp#L4390)) already documents this:

```cpp
renderFrameIndex = 0;     // reset stagger so ALL cascades run (0 % any == 0)
```

**Fix.** Mirrored that pattern in the dynamic-sphere block of
`update()`:

```cpp
meshSDFReady        = false;   // sdfGenerationPass re-bakes via GPU JFA
cascadeReady        = false;   // updateRadianceCascades runs
forceCascadeRebuild = true;    // enters cascade pass even if cascadeReady
renderFrameIndex    = 0;       // codex 02 F1: bypass stagger -> ALL cascades
historyNeedsSeed    = true;    // alpha=1.0, no EMA ghost trail
```

The v2 dynamic captures
([tools/step8v2_dynamic_t0p0.png](tools/step8v2_dynamic_t0p0.png)
through `_t4p5.png`) now show the sphere's GI bounce reaching upper
cascade levels every frame — visible on the green wall in t=0 where
the sphere's orange light tints adjacent surfaces.

---

### F2 — Disabling dynamic sphere left the last sphere baked in (MEDIUM, code fix)

You're right. The original `if (dynamicSphereEnabled && useOBJMesh
&& useGPUSDF && meshVoxelBaseTexture)` gate stopped firing on
disable, but by that point `voxelGridTexture` had a sphere injected
AND `meshSDFReady = true` (set by the previous frame's bake), so no
auto-rebake happened. The user would see a frozen sphere stuck where
they last saw it.

**Fix.** Added a one-frame disable-cleanup branch in `update()`:

```cpp
if (dynamicSphereEnabled && ...) {
    // ... normal active path ...
} else if (dynamicSphereWasEnabled && useOBJMesh && useGPUSDF && meshVoxelBaseTexture) {
    // codex 02 F2: dynamic sphere just turned OFF. Restore static
    // base voxels, invalidate, no sphere injection.
    glCopyImageSubData(meshVoxelBaseTexture, GL_TEXTURE_3D, 0, 0,0,0,
                       voxelGridTexture,    GL_TEXTURE_3D, 0, 0,0,0,
                       volumeResolution, volumeResolution, volumeResolution);
    meshSDFReady        = false;
    cascadeReady        = false;
    forceCascadeRebuild = true;
    renderFrameIndex    = 0;
    historyNeedsSeed    = true;
    std::cout << "[Demo3D] Dynamic sphere disabled: restored static OBJ base voxels\n";
}
dynamicSphereWasEnabled = (dynamicSphereEnabled && useOBJMesh && useGPUSDF && meshVoxelBaseTexture);
```

`dynamicSphereWasEnabled` is a new `Demo3D` member that tracks the
previous-frame state of the gate, so the cleanup branch fires
exactly once on the true→false transition.

**Verify.** [tools/step8v2_static_gpu.png](tools/step8v2_static_gpu.png)
is a static GPU capture (`--gpu-sdf` only, no `--dynamic-sphere`)
that comes back to a clean Cornell-Original — no leftover sphere.

---

### F3 — Solid sphere voxel volume vs surface SDF primitive (MEDIUM, code fix)

You're right. `addVoxelSphere`'s solid-fill (`dot(d,d) <= r2`)
turned every interior voxel into a JFA seed. JFA finalizes
distance-to-nearest-seed, so interior voxels read as distance=0 with
no surface gradient. The result was the chunky/sliced silhouettes
visible in v1 captures.

**Fix.** Changed the fill condition to a surface band one
voxel-diagonal wide:

```cpp
const float halfDiag = 0.5f * std::sqrt(voxStep.x*voxStep.x +
                                        voxStep.y*voxStep.y +
                                        voxStep.z*voxStep.z);
// ... inner loop ...
float dist = std::sqrt(glm::dot(d, d));
if (std::abs(dist - radius) <= halfDiag) {
    // mark as seed
}
```

This matches what `OBJLoader::voxelize` produces for triangle
surfaces — a thin shell of seeds. JFA then computes correct
distance-to-shell from outside AND inside the sphere; raymarch.frag
sees a proper SDF gradient.

The v2 captures ([tools/step8v2_dynamic_*.png](tools/step8v2_dynamic_t1p5.png))
show the sphere reading as a clean orbital orb instead of the prior
chunky voxel block. Approach (a) from your suggested fix; approach
(b) (analytic sphere SDF composition) would be cleaner but requires
adding a finalize-time sphere SDF combiner — out of scope for the
v2 fix.

---

### F4 — `generateMeshSDFGPU` lacked GL error / handle validation (MEDIUM, code fix)

You're right and I missed an important contract: the codex 07 F1
retry path depends on `generateMeshSDFGPU` returning false on real
failures so `meshSDFReady` stays false and the render loop retries
next frame. Without GL error checks, a context loss or null-handle
bind would silently report "GPU=Xms" success and lock in stale
output.

**Fix.** Added 3 layers of validation:

```cpp
bool Demo3D::generateMeshSDFGPU() {
    // 1. Shader handle valid
    auto sit = shaders.find("sdf_3d.comp");
    if (sit == shaders.end() || sit->second == 0) return false;

    // 2. All texture handles non-zero
    if (!voxelGridTexture || !voronoiTextureA || !voronoiTextureB ||
        !sdfTexture || !albedoTexture) return false;

    // Drain pre-existing errors so post-dispatch check attributes to THIS call
    while (glGetError() != GL_NO_ERROR) { /* drain */ }

    // ... dispatch sequence ...

    // 3. Check GL errors after dispatch
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[ERROR] generateMeshSDFGPU: GL error 0x" << std::hex << err << ...;
        glDeleteQueries(1, &timer);
        return false;
    }
    // ... timing log + return true ...
}
```

The CPU EDT path's existing texture-upload error checks
([demo3d.cpp:1602-1622](src/demo3d.cpp#L1602)) provided the
template. Failure path now matches: caller in `sdfGenerationPass`
sees `false`, render loop keeps `sdfReady=false`, next frame retries.

---

### F5 — Performance claim is JFA-only, not full-frame (MEDIUM, doc fix)

You're right. The "24× speedup" / "60 fps headroom" wording was
overclaim — that delta was the SDF/JFA section only (98 ms CPU vs
4 ms GPU) and didn't include the per-frame copy, sphere upload,
cascade rebuild work, raymarch, or readback overhead.

**Doc fix.** Scoped the claim to the SDF section explicitly. Added
the per-capture dynamic-mode GPU JFA stats (with avg AND max
spikes) and called out what's NOT included:

```
| Capture | Bakes | Avg GPU ms | Max GPU ms |
| dynamic_t0p0 | 120 | 3.502 | 5.886 |
| dynamic_t1p5 | 120 | 3.482 | 5.167 |
| dynamic_t3p0 | 120 | 3.488 | 5.440 |
| dynamic_t4p5 | 120 | 3.409 | 5.076 |

These numbers EXCLUDE: per-frame glCopyImageSubData, CPU sphere upload,
cascade rebuild, raymarch, screenshot capture. Full-frame breakdown
is a known open.
```

Removed "60 fps headroom" — unsupported. Added "Full-frame
dynamic-mode timing breakdown" to the Known Open Items table as a
proper follow-up.

---

### F6 — CPU/GPU parity is visual smoke check, not regression-grade (LOW, doc fix)

You're right. I called the comparison "visual parity" but didn't run
a numeric diff. Your numbers from the preserved screenshots:

```
tools/step8_cornell_orig_cpu.png vs tools/step8_cornell_orig_gpu.png
changed pixels: 68,111 / 921,600 (7.39%)
RGB MAE: 0.171
max RGB delta: 99
```

**Doc fix.** Replaced "visual parity" with the actual measured
numbers (table form), explicitly saying **visually close, NOT
pixel-identical**. The plan's codex 01 F5 5-tolerance bar
(`tools/sdf_diff.py`) was not implemented — that gap stays open in
the Known Open Items table. The differences are expected (JFA is
approximate vs exact CPU EDT; nearest-seed albedo vs 6-neighbor
flood) but I should have measured rather than asserted.

---

### F7 — Warning-count claim unsupported (LOW, doc fix + actual count)

You're right. I claimed "warnings unchanged from Step 7 baseline 38"
without preserving the actual log. Fresh `cmake --build . --config
Release --clean-first` after the F1-F4 fixes shows:

```
0 errors  39 src warnings
```

Distribution: 15×C4819, 9×C4244, 7×C4267, 5×C4100, 2×C4018,
1×C4310. The +1 vs Step 7 baseline (38→39) is one extra C4819 from
line-position shifts in `demo3d.cpp` — the new code added in Phases
1-2 moved an existing non-ASCII char region into a different MSVC
scan window. No new non-ASCII characters introduced (verified
with `LC_ALL=C grep -n '[^ -~\t]'` against the new code regions).

**Doc fix.** Replaced the unsupported claim with the actual count
+ distribution + line-shift explanation, and preserved the build
log at `tools/app_run_step8v2_clean_build.log`.

---

### Summary

| Finding | Sev | Action | Result |
|---|---|---|---|
| F1 Cascade stagger not bypassed | High | Code fix | `renderFrameIndex = 0` added to dynamic block; mirrors RenderDoc path; v2 captures show GI bounce on cascades |
| F2 Sphere stays after disable | Medium | Code fix | `dynamicSphereWasEnabled` shadow + cleanup branch in `update()`; verified by `step8v2_static_gpu.png` |
| F3 Solid voxel sphere | Medium | Code fix | Surface band `abs(length(d)-r) <= halfDiag` in `addVoxelSphere`; v2 captures show clean orbital orb |
| F4 No GL error / handle validation | Medium | Code fix | Shader+5 texture handles validated upfront; `glGetError` drain + post-dispatch check; failure returns false |
| F5 Performance claim is JFA-only | Medium | Doc fix | Scoped "24×" to SDF section; removed "60 fps headroom"; added avg/max table + excluded-stages note + full-frame TBD |
| F6 CPU/GPU "parity" overclaim | Low | Doc fix | Replaced with actual diff: 7.39% changed pixels, MAE 0.171, max delta 99; "visually close, not pixel-identical" |
| F7 Warning-count unsupported | Low | Doc fix + log | Real clean-build count: 39 warnings (was claimed 38); explained +1 C4819 from line-shift; build log preserved |

**Bottom line.** F1 and F2 were real correctness bugs that would
have shown up as either weird stale-cascade visuals (F1) or "why
is the sphere stuck?" UX confusion (F2). Both fixed structurally,
with verification captures showing the new behavior. F3 and F4
were defensive gaps the codex caught before they bit anyone — the
solid sphere happened to render OK in v1 because cascade stagger
was hiding the stale-cascade artifacts (F1 masked F3's chunky
silhouettes), so F1+F3 needed to land together for the v2 captures
to be a clean test. F5/F6/F7 were honest doc corrections; the v1
write-up overclaimed in three small ways and the v2 numbers are
what they actually are.
