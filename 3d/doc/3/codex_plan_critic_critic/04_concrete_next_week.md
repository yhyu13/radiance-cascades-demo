# Concrete Next Week

## Day 1-2

Implement or finish the real final image path:
- raymarch analytic SDF from camera
- output surface hit position and normal correctly
- render direct light on Cornell-box walls and test geometry

Done means:
- the image is readable without debug interpretation
- camera movement keeps the image coherent
- light/material changes are visibly reflected

## Day 3

Add debugging that helps the image path only:
- normal visualization
- hit-distance or step-count visualization
- light position and intensity controls
- optional SDF slice/debug view if already cheap to maintain

Do not add new systems here.

## Day 4-5

Implement one cascade:
- tiny probe grid
- fixed low ray count
- stable radiance storage format
- simple surface sample in final shading

Done means:
- toggling the cascade on and off changes the image in an understandable way
- color bleeding or soft indirect contribution is visible
- the feature is cheap enough to iterate on

## Day 6-7

Decide whether to stop or add one more cascade.

Add a second cascade only if:
- the first one is clearly working
- the value of extra range is visually obvious
- debugging burden stays manageable

Otherwise:
- keep one cascade
- tune parameters
- document the result
- declare the prototype successful

## Practical caveats

- Probe counts explode quickly in 3D. Start much smaller than intuition suggests.
- Ray count per probe should start tiny. Stability and visibility matter more than theoretical coverage.
- Do not confuse "more volume resolution" with "better result". It often just makes debugging slower.
- Make every stage toggleable from the UI so you can isolate failures quickly.
- If the final image path is still not real, do not spend time on advanced cascade logic.

## Final recommendation

Continue the project, but only under strict prototype rules.

The branch is worth further work if the next week is used to produce a visible, direct-lit raymarched scene plus one working cascade. If that is not the focus, the project will likely drift back into infrastructure work without a convincing visual payoff.
