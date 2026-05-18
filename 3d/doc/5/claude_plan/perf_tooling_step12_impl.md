# Step 12 — Implementation Notes: Perf Tooling (1080p Analysis + Scaling Experiment) — revised after codex 13

## Changelog (post codex `13_perf_tooling_step12_impl_review.md`)

All 8 findings accepted as doc improvements; no code changes:

- **F1 (medium) doc fix.** FOV-fit math line ref `:4993` (closing
  brace of `launchSequenceAnalysis()`) → `:5083-5088` (the actual
  `GetScreenWidth/Height` + `tan(fovy)` math inside
  `applyOBJViewPreset()`).
- **F2 (medium) doc fix.** "No per-pass instrumentation needed"
  was overstated. RenderDoc reliably covers **large-difference
  measurements** (Exp 1+2 class, 16-64× workload changes) where
  variance is dwarfed by signal. For **small-difference
  measurements** (Exp 3+4 class, 2× changes), single-shot captures
  are inadequate — needs GPU clock locking, N-capture averaging,
  or `cascadeTimeMs` stdout cross-check. The cross-check is
  escalated from "open item" to "recommended before any before/after
  optimization measurement".
- **F3 (medium) doc fix — intentional design, no code change.**
  ImGui handler at [demo3d.cpp:792-801](../../src/demo3d.cpp#L792)
  does 3-line invalidation; CLI setter does 6 lines. **Intentional**:
  CLI needs immediate convergence for `--auto-rdoc` capture at +8s
  (otherwise C2/C3 wouldn't have re-baked yet); ImGui interactive
  use can rely on stagger cadence for eventual convergence.
  Documented in architecture notes.
- **F4 (low) doc fix.** "What's next" rewritten to recommend
  **path 2 (variance control) BEFORE path 1 (Tier 1 optimizations)**.
  Without variance control, before/after comparisons have ±2-5×
  uncertainty bars — measurements indistinguishable from noise.
- **F5 (low) doc fix.** "No new GPU resources" → "no new permanent
  GPU resources; `setCascadeC0Res` triggers runtime reallocation
  of existing cascade textures at new probe-res dimensions
  (~1-2 s overhead per change)".
- **F6 (low) doc fix.** Parser pattern: `sscanf` for multi-value
  flags (`--window-size`, `--camera-pos`, `--camera-target`),
  `atoi` for single-value flags (the 3 new scaling knobs). Both
  trigger the same MSVC C4996 nag. Consistent with the broader
  codebase, not just `--camera-pos`.
- **F7 (low) doc fix.** Manual `if`-clamp instead of `std::clamp`
  in `setGIBlurRadius` is intentional — avoids pulling
  `<algorithm>` into `demo3d.h` (currently not included). The
  scaling experiment plan showed `std::clamp`; impl deviated for
  this reason. Functionally identical.
- **F8 (low) doc fix.** Config B anomaly elevated from "magnitude
  noise" to **"direction-flipping severity"**: 720p measured 4×
  SLOWER than 1080p — variance can invert the expected ordering.
  This is the load-bearing reason path 2 (variance control) must
  precede path 1 (optimizations).

**Date:** 2026-05-11 (revised post codex 13)
**Status:** Implemented and verified. Build clean (0 errors); 22 RenderDoc
captures landed (2 baseline + 20 scaling-experiment) with auto-extracted
per-pass timing tables. Window-bound vs volume-bound classification
empirically confirmed.

**Plan sources:**
- [gi_pass_1080p_perf_analysis_plan.md](gi_pass_1080p_perf_analysis_plan.md) (revised after codex 11)
- [gi_pass_scaling_experiment_plan.md](gi_pass_scaling_experiment_plan.md) (revised after codex 12)

**Reports produced:**
- [perf/gi_pass_1080p_perf_analysis.md](perf/gi_pass_1080p_perf_analysis.md) — 1080p baseline + budget gap (~83× over 1ms)
- [perf/gi_pass_scaling_experiment.md](perf/gi_pass_scaling_experiment.md) — 4-experiment scaling sweep + per-shader bottleneck research

---

## Summary

| Change | File | Effect |
|---|---|---|
| `--window-size=W,H` CLI flag (parsed BEFORE InitWindow per codex 11 F2+F5) | [src/main3d.cpp](../../src/main3d.cpp) | Headless 1080p / arbitrary-resolution captures |
| `initializeApplication(int, int)` refactor | [src/main3d.cpp](../../src/main3d.cpp) | Window dims passed at InitWindow time; Demo3D reads correct dims via GetScreenWidth/Height immediately, no after-init reconciliation needed |
| `--cascade-c0-res=N` CLI flag + `setCascadeC0Res(int)` public setter | [src/demo3d.h](../../src/demo3d.h), [src/demo3d.cpp](../../src/demo3d.cpp), [src/main3d.cpp](../../src/main3d.cpp) | Cascade probe-grid resolution scaling for headless captures (8/16/24/32/48/64 per existing ImGui dropdown) |
| `--raymarch-steps=N` CLI flag + `setRaymarchSteps(int)` inline setter | same files | Raymarch fragment-shader step count, uniform-only (no GPU resource changes) |
| `--gi-blur-radius=N` CLI flag + `setGIBlurRadius(int)` inline setter | same files | Bilateral GI blur kernel radius, clamped to [1, 8] (matches ImGui range) |

**Total net new code: ~80 lines** across 3 source files. No new shaders,
no new permanent GPU resources (codex 13 F5: `setCascadeC0Res` does
trigger runtime reallocation of existing cascade textures at new
probe-res dimensions — ~1-2 s overhead per change, memory footprint
shifts with probe-res). Per-pass GPU timing comes from RenderDoc's
existing `glPushDebugGroup` labels — **but** see the variance caveat
below (codex 13 F2): RenderDoc reliably covers large-difference
measurements (Exp 1+2 class) but NOT small-difference measurements
(Exp 3+4 class).

---

## Code Highlights

### `--window-size` parse-before-init pattern (codex 11 F2+F5)

`SetWindowSize()` is not used anywhere in the codebase, and calling it
AFTER `InitWindow(DEFAULT...)` would leave `Demo3D` constructed with
stale 720p dims (FOV-fit math at
[demo3d.cpp:5083-5088](../../src/demo3d.cpp#L5083) inside
`applyOBJViewPreset()`, viewport, GI blur FBO).
Correct approach: parse `--window-size` BEFORE `InitWindow` and pass
the user dims directly:

```cpp
// In main(), BEFORE initializeApplication():
int wWidth  = DEFAULT_WIDTH;
int wHeight = DEFAULT_HEIGHT;
for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--window-size=", 0) == 0) {
        int w = 0, h = 0;
        if (std::sscanf(arg.substr(14).c_str(), "%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
            wWidth  = w;
            wHeight = h;
        }
    }
}
initializeApplication(wWidth, wHeight);   // signature now (int, int)
```

`initializeApplication` signature changed from `()` to `(int windowWidth,
int windowHeight)`. Inside, the `InitWindow()` call uses the parameters
instead of hardcoded `DEFAULT_WIDTH/HEIGHT`. Default behavior unchanged
(no flag → still 1280×720).

### `setCascadeC0Res` — the only non-trivial setter (codex 12 F2+F8)

The 3 scaling-experiment setters land in different complexity tiers.
Two are uniform-only (inline in header). Codex 13 F7: manual `if`-clamp
in `setGIBlurRadius` instead of `std::clamp` is intentional — avoids
pulling `<algorithm>` into `demo3d.h` (currently not included).
Functionally identical to the plan's `std::clamp(v, 1, 8)`.

```cpp
void setRaymarchSteps(int v) {
    raymarchSteps = v;
    std::cout << "[Demo3D] raymarchSteps=" << v << "\n";
}
void setGIBlurRadius(int v) {
    if (v < 1) v = 1;
    if (v > 8) v = 8;
    giBlurRadius = v;
    std::cout << "[Demo3D] giBlurRadius=" << giBlurRadius << "\n";
}
```

The cascade-res setter is out-of-line and reproduces the existing
ImGui handler's destroy/init cycle PLUS the codex 08 lighting-
invalidation extras for clean EMA seeding:

```cpp
void Demo3D::setCascadeC0Res(int v) {
    if (cascadeC0Res == v) return;
    cascadeC0Res = v;
    destroyCascades();           // matches ImGui handler at demo3d.cpp:793-801
    initCascades();
    cascadeReady        = false;
    forceCascadeRebuild = true;  // codex 08-style: bypass stagger so all 4 cascades dispatch on next frame
    renderFrameIndex    = 0;     // ensure --auto-rdoc captures all 4 cascades
    historyNeedsSeed    = true;  // EMA history was zeroed in initCascades; seed cleanly
    // NOT meshSDFReady = false (codex 12 F8 partial reject):
    // probe-res change does not affect the SDF voxel grid -- probes SAMPLE
    // the SDF, they don't define it. SDF is sized by volumeResolution (128).
}
```

The `meshSDFReady` partial reject is the same lesson codex 08 F1+F7
taught for the strip toggle: don't conflate geometry-change
invalidation (5 lines incl. `meshSDFReady`) with lighting/structure-
change invalidation (4-5 lines without). Probe-res changes the cascade
probe atlases, not the SDF.

### CLI parser pattern (matches Step 10 `--camera-pos`)

All 3 new flags use `std::atoi` for integer values + explicit error
logging on parse failure. Codex 13 F6: parser pattern in this codebase
is `sscanf` for multi-value flags (`--window-size`, `--camera-pos`,
`--camera-target`) and `atoi` for single-value flags — the 3 new
scaling knobs follow the single-value precedent. Both trigger the same
MSVC C4996 nag.

```cpp
} else if (arg.rfind("--cascade-c0-res=", 0) == 0) {
    int v = std::atoi(arg.substr(17).c_str());
    if (v > 0) {
        demo->setCascadeC0Res(v);
        std::cout << "[MAIN] --cascade-c0-res=" << v << "\n";
    } else {
        std::cerr << "[MAIN] --cascade-c0-res: expected positive int (got: '"
                  << arg.substr(17) << "')\n";
    }
}
```

Identical pattern for `--raymarch-steps=N` and `--gi-blur-radius=N`.

---

## Verification

### Build
- `cmake --build build --config Release` — 0 errors, only baseline warnings
- 2 new sscanf-deprecation nags consistent with the existing codebase
  (Step 10 `--camera-pos` has the same; would need `_CRT_SECURE_NO_WARNINGS`
  to silence; not worth the noise).

### Smoke test (one capture exercises all 4 flags)
```
RadianceCascades3D.exe \
  --window-size=320,180 \
  --load-obj=cornell \
  --cascade-c0-res=16 --raymarch-steps=64 --gi-blur-radius=4 \
  --camera-pos=1.0710,-0.0723,-0.3393 \
  --camera-target=0.1212,-0.0812,-0.6520 \
  --exit-frames=10
```
Log confirms:
- `[MAIN] --window-size=320,180`
- `[INIT] Window created at 320x180`
- `[Demo3D] cascadeC0Res=16 (cascade reallocated; SDF unchanged)`
- `[Demo3D] raymarchSteps=64`
- `[Demo3D] giBlurRadius=4`
- Cascade re-init line shows `Cascade 0: 16^3 probes` (was 32^3)

### Capture pipeline (existing infrastructure, just exercised)
22 RenderDoc captures landed end-to-end across both plans:
- 2 baseline (1080p forced + 720p re-cap from `gi_pass_1080p_perf_analysis_plan.md`)
- 20 scaling experiment (5 + 6 + 5 + 4 across 4 axes)

Per-pass GPU times auto-extracted to
`tools/analysis/rdoc_frame_frame<N>_pipeline.md` via the existing
`tools/rdoc_extract.py` + `tools/analyze_renderdoc.py` chain. No
manual extraction required.

### Cleanest signal in the dataset
Experiment 2 (probe-res sweep at fixed 320×180 window):
**raymarch flat at 1.97 ms across all 6 probe-res values (8/16/24/32/48/64).**
This is the cleanest empirical signal in the entire perf-tooling
work — proves raymarch is NOT cascade-bound. Same for GI blur (~225 µs
flat). Cascade C0 bake on the same sweep grows ~267× (167 µs → 44.6 ms),
confirming volume-boundness with sub-cubic memory-bound scaling.

---

## Codex Findings Folded In

### Codex 11 (1080p perf analysis plan review)
- F1: line ref `:2721` → `:4721` (rdocForceRebuildCount)
- **F2+F5**: `SetWindowSize` after-init wrong → parse-before-init refactor
- F3: 2.25× raymarch scaling labelled estimate (±15%)
- F4: Phase ordering strict (Phase 1 build → Phase 2 captures)
- F6: cross-check vs CPU `cascadeTimeMs` (no log line yet, deferred)
- F7: parser pattern references Step 10 `--camera-pos` precedent
- F8: "6 dispatch sites" → "10 sites; 6 per-frame + 4 mesh-bake one-shots"

### Codex 12 (scaling experiment plan review)
- F1: `volumeResolution` is runtime, not compile-time (deferral rationale corrected)
- **F2**: All 3 setters explicitly added (none existed)
- F3: runtime estimate 5 min → 7 min (cascade re-allocation overhead)
- F4: `giBlurRadius` member default is 8, not 1 (flag table corrected)
- F5: `--window-size` already implemented (codex misread)
- F6: full filenames instead of "codex N" shorthand
- F7: `raymarchSteps` default 256 documented; Experiment 3 sweep dropped 512
- **F8**: Partial reject on `meshSDFReady = false` — probe-res change doesn't
  affect SDF voxel grid (same lesson as codex 08 F1+F7)

---

## Architecture Notes

**The capture-and-analyze pipeline is the actual MVP.** The CLI flags
are small (~80 lines net) but they unlock the whole headless-perf-
measurement workflow: any future perf change can now be measured at
arbitrary window/probe/step/blur configurations without touching the
ImGui or recompiling. The 20-capture sweep was practical because the
flags + the existing `--auto-rdoc` + `tools/rdoc_extract.py` pipeline
all compose cleanly.

**Init-order matters.** The codex 11 F2+F5 catch (parse-before-init
vs SetWindowSize-after-init) is the kind of subtle bug that would
have produced wrong measurements (Demo3D constructed at 720p but
window resized to 1080p afterward → FOV-fit math + viewport size
mismatch → silently incorrect data). The "simpler is correct"
principle applied here: pass dims to InitWindow directly, no
reconciliation.

**Setter invalidation chains are now a recognized codebase pattern.**
The codex 08 / codex 12 distinction between geometry-change (5 lines
incl. `meshSDFReady`) vs lighting-or-structure-change (4 lines
without) keeps surfacing. Each new setter that triggers a cascade
rebake is one careful read of "does this affect the SDF voxel grid
or just the cascade probe atlases?" If just probes → 4-line pattern.
If voxel grid → 5-line pattern.

**Single-shot RenderDoc captures have ±2-5× variance — and codex 13 F8
elevates this: variance can FLIP the direction of results, not just
inflate magnitudes.** Config B's 720p measurement at 341.7 ms vs
Config A's 1080p at 82.8 ms is a 4× INVERSION of the expected ordering.
Without variance control, before/after optimization comparisons can
show the "optimized" version as slower than the baseline. The scaling
experiment confirmed the magnitude side empirically: Exp 3 (raymarch
step count) and Exp 4 (blur radius) showed wild capture-to-capture
variance that swamped the actual workload differences. For precise
scaling slopes, need GPU clock locking (NVIDIA Inspector), N-capture
averaging, OR `cascadeTimeMs` stdout cross-check (codex 11 F6 open
item — escalated to "recommended before any before/after measurement").
Exp 1 + 2 worked because the workload differences (window pixel count
or probe-res cubed) were large enough to overcome the noise.

**ImGui handler vs CLI setter invalidation difference is intentional
(codex 13 F3).** [demo3d.cpp:792-801](../../src/demo3d.cpp#L792)'s
ImGui handler for `cascadeC0Res` does 3-line invalidation (destroy/init
+ `cascadeReady=false`). The CLI `setCascadeC0Res` does 6 lines (adds
`forceCascadeRebuild=true`, `renderFrameIndex=0`, `historyNeedsSeed=true`).
The two paths have different convergence constraints:

| Use case | Constraint | Required |
|---|---|---|
| **ImGui interactive** | User changes dropdown, continues viewing | Eventual convergence over a few stagger cycles is fine |
| **CLI + `--auto-rdoc`** | Capture fires at +8s; staggered cascades wouldn't all be re-baked | Force all 4 cascades to dispatch on next frame |

Both are correct for their use case. If a future ImGui feature needs
single-frame convergence (e.g., "snapshot to PNG from button"), the
ImGui handler should adopt the 6-line pattern. Not needed today.

---

## Known Open Items

| Item | Notes |
|---|---|
| In-app per-pass `GL_TIME_ELAPSED` overlay | Would let live ImGui display per-pass µs without RenderDoc capture; codex 11 F6's `cascadeTimeMs` cross-check needs this too |
| Stdout-log `cascadeTimeMs` for headless cross-check (codex 11 F6) | Currently only displayed in ImGui ([demo3d.cpp:3654](../../src/demo3d.cpp#L3654)) and JSON dumps — never to stdout, so headless captures can't cross-check |
| Volume resolution CLI flag | `volumeResolution` is runtime-changeable in principle but requires reallocation infrastructure for 6+ volume textures; out of scope for Step 12 |
| GPU clock locking for variance reduction | Would defeat the single-shot capture noise that defeated Exp 3+4 |
| N-capture averaging in `tools/rdoc_extract.py` | Same goal as above — extract from N captures, average per-pass times, surface std-dev |

---

## What's Next (revised after codex 13 F4)

**Path 2 BEFORE Path 1.** The codex 13 F4 + F8 evidence is decisive:
single-shot RenderDoc variance can FLIP the direction of results
(Config B 720p was 4× SLOWER than 1080p). Implementing Tier 1
optimizations without variance control would produce before/after
comparisons indistinguishable from noise — a "successfully
optimized" run could appear slower than baseline purely due to
power-state variance.

### Path 2 first — variance control (~1-2 days tooling)

1. **Stdout-log `cascadeTimeMs`** for headless cross-check (codex
   11 F6, escalated by codex 13 F2). Currently only displayed in
   ImGui ([demo3d.cpp:3654](../../src/demo3d.cpp#L3654)) and JSON
   dumps. Adding a one-line `std::cout` in headless / verbose modes
   provides a CPU-side sanity check independent of RenderDoc.
2. **N-capture averaging in `tools/rdoc_extract.py`** — extract
   from N captures (e.g., N=5), compute per-pass mean + std-dev,
   surface uncertainty bars in the analysis report. Defeats the
   single-shot variance.
3. **Optional: GPU clock locking** via NVIDIA Inspector (out-of-app
   tooling). Removes power-state variance entirely but requires
   manual setup per measurement session.

### Path 1 second — Tier 1 optimizations (with measured before/after)

After path 2 lands, implement and measure:

- Raymarch step cap (`--raymarch-steps=128`)
- Lower upper-cascade probe-res (C2=4³, C3=2³)
- Half-res raymarch + bilateral upsample
- Disable AABB clamp on temporal blend

Each with **N=5 captures averaged** so the before/after delta has
meaningful uncertainty bars. Without path 2's tooling, these
measurements are untrustworthy — don't run them.
