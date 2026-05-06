# Recommendation

## Status I would publish

Use this wording instead:

- Phase 1: implemented and build-verified; visual confirmation still recommended
- Phase 2: implemented and build-verified; visual smoke test still required before calling it complete

## Best next steps

### 1. Do the missing visual confirmation properly

Required checks:
- with `uUseCascade = 0`, confirm the image is stable and readable
- with `uUseCascade = 1`, confirm the image changes in a controlled way
- capture screenshots for both states from the same camera
- add one debug mode to view the cascade volume directly or show sampled indirect term only

### 2. Fix the two most important correctness gaps before calling the result successful

Priority order:
- add surface albedo/material influence to both direct shading and probe shading
- add a shadow or visibility check for the direct light

These two changes matter more than adding a second cascade.

### 3. Clean up status drift

Update:
- `src/demo3d.cpp` debug UI placeholder bullets
- plan wording that implies more visual proof than currently exists
- unreachable frozen lighting code if it is not part of the new path anymore

## Strategic judgment

The implementation direction is still worth continuing.

But the right interpretation is:
- the branch has reached a meaningful prototype milestone
- it has not yet reached a trustworthy Cornell-box GI milestone

If the goal is the simplest convincing result, do not start Phase 3 yet.
First make Phase 2 visually honest by adding material color, light visibility, and a direct A/B screenshot-based smoke test.
