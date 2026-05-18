# Phase 2 Pre-flight: `uDirectionalAtlas` Fetch-Site Audit + Allocation Format Discovery

**Date:** 2026-05-14
**Status:** **Plan §3.3 assumption was wrong — atlas is already RGBA16F.** Phase 2A (atlas allocation `GL_RGB8 → GL_RGBA8`) collapses to zero work. The bit-exact verification checkpoint the user proposed for 2A no longer exists because there is no format change to verify. **Stopping for user input on how to proceed** (skip 2A entirely → 2B; or do 2B as one autonomous chunk; or pause).

**Related plan:** [visibility_phase1.5_and_phase2_plan.md §3.3, §3.8](visibility_phase1.5_and_phase2_plan.md)

---

## Allocation site (the load-bearing finding)

[src/demo3d.cpp:2856-2858](../../../src/demo3d.cpp#L2856-L2858):

```cpp
cascades[i].probeAtlasTexture = gl::createTexture3D(
    cascades[i].dirRes * cascades[i].resolution,
    cascades[i].dirRes * cascades[i].resolution,
    cascades[i].resolution,
    GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
```

Confirmed by additional sites:
- [demo3d.cpp:2871](../../../src/demo3d.cpp#L2871): `probeAtlasHistory` allocated `GL_RGBA16F` (same format, parallel texture for temporal blend)
- [demo3d.cpp:2160](../../../src/demo3d.cpp#L2160): `glBindImageTexture(0, c.probeAtlasTexture, ..., GL_WRITE_ONLY, GL_RGBA16F)` — bake-side image binding
- [demo3d.cpp:2120](../../../src/demo3d.cpp#L2120): `probeAtlasHistory` bound `GL_RGBA16F` at temporal blend

**What this means for Phase 2:**

- **No `GL_RGB8 → GL_RGBA8` migration needed.** The atlas has been 4-channel half-float since Phase 5g (when the directional atlas was first introduced).
- **No memory cost increase.** The "+33%" concern in the plan §3.3 was based on the wrong baseline — the atlas was never 3-channel. Memory stays at exactly its current footprint.
- **2A (format-only sub-commit, "functionally inert", with bit-exact verification checkpoint) is empty work.** There is no allocation change to land in 2A.
- **Bake side already writes vec4.** [radiance_3d.comp:428](../../../res/shaders/radiance_3d.comp#L428) already does `imageStore(oAtlas, atlasTxl, vec4(blended, hit.a))`. The alpha channel is already populated — currently with `hit.a` (the per-bin ray hit distance, used by Mode 4 today).

**The plan's assumption was carried forward from the plan-source documents into both visibility_unified_plan.md and visibility_phase1.5_and_phase2_plan.md without verification.** Critic 04 and critic 07 didn't catch it either — none of the critic reviews actually inspected the allocation code. Filing as a meta-finding: future plans should audit allocation formats at the spec-writing time, not the implementation time.

---

## Render-side fetch-site enumeration

| Site | Code | What it reads |
|---|---|---|
| [raymarch.frag:126](../../../res/shaders/raymarch.frag#L126) | `uniform sampler3D uDirectionalAtlas;` | Sampler declaration. Stays as `sampler3D` for `RGBA16F`. |
| [raymarch.frag:366](../../../res/shaders/raymarch.frag#L366) | `irrad += texelFetch(uDirectionalAtlas, ...).rgb * w;` | `sampleProbeDir` (Mode 0/1/2). Reads `.rgb`, **drops alpha**. |
| [raymarch.frag:403](../../../res/shaders/raymarch.frag#L403) | `irrad += texelFetch(uDirectionalAtlas, ...).rgb * w;` | `sampleProbeDirPerBinOccluded` (Mode 3). Reads `.rgb`, **drops alpha**. |
| [raymarch.frag:444](../../../res/shaders/raymarch.frag#L444) | `vec4 a = texelFetch(uDirectionalAtlas, ...);` | `sampleProbeDirDepthAware` (Mode 4). Reads full `vec4`; uses `a.a` as `hitDist`. |

**Key observation:** the only site that reads alpha is Mode 4. Modes 0/1/2/3 silently drop it. This means:

- For Phase 2's α-as-transparency semantics (where `α = 0` means opaque, `α = 1` means transparent), the **existing Mode 0/1/2/3 paths are unaffected** by the bake-side α derivation change — they ignore alpha.
- **Only Mode 4 breaks** if the bake's α-write changes meaning from "hit distance" to "transparency interval." Mode 4 reads `a.a > 0` as "hit at distance a.a"; with Phase 2B, `a.a` becomes 0 (opaque) or 1 (transparent), and Mode 4's signed-projection test would mis-classify everything.
- This means the plan's "deprecate Mode 4 during 2B verification, delete in 2C" approach **literally breaks Mode 4 the moment 2B lands.** "Deprecated but functional for A/B" isn't possible without preserving the old encoding somewhere.

---

## C++ binding site

[src/demo3d.cpp:2430](../../../src/demo3d.cpp#L2430):

```cpp
glUniform1i(glGetUniformLocation(prog, "uDirectionalAtlas"), 3);
```

Texture unit 3. Selected per render: [demo3d.cpp:2423-2428](../../../src/demo3d.cpp#L2423-L2428) picks `cascades[selC].probeAtlasTexture` or `probeAtlasHistory` depending on temporal-blend mode. No format-related code here; nothing to change.

---

## Bake-side write site

[res/shaders/radiance_3d.comp:428](../../../res/shaders/radiance_3d.comp#L428):

```glsl
imageStore(oAtlas, atlasTxl, vec4(blended, hit.a));   // EMA path
imageStore(oAtlas, atlasTxl, vec4(sanitizeRadiance(rad), hit.a));  // plain path
```

Where:
- `blended` / `sanitizeRadiance(rad)` = RGB radiance computed by the bake
- `hit.a` = per-bin ray hit distance (`> 0` = surface, `== 0` = miss, `< 0` = sky sentinel)

**Phase 2B's bake-side change** would replace the `hit.a` write with the derived transparency α:

```glsl
float alpha = (hit.a > 0.0) ? 0.0 : 1.0;  // 0 = opaque (surface hit), 1 = transparent
imageStore(oAtlas, atlasTxl, vec4(blended, alpha));
```

Plus the cascade-inheritance merge formula change earlier in the same shader (paper's `rad = thisRad + thisAlpha * upperDir.rgb; alpha = thisAlpha * upperDir.a`).

---

## Implications for the plan's three-sub-commit structure

**Original plan §3.8:**
- 2A (format only) — atlas RGB8→RGBA8, audit fetch sites, no semantic change. Verifiable bit-exact.
- 2B (semantic) — bake-side α + interval merge + render-side α-gate.
- 2C (cleanup) — delete deprecated modes.

**Revised plan based on this audit:**

- 2A is **empty** (no format change exists). Skip entirely.
- 2B does: (a) bake-side α derivation + merge formula change in `radiance_3d.comp`; (b) render-side α-gate in `raymarch.frag`'s `sampleProbeDir` (the Mode 0 path); (c) **Mode 4 breaks** (its `hitDist` reading becomes 0/1 binary, signed projection becomes meaningless). Modes 0/1/2/3 still work because they ignore alpha.
- 2C cleanup remains as planned (delete deprecated modes incl. broken Mode 4).

**Net effect on commit shape:** instead of "format change → semantic change → cleanup," it becomes "**semantic change (which breaks Mode 4) → cleanup**." There is no checkpoint between the inert format and the breaking semantic change because the format was never the load-bearing piece.

**Implication for the recently-landed Mode 4 default-flip:** the default is currently Mode 4. The moment 2B lands, the default visual output changes (Mode 4 is broken; would render incorrectly). This needs to be coordinated:

- **Option X**: Land 2B + change default back to Mode 0 (the temporary default until 2C deletes the modes and the new α-gated Mode-0-equivalent is the only path). User would see one render regression then a recovery.
- **Option Y**: Land 2B + simultaneously update Mode 0's `sampleProbeDir` to use the new α-gated path. Default stays at Mode 4 visually-broken but not used; 2C cleanup deletes it.
- **Option Z**: Bundle 2B and 2C into a single commit. No deprecation period. Bigger blast radius but cleaner end-state.

Plan §3.5 implied Option Y but didn't account for Mode 4 actually breaking in 2B. The right answer needs user input.

---

## What I haven't done (deliberately, awaiting user input)

- Not landed any 2A change (because 2A is empty).
- Not started 2B (semantic change). It's coordinated with the Mode 4 default flip and needs the X/Y/Z choice above.
- Not authored the bake-leak test scene (Phase 2 pre-flight #2). That work doesn't depend on 2A/2B sequencing — it could be done in parallel. ~0.5 day estimate stands.
- Not updated the plan doc with this finding. Will do after the user picks a 2B sequencing strategy.

---

## Recommended next message to user

> Atlas is already RGBA16F — the GL_RGB8→RGBA8 plan assumption was wrong. Phase 2A is empty. The remaining work (2B semantic + 2C cleanup) needs to coordinate with the just-landed Mode 4 default because **2B breaks Mode 4** (its `hitDist` reading collapses to binary). Three options:
> - **Option X**: Land 2B + revert default to Mode 0 temporarily until 2C cleanup ships.
> - **Option Y**: Land 2B + update Mode 0's path to use the new α-gated formula simultaneously. Default stays nominally at Mode 4 (broken but unused once Mode 0 path is the new correct default — confusing).
> - **Option Z**: Bundle 2B + 2C as a single commit. Bigger blast radius, cleaner end-state.
>
> Which sequencing? My recommendation: **Option X** — small visible regression for one commit (Mode 0 leaks again briefly) is the simplest revertable step; 2C cleanup ships within hours, restoring the no-leak behaviour as the new permanent default.
