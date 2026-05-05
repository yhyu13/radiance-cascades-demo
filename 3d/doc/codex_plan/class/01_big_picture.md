# 01 Big Picture

This project is trying to answer one question:

How can we render soft indirect lighting in a 3D room without tracing huge numbers of rays per screen pixel every frame?

The branch's answer is:

1. Precompute lighting at many sample points in the room.
2. Store those samples in textures.
3. Let the final renderer look up the precomputed answer instead of solving the whole lighting problem at every pixel.

The sample points are called probes.

The hard part is that one probe cannot cheaply see both:

- tiny nearby details
- very far lighting

So the branch uses cascades:

- C0 handles nearby light
- C1 handles a bit farther
- C2 handles mid-range
- C3 handles far range

Then it merges them so C0 ends up carrying the full answer.

That gives the branch a natural phase progression:

1. Phase 1: get the scene, SDF, and direct rendering stable
2. Phase 2: prove that one probe grid can store useful indirect light
3. Phase 3: stack 4 cascades so probes can cover near and far distances
4. Phase 4: clean up obvious quality and debugging problems
5. Phase 5: stop treating all directions the same, because that isotropic shortcut is the main remaining quality limit
6. Phase 6: make visual and GPU-pipeline diagnosis reproducible with screenshots and RenderDoc
7. Phase 7-8: add stronger diagnostic views and live directional-resolution control
8. Phase 9-10: use temporal accumulation, jitter, history clamping, and staggered updates to trade time for smoother probe results
9. Phase 12-14: make capture workflows richer, tune temporal/blur defaults, and fix C0/C1 coverage gaps with minimum ray reach

The main conceptual jump is this:

- Early phases store one average light color per probe.
- Phase 5 stores one light color per direction per probe.
- Later phases stop changing the basic transport model as much and focus on making it observable, smoother, and more stable over time.

Everything before Phase 5 exists so Phase 5 has a stable base to build on. Everything after Phase 5 mostly exists to diagnose, smooth, and tune that directional-cascade renderer.

## What the final image path does

At a high level:

1. The code raymarches the visible surfaces of the Cornell Box.
2. At a surface hit, it computes direct light from the point light.
3. It samples precomputed indirect light from the probe system.
4. It combines direct + indirect and tone-maps to the screen.

Important point:

The final image does not directly run all the cascade logic. The cascade system runs earlier and fills textures. The final image only reads the result.

That is why the branch can accumulate a lot of jargon. There are really two systems:

- the probe baking system
- the final screen rendering system

Most of the complex Phase 3-5 work is happening in the probe baking system.

But in the latest codebase the final screen renderer also has important Phase 5 logic:

- a hard shadow ray for direct light
- an optional directional-GI path that reads the selected cascade's atlas directly
- an optional soft-shadow approximation
- an optional GI blur pass that blurs the indirect term while preserving depth and normal edges

So in the current codebase both systems matter:

- the probe bake produces atlas/grid textures, and can accumulate them over time
- the final renderer chooses how to consume those textures, isolate debug views, and optionally blur the indirect result

## The shortest mental model

Use this model if you get lost:

1. The room is turned into an SDF and albedo volume.
2. Probes are scattered through the room.
3. Each probe fires rays into the room.
4. Different cascade levels cover different ray-distance bands.
5. Higher cascades fill in the far-field answer for lower cascades.
6. Phase 5 upgrades that fill-in from "one average color" to "the color for this exact direction."
7. The final renderer can now either read the isotropic reduced grid or a directional atlas-based GI answer.
8. Temporal accumulation and jitter repeatedly rebake nearby probe positions and blend them into history.
9. Debug/capture tools let the branch compare final, indirect, GI-only, probe-boundary, and GPU-pipeline views instead of guessing from one image.

Everything else is optimization, debugging, or cleanup around that core.

## Current defaults to know

The constructor defaults now bias toward the more advanced path:

- non-co-located cascades are on
- directional merge, directional bilinear, spatial trilinear, direct shadow rays, and directional GI are on
- temporal accumulation, history clamp, probe jitter, staggered updates, and GI blur are on
- `dirRes=8`, and scaled D uses `min(16, dirRes << cascadeIndex)`
- C0 and C1 both have `1.0` world-unit minimum ray reach
- environment fill and soft-shadow approximations are off

Some older UI/help text still reflects earlier defaults, so treat `Demo3D::Demo3D()` as the source of truth for current startup behavior.
