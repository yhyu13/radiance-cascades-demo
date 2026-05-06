# 08 Phase 5 in the Final Renderer: Directional GI, Shadow Ray, and Soft Shadow

Up to Phase 5f, most of the branch's complexity lived in the probe bake.

But the final screen shader also changed in important ways after that:

- it gained a real direct-shadow path
- it gained a directional-GI path that can read a cascade atlas directly
- and it gained an optional soft-shadow approximation

This note explains that side of Phase 5.

## Before 5g, 5h, and 5i

Originally the final renderer did two simple things at a surface hit:

1. compute direct light as unshadowed Lambertian `N dot L`
2. sample one isotropic probe value from `uRadiance`

So even after the bake became directional, the final image still threw away some of that extra structure.

That is why later Phase 5 split into two conceptual halves:

- bake-side upgrades
- final-render upgrades

## Phase 5h: hard direct shadow in the final renderer

Problem:

- the visible surface shading path had no direct occlusion check
- every surface got full direct light if it faced the point light

Fix:

- cast a shadow ray from the visible hit point to the light

Important detail:

- the final shader knows the surface normal
- so it can push the shadow-ray origin slightly along the normal and slightly along the light direction
- this is better than the bake shader's older fixed `t = 0.05` bias

Effect:

- the final direct-light term can now be hard-shadowed correctly
- mode 4 "direct only" became much more useful because it shows this direct term by itself

## Phase 5g: directional GI in the final renderer

Problem:

- `probeGridTexture` is still an isotropic average
- the final renderer used to read only that average
- so the screen shader was discarding the directional information already present in the atlas

Fix:

- add a second final-render path that reads the selected cascade's directional atlas
- for each visible surface hit:
  - find the nearby probes
  - for each probe, integrate the atlas bins over the surface normal's hemisphere
  - spatially trilinearly blend the 8 surrounding probes

This means the final renderer now has two indirect-light modes:

- isotropic: read `uRadiance`
- directional: read the selected cascade atlas with cosine-weighted hemisphere integration

Important caveat:

- the startup default selects C0
- the current UI selector can inspect another cascade for both isotropic and directional GI

## Phase 5i: soft-shadow approximation

Problem:

- even after 5h, the direct shadow edge is binary because the light is still a point light
- the banding analysis explored whether a soft approximation would hide the harshest visible edge

Fix:

- optional SDF cone soft shadow in the final renderer
- optional similar soft-shadow approximation in the bake shader

This is not a physically correct area-light model.

It is an appearance approximation:

- lower `k` means wider, softer shadow
- higher `k` moves back toward the binary look

The branch keeps this as a toggle because it changes the look, not just the numerical correctness.

## The important mental split

If Phase 5 becomes confusing, keep this split:

1. 5a-5f mostly changed how probes are stored and merged
2. 5g changed how the final renderer consumes indirect light
3. 5h changed direct-light visibility in the final renderer
4. 5i added optional soft-shadow approximations in both final shading and bake shading

That is why the later docs talk about both:

- atlas bins and upper-cascade merge
- shadow rays and final image modes

Those are different layers of the system.
