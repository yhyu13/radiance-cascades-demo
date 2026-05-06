# 07 Phase 5 Extras: Spatial Layout, D Scaling, and Why the Docs Got Complicated

After the core atlas/merge upgrade, the branch explored two more questions:

1. Should higher cascades use different probe spacing?
2. Should higher cascades use more direction bins?

These are useful questions, but they also introduced much of the later jargon.

## Part 1: co-located vs non-co-located cascades

### Co-located

All cascades use the same probe positions.

Benefits:

- simple addressing
- easier merge logic
- no need to spatially interpolate across different probe layouts

Cost:

- far cascades may be spatially denser than necessary

### Non-co-located

Higher cascades use fewer probes with bigger spacing, such as:

- C0: `32^3`
- C1: `16^3`
- C2: `8^3`
- C3: `4^3`

Benefits:

- less memory for upper levels
- more ShaderToy-like multi-resolution spatial layout

Cost:

- more addressing complexity
- visibility and interpolation questions become harder

## Why this mattered for Phase 5d

A visibility-weighting idea was introduced:

"If the upper probe cannot actually see the lower probe's location, maybe its contribution should be suppressed."

That idea is trivial in co-located mode because the upper and lower probes are at the same place.

But the later implementation work found something stronger:

Even in the current non-co-located implementation, the specific Euclidean-distance visibility test is still structurally inert under the current interval design.

The branch then went one step farther:

- instead of stopping at `upperProbePos = probePos / 2`
- it added real 8-neighbor spatial trilinear interpolation for the non-co-located directional merge path

So the current code is no longer just "nearest parent" in non-co-located mode.

So the concept is understandable, but the current live behavior is subtler than the original pitch.

## Part 2: D scaling

`D` is the atlas tile width per axis.

Examples:

- `D=4` means 16 direction bins
- `D=8` means 64 direction bins
- `D=16` means 256 direction bins

Question:

Should all cascades use the same D?

Fixed-D view:

- simpler
- easier to debug

Scaled-D view:

- higher cascades cover larger distances
- higher cascades may need finer angular detail

So scaled D tries to give far cascades more directional resolution than near cascades.

In the current codebase, the implemented scaled path is a formula:

- cascade `i` uses `min(16, dirRes << i)`

With the current constructor default `dirRes=8`, that means:

- C0: `D=8`
- C1: `D=16`
- C2: `D=16`
- C3: `D=16`

With `dirRes=4`, it becomes the older teaching example:

- C0: `D=4`
- C1: `D=8`
- C2: `D=16`
- C3: `D=16`

The earlier idea of using `D=2` at C0 was dropped because it is too degenerate for this branch's directional encoding.

## Why this made the branch harder to follow

Once 5d and 5e landed, the branch had several interacting toggles:

- directional merge on/off
- co-located vs non-co-located probe layout
- fixed D vs scaled D
- nearest-bin vs bilinear directional sampling

That is powerful for debugging, but bad for human readability.

The safe way to think about it is:

1. Phase 5c is the core directional-merge upgrade
2. Phase 5d experiments with spatial layout
3. Phase 5e experiments with angular-budget distribution
4. Phase 5f smooths directional bin boundaries
5. 5g, 5h, and 5i then change how the final renderer consumes and displays the result

If you keep that order, the later jargon becomes easier to place.
