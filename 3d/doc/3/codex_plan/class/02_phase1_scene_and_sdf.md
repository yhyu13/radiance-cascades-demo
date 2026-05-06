# 02 Phase 1: Scene and SDF

Before probes and cascades matter, the renderer needs a trustworthy 3D scene representation.

Phase 1 solved that base problem.

## What had to work first

The project needed all of these before indirect lighting made sense:

1. A visible Cornell Box scene
2. A stable camera
3. A correct signed-distance field
4. A correct surface normal estimate
5. A correctly placed point light

If any of those are wrong, later GI work becomes impossible to interpret.

## Why the SDF matters

This branch raymarches against an SDF instead of rasterizing triangle meshes.

That means two things:

1. The final renderer needs the SDF to find visible surfaces.
2. The probe system also needs the SDF, because probe rays are raymarched through the same volume.

So the SDF is shared infrastructure. It is not just a debug view.

## What Phase 1 stabilized

The key fixes were:

- the room geometry was redesigned so the walls were thick enough to exist at the chosen voxel resolution
- the camera and light were moved to match the actual room coordinates
- the normal-estimation epsilon was increased so shading stopped collapsing
- debug state initialization was cleaned up
- the default volume resolution was reduced to a workable value

The important teaching point is not each specific bug. It is this:

Phase 1 made the scene physically interpretable enough that later lighting changes could be judged visually.

## What data exists after Phase 1

The project has:

- an SDF volume saying where surfaces are
- an albedo volume saying what color those surfaces are
- a final raymarch shader that can hit surfaces and shade them

That is the minimum base for Phase 2.

## Why Phase 2 had to come next

Once the room and direct shading were trustworthy, the next question became:

Can a single probe grid add useful indirect light at all?

That is the Phase 2 problem.
