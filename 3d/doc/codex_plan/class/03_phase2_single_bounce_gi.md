# 03 Phase 2: One Probe Grid, One Indirect-Light Answer Per Probe

Phase 2 asked the simplest GI question:

Can we store one indirect-light answer per probe and make the final image brighter and more believable?

## The Phase 2 idea

Put a `32 x 32 x 32` grid of probes in the room.

For each probe:

1. fire several rays
2. raymarch through the SDF
3. if a ray hits a surface, shade that surface
4. average the ray results
5. store the average color in a 3D texture

Then, when the final renderer hits a surface, it samples that texture to get indirect light.

This is the first usable GI loop in the branch.

## What Phase 2 proved

It proved three important things:

1. The SDF and surface shading path could be reused for probes.
2. A probe grid could be baked by a compute shader.
3. The final image could consume probe GI through `probeGridTexture`.

Without this, later cascade work would just be architecture on paper.

## The main limitation

This Phase 2 probe is isotropic.

That means:

- every direction seen from the probe is averaged together
- the probe stores one color, not a directional distribution

Why this matters:

If a probe is near a red wall on one side and a green wall on the other, an isotropic probe cannot keep those directions separate. It only remembers the average.

This is acceptable as a first proof of life, but it guarantees later quality limits.

## Why Phase 2.5 followed

Once the single-grid proof worked, several support issues became obvious:

- albedo needed to be wired cleanly
- direct-light shadow checks at probe hits mattered
- debug views were needed to see what the probe texture contained
- UI/runtime wiring needed cleanup

That work prepared the branch for multiple cascades instead of just one level.

## The dot that connects Phase 2 to Phase 3

Phase 2 gave a local answer:

"A probe can tell me about nearby light."

It did not solve:

"How does a probe cheaply learn about farther light?"

That is why Phase 3 exists.
