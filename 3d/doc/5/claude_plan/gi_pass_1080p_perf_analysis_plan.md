# Plan: GI Pass 1080p Performance Analysis (vs. 1ms budget) — revised after codex 11

## Changelog (post codex `11_gi_pass_1080p_perf_analysis_plan_review.md`)

All 8 findings accepted. Plan revised before any code lands:

- **F2+F5 (medium+medium) plan revision.** `SetWindowSize()` is
  not used anywhere in the codebase; calling it AFTER
  `InitWindow(DEFAULT...)` would leave `Demo3D` constructed with
  stale 720p dims (viewport, FOV-fit math at
  [demo3d.cpp:4993](../../../src/demo3d.cpp#L4993), GI blur FBO).
  Correct approach: parse `--window-size` **BEFORE** `InitWindow`
  and pass the user dims directly. Init order is now:
  parse argv → `initializeApplication(W, H)` → `glewInit` →
  `Demo3D` construction. No after-init resize. Simpler AND correct.
- **F1 (low) doc fix.** `rdocForceRebuildCount` line ref `:2721`
  → `:4721`. The `forceCascadeRebuild + renderFrameIndex=0`
  sustain logic is at lines 869-875.
- **F3 (medium) doc fix.** "1080p raymarch ~2.25×" relabelled as
  ESTIMATE; actual scaling will be confirmed by the 1080p capture
  (real raymarch could land ±15% of this estimate depending on
  warp coherence and texture-cache behavior).
- **F4 (low) doc fix.** Phase 2 captures explicitly require Phase 1
  to land first (the `--window-size` flag must be built before any
  1080p capture can run). Phase ordering is strict.
- **F6 (low) doc add.** Cross-check step added to Phase 4: read
  existing CPU-side `cascadeTimeMs` log line at
  [demo3d.cpp:885](../../../src/demo3d.cpp#L885) and compare against
  RenderDoc cascade total. Free sanity check; should be in same
  ballpark (CPU includes GPU + driver wait so CPU ≥ GPU expected).
- **F7 (low) doc add.** `--window-size` parser follows the Step 10
  `--camera-pos=x,y,z` precedent at
  [main3d.cpp:236-244](../../../src/main3d.cpp#L236) (`std::sscanf`
  with explicit error logging on parse failure).
- **F8 (low) doc fix.** "6 dispatch sites" → "10 `glPushDebugGroup`
  sites; 6 per-frame GI passes + 4 mesh-SDF-bake one-shots that
  fire on OBJ load only".

## Context

User wants a RenderDoc-based GPU performance analysis of the GI pass
at 1080p with a target budget of **1 ms total**. The most recent
measurement (frame 401, 2026-05-06, **720p**) was:

```
Cascade bake C0   5271.9 µs       Reduction C0    27.2 µs
Cascade bake C1   6340.8 µs       Reduction C1   193.2 µs
Cascade bake C2  10525.4 µs       Reduction C2   198.7 µs
Cascade bake C3   6992.6 µs       Reduction C3   223.0 µs
Raymarching      5916.7 µs       GI blur      1724.4 µs
                                         Total ~37.4 ms (all cascades forced)
```

So we're starting at **~37× over budget** in the worst case
(all-cascades-forced) at 720p. With staggered cadence (C0 every
frame, C1/2/3 at 1/2/4/8 cadence) the steady-state average is
~14 ms cascades + ~7.6 ms render = **~22 ms staggered at 720p**,
still ~22× over budget.

At 1080p the cascade dispatches don't change (volume-resolution
bound: 32³/16³/8³/4³ probes), but raymarch fragment work scales
~2.25× (1920×1080 vs 1280×720). Estimated 1080p staggered:
~14 ms cascades + ~13.3 ms raymarch + 1.7 ms blur = **~29 ms**.

The 1 ms goal isn't achievable without significant restructuring,
but we can't even discuss that until we have a clean baseline AT
1080p. **This plan is the measurement step.** Optimization work
follows in a later plan once we know exactly where time is going.

---

## Approach

### Phase 1 — Measurement infrastructure (small code change)

**Add `--window-size=WxH` CLI flag** in `main3d.cpp`. The current
default is hardcoded 1280×720 ([main3d.cpp:31-34](../../../src/main3d.cpp#L31)),
no CLI override exists. Default unchanged so existing captures
stay reproducible.

**Init order matters (codex 11 F2+F5).** `SetWindowSize()` is not
used in this codebase, and calling it after `InitWindow(DEFAULT...)`
would leave `Demo3D` constructed with stale 720p dims for FOV-fit
math, viewport, and GI blur FBO. Correct approach: parse the flag
BEFORE `InitWindow`, then pass the parsed dims directly:

```cpp
// In main(), BEFORE initializeApplication():
int wWidth  = DEFAULT_WIDTH;
int wHeight = DEFAULT_HEIGHT;
for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--window-size=", 0) == 0) {
        if (std::sscanf(arg.substr(14).c_str(), "%d,%d", &wWidth, &wHeight) != 2) {
            std::cerr << "[MAIN] --window-size: expected W,H (got: '"
                      << arg.substr(14) << "')\n";
            wWidth  = DEFAULT_WIDTH;
            wHeight = DEFAULT_HEIGHT;
        } else {
            std::cout << "[MAIN] --window-size=" << wWidth << "x" << wHeight << "\n";
        }
    }
}

// Then call initializeApplication(wWidth, wHeight) -- this requires
// adding W/H parameters to the function signature, replacing the
// hardcoded DEFAULT_WIDTH/HEIGHT inside InitWindow() at line 430.
//
// Demo3D construction at line ~490 then naturally reads the right
// dims via GetScreenWidth()/GetScreenHeight(). No post-construction
// reconciliation needed.
```

Parser pattern matches the Step 10 `--camera-pos=x,y,z` precedent at
[main3d.cpp:236-244](../../../src/main3d.cpp#L236) (`std::sscanf` with
explicit error logging on parse failure).

**No GPU-timer instrumentation needed** — RenderDoc's per-dispatch
GPU timer (already extracted by `tools/rdoc_extract.py`) gives
exactly the per-pass µs we need. The codebase has 10
`glPushDebugGroup` sites (6 per-frame GI passes: sdf_analytic,
radiance_3d, reduction_3d, temporal_blend, raymarch, gi_blur; plus
4 mesh-SDF-bake one-shots: GPU JFA, GPU triangle voxelize, etc.
that fire on OBJ load) — RenderDoc auto-groups by these labels so
the per-pass table comes out clean.

There IS existing CPU-side cascade timing at
[demo3d.cpp:885](../../../src/demo3d.cpp#L885) (`cascadeTimeMs =
(GetTime() - t0) * 1000.0`) — too coarse for per-pass breakdown
but useful as a Phase 4 cross-check against the RenderDoc total
(see Phase 4 below).

### Phase 2 — Capture three configurations (requires Phase 1 build complete)

**Phase ordering note (codex 11 F4)**: Phase 2 captures depend on
the `--window-size` flag from Phase 1. Phase 1 must build + smoke-test
clean before any 1080p capture can run. Ordering is strict:
Phase 1 build → Phase 2 captures → Phase 3 extract → Phase 4 analyze.

Sponza-master at the established cam.md viewpoint (so results are
comparable across steps).

```
RadianceCascades3D.exe \
    --window-size=1920,1080 \
    --load-obj=sponza-master --gpu-voxelize --gpu-sdf \
    --camera-pos=1.0710,-0.0723,-0.3393 \
    --camera-target=0.1212,-0.0812,-0.6520 \
    --auto-rdoc                              # captures at +8s warmup
```

`--auto-rdoc` already forces all cascades to dispatch in the
captured frame (`rdocForceRebuildCount=2` in
[demo3d.cpp:4721](../../../src/demo3d.cpp#L4721); the
`forceCascadeRebuild + renderFrameIndex=0` sustain logic is at
[demo3d.cpp:869-875](../../../src/demo3d.cpp#L869)) — so the captured
.rdc reflects the worst-case **all-cascades-forced** scenario, NOT
the staggered steady state.

To get staggered steady-state numbers we'd need a separate
capture at a frame where stagger has settled — but the existing
`--auto-rdoc` doesn't give us that. **Capture targets**:

| Config | How | What it tells us |
|---|---|---|
| **A**: 1080p, all cascades forced (worst case) | `--auto-rdoc` | Per-pass dispatch cost, isolated from stagger; baseline for "if every cascade ran every frame" |
| **B**: 720p, all cascades forced | `--auto-rdoc` (no `--window-size`) | Repro of frame 401 baseline at current code state — sanity-check that nothing has regressed since 2026-05-06 |
| **C**: 1080p, mode 0 default settings, 600 frames render | Headless `--exit-frames=600 --screenshot=...` + read existing in-app `[Demo3D]` log lines (CPU+GPU timing in mesh SDF bake; cascade times only via RenderDoc) | Validates that visual output matches prior captures at 1080p; not a perf measurement on its own |

For staggered steady-state numbers, we'll **derive** them from A's
per-pass cost + the known stagger pattern (C0 every frame, C1
every 2, C2 every 4, C3 every 8) rather than capture separately.
Math: `staggered = C0 + C1/2 + C2/4 + C3/8 + reduce_costs/stagger
+ raymarch + gi_blur`.

### Phase 3 — Extract per-pass timings

Run `tools/rdoc_extract.py` against each .rdc to produce the
per-pass µs table (matching the [frame 401 format](../../../tools/analysis/rdoc_frame_frame401_pipeline.md)).
Output goes to `tools/analysis/rdoc_frame_<name>_pipeline.md` per
the existing convention. The script also extracts thumbnails of
key textures (atlas, SDF, albedo) — useful for sanity but not
required for perf analysis.

### Phase 4 — Build the comparison table

Single markdown table:

| Pass | 720p baseline (frame 401) | 720p re-cap (config B) | 1080p forced (config A) | 1080p staggered (derived) | % of 1ms budget |
|---|---|---|---|---|---|
| C0 bake | 5271.9 µs | ? | ? | ? | ?% |
| C0 reduce | 27.2 µs | ? | ? | ? | ?% |
| ...C1/C2/C3... | ... | ... | ... | ... | ... |
| Raymarch | 5916.7 µs | ? | **~13300 µs estimated** (codex 11 F3) | same | ?% |
| GI blur | 1724.4 µs | ? | ? | ? | ?% |
| **Total (worst)** | 37426 µs | ? | ? | — | ?× over |
| **Total (staggered)** | ~22000 µs | — | — | ? | ?× over |

**Codex 11 F3**: the 2.25× raymarch scaling estimate assumes linear
pixel-count scaling. Real scaling depends on warp coherence and
texture-cache behavior at the new resolution; actual could land
±15% of estimated. The 1080p capture will confirm or correct.

Plus per-pass scaling note: which passes are window-bound
(raymarch, gi_blur fragment) vs volume-bound (radiance_3d,
reduction_3d, temporal_blend) — so we know which ones get cheaper
if we drop resolution and which ones don't.

**Cross-check (codex 11 F6).** Read the existing CPU-side
`cascadeTimeMs` from app stdout (logged at
[demo3d.cpp:885](../../../src/demo3d.cpp#L885)) and compare against
RenderDoc's cascade-bake total. CPU wall-clock includes GPU + driver
wait, so CPU ≥ GPU is expected. If they're wildly different (e.g.,
CPU is 30 ms but RenderDoc says 5 ms), there's CPU-side overhead
in the cascade loop worth investigating separately.

### Phase 5 — Write the analysis report

Dump to **`doc/5/claude_plan/perf/gi_pass_1080p_perf_analysis.md`**
(create the `perf/` subdir under claude_plan) following the
established impl-doc style:

- **Headline**: how many × over budget the actual 1080p cost is, both worst-case and staggered
- **Per-pass table** as above
- **Hotspot ranking**: top 3 by absolute time, top 3 by "if I cut this, ms saved per frame"
- **Optimization candidates** (categories only — not specific
  implementations): cascade resolution reduction, deeper stagger,
  probe culling on under-occupied cascades (codex 09 P1 territory),
  raymarch step-count cap, blur skip / smaller kernel, removing
  per-frame redundant work, fused EMA tuning
- **Out of scope of this analysis**: actual optimizations (deferred
  to next plan once user picks which knobs to turn); changing the
  default window size (the CLI flag is the only persistent change)

Also append a short entry to `.wolf/memory.md` per the project's
established protocol.

---

## Files Modified

- [src/main3d.cpp](../../../src/main3d.cpp) — add `--window-size=WxH` CLI
  parser entry alongside the existing flag block at lines ~167-232;
  apply via `SetWindowSize()` (raylib) right after `InitWindow`.
  Default 1280×720 unchanged. (~15 lines net)

That's the only code change. Everything else is capture + analysis +
docs.

---

## Reuse from existing code

- [src/main3d.cpp:184](../../../src/main3d.cpp#L184) — `--auto-rdoc` flag
  already wires the RenderDoc capture trigger + 8s warmup +
  forced-all-cascades behavior
- [tools/rdoc_extract.py](../../../tools/rdoc_extract.py) — already produces
  the per-pass µs table from a .rdc file (frame 401 example in
  [tools/analysis/rdoc_frame_frame401_pipeline.md](../../../tools/analysis/rdoc_frame_frame401_pipeline.md))
- [tools/analyze_renderdoc.py](../../../tools/analyze_renderdoc.py) —
  Claude-vision triage on the captured frame thumbnails (optional
  for this analysis; can run if we want sanity-check on visual
  correctness alongside perf)
- `glPushDebugGroup`-labeled passes already in `demo3d.cpp` at
  **10 dispatch sites** (codex 11 F8): 6 per-frame GI passes
  (sdf_analytic, radiance_3d, reduction_3d, temporal_blend ×2,
  raymarch, gi_blur) + 4 mesh-SDF-bake one-shots that fire on OBJ
  load only (GPU JFA, GPU triangle voxelize). RenderDoc auto-groups
  dispatches by these labels so the per-pass table comes out clean.
- Step 10's `--camera-pos=x,y,z` parser at
  [main3d.cpp:236-244](../../../src/main3d.cpp#L236) is the
  precedent for the new `--window-size=W,H` parser — same
  `std::sscanf` pattern, same explicit error logging on parse
  failure (codex 11 F7).
- Existing CPU-side `cascadeTimeMs` log at
  [demo3d.cpp:885](../../../src/demo3d.cpp#L885) — sanity
  cross-check against RenderDoc cascade total (codex 11 F6)
- `cam.md` viewpoint is the established cross-step capture pose —
  use it for comparability with Step 10/11 captures

---

## Verification

1. **Build clean** with the new `--window-size` flag (zero errors,
   ≤ baseline warnings)
2. **Window opens at 1920×1080** when launched with
   `--window-size=1920,1080` (visual check, or assert via
   `GetScreenWidth()` log line at startup)
3. **At least 3 RenderDoc captures land cleanly** in `tools/captures/`
   (config A 1080p, config B 720p re-cap, config C 1080p visual)
4. **`rdoc_extract.py` produces per-pass tables** for at least
   configs A and B, written to `tools/analysis/`
5. **Comparison table is filled in** with concrete µs numbers for
   every cascade + raymarch + blur in the analysis doc
6. **No visual regression** — config C's mode-0 capture at 1080p
   should look like the post-Step-11 mode-0 capture but at higher
   resolution (smoother edges, otherwise identical)

---

## Out of Scope

- Implementing any optimization — that's a separate plan after we
  see the numbers and decide which knobs (cascade res / stagger /
  raymarch step count / blur kernel) to turn first
- In-app per-pass GPU timer overlay (could be useful later but
  RenderDoc covers this analysis)
- Changing the default window resolution (1280×720 stays the
  default; 1080p is opt-in via the new flag)
- Cross-scene comparison (Cornell vs Sponza-master) — focus on
  Sponza-master since it's the heavier load and what the user
  cares about
- Tuning probe occupation (codex 09 P1, user already chose to
  skip)

---

## Open Question

The 1 ms budget is very aggressive — current best-case staggered
estimate at 1080p is ~29 ms (~29× over). The analysis WILL surface
this clearly, but I want to flag now that hitting 1 ms likely
requires fundamental restructuring (e.g., much smaller cascade
volumes, much sparser probe sets, or moving cascade work off the
critical path entirely via async compute) — not just tuning. The
analysis report will rank optimization candidates by ROI but won't
sugarcoat the gap.