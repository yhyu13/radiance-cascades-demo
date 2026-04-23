# Accepted Findings

---

## Finding 1 — Material colors not used in shading (HIGH) ✅ ACCEPTED IN FULL

**The critique is correct.**

The analytic SDF stores per-primitive color (`glm::vec3 color`) and `createCornellBox()` assigns red to the left wall and green to the right wall. But:

- `sdf_analytic.comp` only writes SDF distance to the R32F texture — it discards color entirely. The GPU SSBO struct has `color[4]` but the shader never reads it.
- `raymarch.frag` has no color sampler bound. `surfaceColor = diff * uLightColor + vec3(0.05)` — all surfaces receive the same white-warm tint regardless of which wall was hit.
- `radiance_3d.comp` does the same: `diff * uLightColor + vec3(0.02)` for every hit.

**Consequence:** The current image is a white Cornell box with one warm-white light. There is no red left wall, no green right wall. The Phase 2 toggle can produce a visible brightness change but cannot produce color bleeding. Any indirect contribution is a brightness bias, not GI color transport.

**What I should have done:** Add a second R11G11B10F (or RGBA8) volume for surface albedo that `sdf_analytic.comp` fills and both shading shaders sample. This is the single most important correctness gap.

---

## Finding 2 — No shadow / visibility check from hit point to light (HIGH) ✅ ACCEPTED

**The critique is correct.** Both shading paths use bare Lambertian NdotL with no secondary ray to the light. Interior boxes will appear lit through walls. The cascade stores unshadowed direct illumination, which makes the "indirect" result meaningless as a quality metric — you cannot tell whether a brightness change comes from real transport or from unshadowed leaked light.

**Caveat I keep in mind:** Shadow rays double the compute cost in `radiance_3d.comp` (each probe ray that hits needs a second march to the light). At 32³×4 rays this is 130K secondary marches per dispatch. Acceptable once, but not per-frame. Since the cascade now dispatches only when scene changes, this is feasible.

**What I should do:** Cast a shadow ray in both shaders. See `03_action_plan.md`.

---

## Finding 4 — UI still shows placeholders for SDF and raymarching (MEDIUM) ✅ ACCEPTED IN FULL

The `renderTutorialPanel()` still has:
```
✗ SDF generation (placeholder)
✗ Full raymarching (placeholder)
```

This is wrong. Both are implemented. It will confuse future debugging. Fix is trivial — update those bullet strings.

---

## Finding 5 — `injectDirectLighting()` has dead code after early return (MEDIUM) ✅ ACCEPTED IN FULL

I added `return;` at the top of `injectDirectLighting()` but left ~80 lines of old implementation unreachable. This is sloppy. The old code should either be deleted or wrapped in `#if 0` with a comment explaining why it was disabled. Dead code in a lighting path increases maintenance friction.

---

## Finding 8 — Plan wording stronger than evidence (LOW) ✅ ACCEPTED IN FULL

`phase2_changes.md` says "Implemented" in the header and lists completed items with ✅. The toggle test is correctly marked ⬜. But the surrounding confidence level reads as if the visual result is known to be correct. It is not. I was writing about code correctness, not visual correctness. Those are different claims.

The correct phrase for both phases is: **"implemented, build-verified, runtime-plausible, visually unconfirmed"** — which is what the codex critic settled on, and which I should adopt.
