# Plan: Verify Window-bound vs Volume-bound Classification + Shader Bottleneck Research (revised after codex 12)

## Changelog (post codex `12_gi_pass_scaling_experiment_plan_review.md`)

7 of 8 findings accepted; F8 partially rejected (the
`meshSDFReady = false` part — probe-res change does NOT affect
the SDF voxel grid):

- **F1 (medium) doc fix.** `volumeResolution` is a runtime member
  ([demo3d.h:922](../../../src/demo3d.h#L922)) initialized from
  `DEFAULT_VOLUME_RESOLUTION` constexpr, NOT compile-time only.
  Deferral decision stands but rationale corrected: "runtime-changeable
  but would require texture reallocation infrastructure for all
  volume textures — non-trivial, deferred."
- **F2 (medium) plan revision.** Explicit Phase 1 addition of 3
  public setters in `demo3d.h`: `setCascadeC0Res(int)`,
  `setRaymarchSteps(int)`, `setGIBlurRadius(int)`. Step 10/11
  setter pattern is the precedent — public setters with explicit
  invalidation, not direct member access from `main3d.cpp`.
- **F3 (low) doc note.** Cascade re-allocation costs ~1-2 s per
  probe-res change. Runtime estimate revised: ~5 min → ~7 min
  total. Reallocation happens during the 8 s warmup so it doesn't
  affect captured-frame measurements.
- **F4 (low) doc fix.** `giBlurRadius` member default is 8, not 1.
  Flag table corrected. Experiment 4 sweep {1, 2, 4, 8} unchanged
  (covers the full ImGui range with 8 being the existing default).
- **F5 (low) doc clarification.** `--window-size` flag is already
  implemented (Step 12 / codex 11 reply,
  [main3d.cpp:145-159](../../../src/main3d.cpp#L145)) — prerequisite
  is met, not a blocker.
- **F6 (low) doc fix.** Cross-references use full plan/review
  filenames instead of "codex N" shorthand to avoid numbering
  confusion.
- **F7 (low) doc fix + experiment retune.** `raymarchSteps`
  default is 256 (was "(existing default)"). Experiment 3 sweep
  {32, 64, 128, 256, 512} → **{32, 64, 128, 256, 384}** — drops
  512 to avoid warmup-convergence concerns at ~250-500 ms/frame.
- **F8 (medium) plan revision + partial reject.** `setCascadeC0Res`
  setter must replicate the existing ImGui handler's full
  destroy/init cycle ([demo3d.cpp:793-801](../../../src/demo3d.cpp#L793))
  PLUS the codex 08 4-line lighting-invalidation extras
  (forceCascadeRebuild, renderFrameIndex=0, historyNeedsSeed). But
  **NOT** `meshSDFReady = false` — probe-res change doesn't affect
  the SDF voxel grid (sdfTexture is sized by `volumeResolution`,
  not by cascade probe-res). Probes SAMPLE the SDF; they don't
  define it. Adding `meshSDFReady = false` would force a wasted
  ~3-7 ms SDF rebake — same lesson as codex 08 F1+F7.

## Context

Step 12 perf analysis classified each pass as window-bound or
volume-bound based on dispatch architecture, but the actual per-pass
times had ~3× variance between Config A (1080p, 82.8 ms) and
Config B (720p, 341.7 ms) — likely GPU power-state noise. Before
recommending optimizations, the user wants to **empirically
verify** the classification by varying the scaling axis for each
pass and confirming the timing moves the way the classification
predicts.

**Current baseline (Config A, 1080p forced)**:

| Pass | Time (ms) | Predicted scaling |
|---|---:|---|
| Raymarch | 26.1 | Window-bound (fragment shader) |
| Cascade C2 bake | 15.1 | Volume-bound (probe³ × D²) |
| Cascade C3 bake | 13.9 | Volume-bound |
| GI blur | 11.4 | Window-bound (fragment shader) |
| Cascade C1 bake | 9.9 | Volume-bound |
| Cascade C0 bake | 5.8 | Volume-bound |

**Hypothesis to test**: window-bound passes shrink linearly with
pixel count when window is shrunk; cascade passes don't change.
Conversely cascade passes shrink with C0 probe-res reduction; window
passes don't change.

Then for each pass, **read the shader to identify the dominant
inner loop** (raymarch step loop, bilateral kernel, per-direction
ray cast, etc.) so we know WHICH lines to optimize, not just
which passes.

---

## Approach

### Phase 1 — Add 3 CLI scaling knobs (small code change)

Currently only `--window-size` is CLI-tunable. Cascade probe-res +
raymarch step count + blur radius are ImGui-only, which doesn't
work for headless `--auto-rdoc` capture. Add 3 flags:

| Flag | Member | Runtime default | Purpose |
|---|---|---|---|
| `--cascade-c0-res=N` | `cascadeC0Res` | **32** | Scale cascade probe-grid (8/16/24/32/48/64 per existing ImGui dropdown) |
| `--raymarch-steps=N` | `raymarchSteps` | **256** ([demo3d.cpp:225](../../../src/demo3d.cpp#L225)) | Bound raymarch fragment cost |
| `--gi-blur-radius=N` | `giBlurRadius` | **8** ([demo3d.cpp:279](../../../src/demo3d.cpp#L279)) | Scale bilateral kernel size (ImGui slider range 1-8) |

Same parser pattern as Step 10 `--camera-pos` / Step 11
`--strip-ambient-floor-bake` (sscanf for `=N`, error-log on bad input).
**Codex 12 F2: all 3 setters must be added** — none exist in the
current header. Apply via the new public setters (definitions
below). All 3 flags MUST be applied BEFORE the first cascade/render
dispatch — same insertion point as the other post-init flags at
`main3d.cpp:~166`.

**Setter definitions** (codex 12 F2 + F8):

```cpp
// In demo3d.h, public section:
void setCascadeC0Res(int v);   // full destroy/init cycle (geometry change)
void setRaymarchSteps(int v);  // uniform-only; no GPU resource changes
void setGIBlurRadius(int v);   // uniform-only; clamped to [1, 8]

// In demo3d.cpp:
void Demo3D::setCascadeC0Res(int v) {
    if (cascadeC0Res == v) return;
    cascadeC0Res = v;
    destroyCascades();           // matches existing ImGui handler at :793-801
    initCascades();
    cascadeReady        = false;
    forceCascadeRebuild = true;  // codex 08-style: bypass stagger so all 4 cascades dispatch on next frame
    renderFrameIndex    = 0;     // ensure --auto-rdoc captures all 4 cascades
    historyNeedsSeed    = true;  // EMA history was zeroed in initCascades; seed cleanly
    // NOT meshSDFReady = false (codex 12 F8 partial reject):
    // probe-res change does not affect the SDF voxel grid -- probes SAMPLE
    // the SDF, they don't define it. SDF is sized by volumeResolution (128).
    std::cout << "[Demo3D] cascadeC0Res=" << v << " (cascade reallocated)\n";
}
void Demo3D::setRaymarchSteps(int v) {
    raymarchSteps = v;
    std::cout << "[Demo3D] raymarchSteps=" << v << "\n";
}
void Demo3D::setGIBlurRadius(int v) {
    giBlurRadius = std::clamp(v, 1, 8);
    std::cout << "[Demo3D] giBlurRadius=" << giBlurRadius << "\n";
}
```

**Volume resolution scaling deferred** (codex 12 F1 corrected):
`volumeResolution` is a runtime member at
[demo3d.h:922](../../../src/demo3d.h#L922) initialized from the
constexpr `DEFAULT_VOLUME_RESOLUTION = 128` — runtime-changeable in
principle, but changing it requires texture reallocation
infrastructure for all volume textures (`voxelGridTexture`,
`sdfTexture`, `albedoTexture`, `voronoiTextureA/B`,
`meshVoxelBaseTexture`, `voxelOwnerTexture`). Non-trivial; deferred.
We instead verify cascade volume-boundness by varying probe-res
(which IS the dispatch driver for `radiance_3d.comp`), not the SDF
voxel grid.

### Phase 2 — Run 4 scaling experiments

**Standard test scene**: Sponza-master, GPU voxelize + GPU SDF,
cam.md viewpoint, `--auto-rdoc` (forces all cascades). One capture
per data point. Per-pass times extracted from the auto-generated
`tools/analysis/rdoc_frame_frame<N>_pipeline.md`.

#### Experiment 1 — Window scaling (verify raymarch + GI blur)

Fix: probe-res 32, default raymarch steps, default blur radius
Vary window: **320×180, 640×360, 1280×720, 1920×1080, 2560×1440**

Expected: raymarch + GI blur times scale ~linearly with pixel
count (5 data points span 0.03× → 1× of 1080p). Cascade times stay
flat. Plot the pass times vs pixel count on a log-log; window-bound
passes should give slope=1, volume-bound should give slope=0.

#### Experiment 2 — Cascade probe-res scaling (verify cascade bakes)

Fix: window **320×180** (minimize fragment-bound noise)
Vary `--cascade-c0-res`: **8, 16, 24, 32, 48, 64**

Expected: cascade bake times scale ~cubically with C0 res (probe
count is res³). Raymarch + GI blur stay flat (window unchanged).
This isolates the cascade dispatches from the dominant raymarch
fragment cost in Experiment 1.

#### Experiment 3 — Raymarch step count scaling

Fix: window 1280×720, probe-res 32, blur off if possible
Vary `--raymarch-steps`: **32, 64, 128, 256, 384** (codex 12 F7:
dropped 512 to avoid warmup-convergence concerns — 512 steps × 1080p
× ~5 fetches/step ≈ 250-500 ms/frame, may not converge in the 8 s
warmup window)

Expected: raymarch time scales ~linearly with step count. Other
passes unchanged. Confirms step count is the inner loop driver
(not e.g. early-termination logic dominating). The 384 data point
(50% above default 256) is enough to demonstrate the scaling trend
above the existing default.

#### Experiment 4 — GI blur radius scaling

Fix: window 1280×720, probe-res 32, default raymarch steps
Vary `--gi-blur-radius`: **1, 2, 4, 8**

Expected: GI blur scales roughly quadratically with radius (2D
kernel = (2r+1)² taps per pixel). Raymarch + cascades unchanged.

**Total captures**: 5 + 6 + 5 + 4 = 20 captures. Runtime estimate
**~7 minutes** (revised from ~5 min per codex 12 F3 — Experiment 2's
6 probe-res changes each pay ~1-2 s for `destroyCascades + initCascades`
on top of the 8 s warmup; reallocation happens DURING warmup so it
doesn't affect captured-frame measurements).

### Phase 3 — Build the empirical scaling table

Single big table per experiment showing measured times. Then derive
**actual scaling exponents** vs predicted:

```
Pass            Measured slope    Predicted slope    Verdict
Raymarch        ~1.0              1.0                ✓ window-bound confirmed
GI blur         ~1.0              1.0                ✓ window-bound confirmed
Cascade C2      ~0.0              0.0                ✓ volume-bound confirmed
... etc
```

If actual slopes diverge significantly from predicted (>15%),
investigate: maybe a volume-bound pass also has window-dependent
cost (e.g., readback), or a window-bound pass has fixed overhead.

### Phase 4 — Shader bottleneck research (per pass)

For each of the 6 per-frame GI passes, READ the shader and
identify the dominant cost. No code changes — just analysis. For
each pass, the report should answer:

- **What is the inner loop?** (per-step SDF lookup, per-direction
  ray cast, bilateral kernel iter, etc.)
- **What's the per-thread cost?** (texture fetches per iteration,
  ALU ops per step, etc.)
- **What scales the loop count?** (uSteps uniform, dirRes uniform,
  blur radius, cascade depth)
- **What's the most expensive operation per iteration?** (texture
  fetch, normalize, cross-product, sampler filter, etc.)

Shaders to read:
- [res/shaders/raymarch.frag](../../../res/shaders/raymarch.frag) — sphere-trace loop
- [res/shaders/gi_blur.frag](../../../res/shaders/gi_blur.frag) — bilateral filter
- [res/shaders/radiance_3d.comp](../../../res/shaders/radiance_3d.comp) — per-direction probe bake
- [res/shaders/reduction_3d.comp](../../../res/shaders/reduction_3d.comp) — D² atlas average
- [res/shaders/temporal_blend.comp](../../../res/shaders/temporal_blend.comp) — EMA + AABB clamp
- [res/shaders/sdf_analytic.comp](../../../res/shaders/sdf_analytic.comp) — analytic SDF generator (rarely
  the bottleneck, but include for completeness)

Cross-correlate with the experimental scaling: if raymarch is
~28× the budget AND the inner loop has a `for (i = 0; i < uSteps;
++i)` with 3 texture fetches per step, then the optimization
target is "reduce step count OR reduce fetches per step".

### Phase 5 — Write the analysis report

Dump to **`doc/5/claude_plan/perf/gi_pass_scaling_experiment.md`**
following the existing perf doc style:

- **Headline**: which classifications were confirmed, which were
  surprising
- **Per-experiment data tables** (4 tables, one per axis)
- **Empirical scaling exponents** vs predicted
- **Per-shader bottleneck breakdown** (6 sections, one per pass)
- **Cross-cutting observations**: which passes have hidden costs
  (constant fixed overhead, readback latency, etc.)
- **Optimization candidates** (categories only, ranked by ROI from
  the empirical data — different from Step 12's a-priori ranking
  because we now know which scalings are real)

Then update `.wolf/memory.md` per the established protocol.

---

## Files Modified

- [src/demo3d.h](../../../src/demo3d.h) — add public setters
  `setCascadeC0Res(int)`, `setRaymarchSteps(int)`,
  `setGIBlurRadius(int)` if missing (some may already exist as
  ImGui-edited members; just need to expose)
- [src/main3d.cpp](../../../src/main3d.cpp) — add 3 new CLI flag parsers in
  the post-init argv loop (~25 lines net)

That's it. No shader changes. No volume-res infrastructure.

---

## Reuse from existing code

- `--window-size` flag from Step 12 (already CLI)
- `--auto-rdoc` capture path
- `tools/rdoc_extract.py` per-pass µs table
- `cam.md` viewpoint convention
- ImGui setter logic for cascade-res / raymarch-steps / blur-radius
  — just need to extract the assignment + invalidation pattern into
  the public setters

---

## Verification

1. **Build clean** with the 3 new flags
2. **Smoke-test each flag**: launch with `--cascade-c0-res=16`,
   `--raymarch-steps=64`, `--gi-blur-radius=4`; verify the log
   echoes the value and the next captured frame uses it (probe-grid
   line shows `16^3`, etc.)
3. **All 20 captures complete** with `.rdc` + `_pipeline.md` files
4. **Empirical scaling slopes agree with predicted within ~15%** —
   if not, investigate before reporting
5. **Shader research section identifies one concrete inner loop +
   one optimization candidate per pass** (no implementation, just
   identification)

---

## Out of Scope

- Volume resolution scaling (would require texture-reallocation
  infrastructure; deferred to a separate experiment if needed)
- Implementing optimizations (the goal here is identification)
- GPU clock locking for variance reduction (single-shot captures
  remain ±2× noisy; we trust the per-experiment relative trends)
- Cross-scene comparison (Cornell vs Sponza-master)
- Modifying the staggering pattern
- Per-cascade D-resolution scaling (would need separate ImGui or
  CLI plumbing — out of scope this round)

---

## Honest expectation note

Some passes won't cleanly fit "window-bound" or "volume-bound" —
e.g., GI blur might have a fixed-cost FBO copy that doesn't scale
with window. Cascade reduction (`reduction_3d.comp`) dispatches
over probe³ but the inner D² loop adds another scaling axis. The
experiment will surface these; the report will document them
honestly rather than forcing them into a binary classification.
