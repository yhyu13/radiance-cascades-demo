# Phase 5a Implementation Learnings — Octahedral Direction Encoding

**Date:** 2026-04-24  
**Branch:** 3d  
**Status:** Complete  
**Follows:** `phase5_plan.md` + Codex critic `codex_critic_phase5/01_phase5_plan_review.md` + `claude_reply/reply_phase5_plan_review.md`

---

## What Was Implemented

### 1. Octahedral direction functions in `radiance_3d.comp`

Four functions added at `res/shaders/radiance_3d.comp:68–95`, replacing the removed `getRayDirection()` Fibonacci function:

```glsl
vec2 dirToOct(vec3 dir)          // full-sphere unit vector → [0,1]²
vec3 octToDir(vec2 uv)           // [0,1]² → full-sphere unit vector
ivec2 dirToBin(vec3 dir, int D)  // direction → bin index [0,D)²
vec3 binToDir(ivec2 bin, int D)  // bin index → bin-center direction
```

The octahedral parameterization is standard (L1 normalized, lower hemisphere folded outward). The round-trip property holds: `dirToBin(binToDir(b, D), D) == b` for all valid b.

### 2. `uniform int uDirRes` added to shader

`res/shaders/radiance_3d.comp:41`: replaces `uRaysPerProbe` as the loop bound. D=4 (16 bins per probe) is the current value. The uniform makes D hot-patchable at runtime without recompilation.

### 3. `uRaysPerProbe` retired from shader

`uniform int uRaysPerProbe` removed from the shader uniform block. The Fibonacci `getRayDirection(int idx)` function that depended on it is removed. The C++ push `glUniform1i(... "uRaysPerProbe", ...)` is removed from `updateSingleCascade()`.

The C++ member `baseRaysPerProbe` and its UI slider remain untouched — the slider is now cosmetically non-functional for the direction count but not worth removing mid-phase.

### 4. Main loop changed from Fibonacci to octahedral bins

**Before (Phase 4):**
```glsl
for (int i = 0; i < uRaysPerProbe; ++i) {
    vec3 rayDir = getRayDirection(i);   // Fibonacci sphere
    ...
}
totalRadiance /= float(uRaysPerProbe);
```

**After (Phase 5a):**
```glsl
for (int dy = 0; dy < uDirRes; ++dy) {
    for (int dx = 0; dx < uDirRes; ++dx) {
        vec3 rayDir = binToDir(ivec2(dx, dy), uDirRes);  // octahedral bin center
        ...
    }
}
totalRadiance /= float(uDirRes * uDirRes);
```

The nested loop structure matches the D×D atlas tile layout that Phase 5b will write. No other logic in `main()` changed — `upperSample`, `blendWidth`, hit-type branches, and packed-hit-count encoding are all unchanged.

### 5. `dirRes` member added to `demo3d.h` / `demo3d.cpp`

- `src/demo3d.h:659`: `int dirRes;` with doc comment "5a: Octahedral direction bin resolution. D^2 rays per probe. Default 4 (16 bins)."
- `src/demo3d.cpp:100`: `, dirRes(4)` in constructor initializer list
- `src/demo3d.cpp:922`: `glUniform1i(glGetUniformLocation(prog, "uDirRes"), dirRes);` pushed in `updateSingleCascade()`

---

## Key Learnings

### Fibonacci → octahedral: what changes and what doesn't

The Fibonacci sphere samples D² approximately-uniform directions chosen for low discrepancy. Octahedral bins also produce approximately-uniform directions, but the bin centers are structured: each `(dx, dy)` pair maps to a unique direction, and the mapping is analytically invertible. This invertibility is the only reason for switching — Phase 5b needs to write atlas texels at specific (dx, dy) positions, and Phase 5c needs to read the exact same (dx, dy) for a given ray direction. Neither is possible with Fibonacci.

The visual quality difference between Fibonacci and octahedral at D=4 is small. Both distribute 16 directions across the sphere. Octahedral bins have mild solid-angle non-uniformity (the poles are slightly over-sampled in the L1 projection), which was noted and accepted in the Codex reply.

### The merged probe stores an isotropic average — still

Phase 5a does NOT change what is stored in `probeGridTexture`. The output is still `vec4(totalRadiance / D², packedHits)` — a single direction-averaged value per probe. The per-direction atlas comes in Phase 5b. Phase 5a is purely an input change (how rays are generated); the output path is unchanged.

This means the rendered image with Phase 5a should look essentially identical to Phase 4. The GI quality change is not visible until Phase 5b+5c supply per-direction storage and directional merge.

### `uRaysPerProbe` removal is clean — no sentinel values broken

The packed hit count `float(surfaceHits) + float(skyHits) * 255.0` is still computed correctly. `surfaceHits` and `skyHits` count individual ray outcomes from the nested loop. With D=4, both saturate at 16 (never reach the 255/256 RGBA16F precision boundary). The RGBA16F precision issue documented in Phase 4e (skyH ≥ 9 → corrupt packed value) is now less likely to trigger at D=4 with env fill ON (max skyH = 16, which at 16×255=4080 exceeds 2048 — still potentially imprecise, but C3 at base=8 was the main concern; that slider is no longer meaningful since D fixes ray count at 16).

### Merge path untouched — isotropic `upperSample` still in play

The Phase 4c blend logic and `upperSample = texture(uUpperCascade, uvwProbe).rgb` are both still active. For Phase 5a, miss rays still pull the same isotropic probe average from the upper cascade. The directional merge that makes this correct is Phase 5c. Phase 5a is forward-compatible: the per-direction atlas that Phase 5b introduces will be read in Phase 5c using the same `(dx, dy)` bin indices that Phase 5a now generates.

---

## Validation

**Build:** MSBuild Debug, 0 errors, 0 new warnings. Pre-existing warnings (C4819 charset, C4100 unused params, C4702 unreachable in frozen inject_radiance path) are unchanged.

**Runtime validation required (user):**
- Launch demo, GI ON, Mode 0 or Mode 6
- Image should look visually equivalent to Phase 4 baseline (same energy, same GI bleed pattern) — 5a only changes direction sampling, not storage or merge
- Per-cascade mean-lum readback in the Cascade panel should be stable (within noise vs Phase 4)
- Ray count shown in stats should reflect D²=16 rays per probe for all cascades

---

## Pre-conditions for Phase 5b

- `binToDir(ivec2(dx,dy), uDirRes)` is called with `dx ∈ [0, uDirRes)`, `dy ∈ [0, uDirRes)` — the (dx, dy) pair is now a well-defined atlas texel address
- `uDirRes` is already pushed as a uniform from C++
- The nested loop structure matches the atlas tile layout: `imageStore(oAtlas, ivec3(probePos.x*D+dx, probePos.y*D+dy, probePos.z), ...)` can be dropped directly into the inner loop body in Phase 5b

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/radiance_3d.comp` | Removed `getRayDirection` (Fibonacci); removed `uRaysPerProbe` uniform; added `uDirRes` uniform + 4 octahedral functions; changed loop to `for dy / for dx` with `binToDir` |
| `src/demo3d.h` | Added `int dirRes;` member |
| `src/demo3d.cpp` | `, dirRes(4)` in constructor; `glUniform1i(... "uDirRes", dirRes)` in `updateSingleCascade()`; removed `uRaysPerProbe` push |
