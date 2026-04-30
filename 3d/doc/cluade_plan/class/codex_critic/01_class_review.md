# Claude Class Docs Review

## Verdict

This class set is substantially better than most of the raw phase notes. It is readable, mostly linear, and many of the later Phase 5 docs already explain the important ideas correctly.

But as a teaching set for the **current** codebase, it still has one major stale model and two smaller documentation issues:

1. it still teaches the final renderer too much as an isotropic `probeGridTexture` consumer,
2. some early docs understate how much 5g/5h/5i changed `raymarch.frag`,
3. and the glossary has a few stale or broken cross-references.

## Findings

### 1. High: the early pipeline docs still describe the final renderer as if it only reads isotropic C0 probe data

The clearest example is [01_scene_and_pipeline.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\01_scene_and_pipeline.md), which says:

- the final image looks up indirect light from the nearest probe in C0's `probeGridTexture`
- the final GI source is C0 `probeGridTexture`
- GI quality entirely depends on what is stored in those `probeGridTextures`

Evidence:

- [01_scene_and_pipeline.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\01_scene_and_pipeline.md:55)
- [01_scene_and_pipeline.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\01_scene_and_pipeline.md:59)
- [01_scene_and_pipeline.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\01_scene_and_pipeline.md:73)

That stopped being generally true once 5g landed. Current `raymarch.frag` has two indirect paths:

- isotropic: `texture(uRadiance, uvw)` from the selected cascade's reduced grid
- directional: `sampleDirectionalGI(pos, normal)` reading the C0 atlas directly

Evidence:

- `src/demo3d.cpp:1254-1262`
- `src/demo3d.cpp:1275-1290`
- `res/shaders/raymarch.frag:405-409`
- `res/shaders/raymarch.frag:437-440`

So the early teaching model is now too narrow. It was good for pre-5g understanding, but it is stale as a description of the current renderer.

### 2. Medium: `02_probes_and_cascades.md` still presents the final GI lookup too simplistically for the current branch

[02_probes_and_cascades.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\02_probes_and_cascades.md) says:

- the final renderer interpolates between nearest probes
- then later says `raymarch.frag` reads `probeGridTexture` at the closest probe position

Evidence:

- [02_probes_and_cascades.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\02_probes_and_cascades.md:23)
- [02_probes_and_cascades.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\02_probes_and_cascades.md:123)

That is not catastrophic in a teaching note, but it mixes:

- a spatial-interpolation mental model

with

- a nearest-probe wording shortcut

And after 5g, the current branch is even more split:

- isotropic path uses filtered `uRadiance`
- directional path uses manual 8-probe trilinear over the C0 atlas

So this document now needs a brief update that says "the old simplified story is isotropic reduced-grid sampling, but the latest branch also has a directional C0-atlas path in the final shader."

### 3. Low: the glossary still contains stale/broken references and now underspecifies the renderer-side mode split

The glossary is generally useful, but it still has old cross-references like:

- `see 02_scene_and_sdf`
- `see 06_phase5_spatial_options`
- `see 05_phase5_atlas`
- `see 06_phase5_bilinear`

Evidence:

- [00_jargon_index.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\00_jargon_index.md:11)
- [00_jargon_index.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\00_jargon_index.md:63)
- [00_jargon_index.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\00_jargon_index.md:110)
- [00_jargon_index.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\00_jargon_index.md:147)

Those names do not match the actual filenames in this folder.

Also, the glossary defines only the radiance-debug mode 6 bin viewer. In the current branch there is another important mode 6 concept:

- the final-render GI-only mode in `raymarch.frag`, which now respects directional GI

That split is explained later in the class set, but the glossary is now missing a concise term for it.

## Where the class set is strong

- The later Phase 5 docs are much stronger than the early pipeline docs.
- [12_phase5g_directional_gi.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\12_phase5g_directional_gi.md) is especially good and already explains the current directional final-render path clearly.
- [13_phase5i_soft_shadow.md](D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\class\13_phase5i_soft_shadow.md) correctly explains that shared `k` is UI convenience, not true physical coupling.
- The overall reading order is still sensible for someone learning the branch from simpler to more complex ideas.

## Bottom line

This class set is worth keeping, but it is now slightly split-brain:

- the later Phase 5 notes describe the modern branch fairly well
- the early "pipeline" notes still describe a pre-5g final renderer

I would fix it by:

1. updating the early pipeline docs to mention the two current final-GI paths,
2. clarifying that the final renderer no longer only means "sample C0 reduced grid",
3. cleaning up the glossary cross-references and adding one short term for the final-render GI-only mode.
