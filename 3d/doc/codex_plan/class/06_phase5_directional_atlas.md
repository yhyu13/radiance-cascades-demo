# 06 Phase 5: Directional Storage and Directional Merge

Phase 5 is the major conceptual upgrade of the branch.

Before Phase 5:

- a probe mostly behaved like "one average light color"

After Phase 5:

- a probe can store different answers for different directions

That is the core idea to keep in mind.

## Why isotropic storage was no longer enough

Suppose a probe sits between:

- a red wall on the left
- a green wall on the right

If the probe stores one average value, then left-looking rays and right-looking rays both see the same mixed answer.

That creates two classes of artifacts:

- wrong color bleeding
- banding when cascades hand off between levels using mismatched directional information

Phase 5 exists to stop that collapse.

## Phase 5a: replace free-form ray directions with addressable direction bins

The project moved to octahedral direction encoding.

Why:

- it gives each direction a stable 2D bin address
- those bins can be stored in an atlas tile

That is the bridge from "sample directions" to "store per-direction data."

## Phase 5b: add the atlas

Each probe now owns a `D x D` tile inside a bigger 3D texture.

Example with `D=4`:

- each probe has 16 direction bins
- each tile is `4 x 4`
- the atlas packs those tiles for every probe

So instead of writing one average color directly into `probeGridTexture`, the compute shader writes one color per direction bin into `probeAtlasTexture`.

## Phase 5b-1: reduction pass

The final renderer still expects one averaged value per probe in `probeGridTexture`.

So after writing the directional atlas, a second compute pass averages the bins back down to one isotropic value per probe.

That means the branch keeps two representations:

- atlas for directional baking/merge/debug
- reduced grid for the final raymarch shader

This is an important connecting dot. Phase 5 changed the bake path first, not the final image sampling path.

## Phase 5c: directional merge

This is the payoff step.

Old behavior:

- when a lower cascade missed, it sampled one isotropic value from the upper probe

New behavior:

- when a lower cascade missed in direction D, it sampled the upper cascade's stored answer for direction D

This is why the atlas exists.

Without Phase 5c, the atlas would just be extra storage.

## Phase 5g, 5h, and 5i: the final renderer catches up

The first half of Phase 5 changed how probes are baked.

The second half changed how the final screen shader uses that information:

- 5g: optional directional GI in the final renderer
- 5h: hard direct shadow ray in the final renderer
- 5i: optional soft-shadow approximation in final shading and in the bake shader

This matters because before 5g the final image still only read the isotropic reduced probe grid even though the bake had already become directional.

## Phase 5 debug views

Once direction bins exist, new debug views become possible:

- atlas raw
- hit-type heatmap
- nearest-bin viewer
- bilinear viewer

These modes exist so you can ask:

- does this probe really store different colors by direction?
- does the merge look up the correct upper-cascade direction?

## Phase 5f: directional bilinear

Nearest-bin lookup still produces hard bin boundaries.

So Phase 5f blends neighboring direction bins instead of snapping to exactly one.

Conceptually:

- Phase 5c fixed "wrong direction"
- Phase 5f smooths "hard boundaries between nearby directions"

That is why 5f belongs inside Phase 5, not as an unrelated polish pass.

## What Phase 5 solved and what it did not

It solved the biggest structural problem left by Phase 4:

- upper-cascade reuse now has directional meaning

It did not magically make every related design perfect.

Open issues still include:

- runtime A/B validation quality
- the exact semantics of non-co-located visibility weighting
- how much of the later soft-shadow path is considered a visual approximation versus a long-term keeper
- which debug modes are meant to validate bake data versus final-render consumption

But conceptually, this is the phase that turns the project from isotropic-probe GI into directional-probe GI.
