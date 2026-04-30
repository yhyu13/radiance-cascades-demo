# Phase 5f Implementation Learnings Review

## Verdict

The core code change is real and the document is directionally correct about what landed: the current `git diff` does add a bilinear upper-atlas sampling path, a new toggle in C++, and a new debug mode in `radiance_debug.frag`.

But the writeup still overstates a few things. The most important issues are a real debug-path wiring bug and one incorrect claim about the clamp behavior of the bilinear sampler.

## Findings

### 1. High: the new mode 6 is not reachable through the advertised `[F]` cycle path

The document says:
- "Toggle mode 5 -> mode 6 to directly see whether bilinear reduces the hard color step"

Evidence:
- `doc/cluade_plan/phase5f_impl_learnings.md:137-139`

But the live input path still does:
- `radianceVisualizeMode = (radianceVisualizeMode + 1) % 6`
- mode names only cover `Slice, MaxProj, Avg, Atlas, HitType, Bin`

Evidence:
- `src/demo3d.cpp:317-320`
- `src/demo3d.cpp:2248-2254`

So mode 6 exists in the radio-button UI, but it is not reachable by the `[F]` cycle the panel advertises. This is the same class of debug-path drift that has already shown up in earlier Phase 5 reviews.

### 2. High: the document's "Clamp invariant" explanation is false at the low edge, and the shader does not actually replicate `GL_CLAMP_TO_EDGE` there

The document claims:
- both reads hit the same border bin at the tile boundary
- the blend therefore behaves like `GL_CLAMP_TO_EDGE` within the tile

Evidence:
- `doc/cluade_plan/phase5f_impl_learnings.md:99-111`

But the code does:
- `octScaled = dirToOct(rayDir) * float(D) - 0.5`
- `b00 = clamp(floor(octScaled), 0, D-1)`
- `b11 = clamp(b00 + 1, 0, D-1)`
- `f = fract(octScaled)`

Evidence:
- `res/shaders/radiance_3d.comp:122-133`

At the low edge, for example when `oct = 0`, `octScaled = -0.5`, so:
- `b00 = 0`
- `b11 = 1`
- `f = 0.5`

That blends the border bin with the next interior bin. It does **not** collapse both reads to the same border bin, and it does **not** match clamp-to-edge semantics at that boundary. The code may still be usable, but the document's correctness argument for border behavior is wrong as written.

### 3. Medium: the debug-shader section describes helper reuse that did not actually happen

The document says:
- the debug shader mirrored `dirToOct` / `binToDir`
- only `dirToOct` is used by mode 6

Evidence:
- `doc/cluade_plan/phase5f_impl_learnings.md:141-142`

In the actual diff, `radiance_debug.frag` adds:
- `dirToOct_dbg`
- `octToDir_dbg`

And mode 6 uses neither of them; it constructs `octScaled` directly from `uAtlasBin`.

Evidence:
- `res/shaders/radiance_debug.frag:49-66`
- `res/shaders/radiance_debug.frag:199-218`

So this section is not just imprecise; it describes a different implementation from the one currently in the code.

### 4. Medium: the build-status claims are internally inconsistent, and `git diff` does not verify compilation

The header says:
- `Status: Implemented, compile-verified`

But the validation table says:
- `Build: 0 errors | Pending`

Evidence:
- `doc/cluade_plan/phase5f_impl_learnings.md:5`
- `doc/cluade_plan/phase5f_impl_learnings.md:183`

I verified the code claims against `git diff`, and the diff does show the relevant code changes in:
- `res/shaders/radiance_3d.comp`
- `res/shaders/radiance_debug.frag`
- `src/demo3d.cpp`
- `src/demo3d.h`

But diff inspection is not compile verification. As written, the document blurs those two standards.

## Where the document is strong

- The main architectural move is real in the diff: `sampleUpperDir()` and `uUseDirBilinear` were added.
- The C++ wiring for the new toggle is present.
- The review correctly keeps `GL_NEAREST` on the atlas and does the interpolation manually in-shader.

## Bottom line

This is a real code change, and the document captures the broad intention correctly. But it should be revised before being treated as a trustworthy status note.

I would fix it by:

1. correcting the mode-6 reachability story,
2. rewriting the clamp/border argument to match the actual shader math,
3. updating the debug-mode section to describe what `radiance_debug.frag` really does, and
4. making the build status consistent with whatever evidence actually exists.
