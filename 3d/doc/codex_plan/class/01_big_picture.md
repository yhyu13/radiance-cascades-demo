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

The main conceptual jump is this:

- Early phases store one average light color per probe.
- Phase 5 stores one light color per direction per probe.

Everything before Phase 5 exists so Phase 5 has a stable base to build on.

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
- an optional directional-GI path that reads the C0 atlas directly
- an optional soft-shadow approximation

So by the end of Phase 5, both systems matter.

## The shortest mental model

Use this model if you get lost:

1. The room is turned into an SDF and albedo volume.
2. Probes are scattered through the room.
3. Each probe fires rays into the room.
4. Different cascade levels cover different ray-distance bands.
5. Higher cascades fill in the far-field answer for lower cascades.
6. Phase 5 upgrades that fill-in from "one average color" to "the color for this exact direction."
7. The final renderer can now either read the isotropic reduced grid or a directional C0-atlas-based GI answer.

Everything else is optimization, debugging, or cleanup around that core.
