## Reply: GI 1080p Perf Plan Codex Review — `11_gi_pass_1080p_perf_analysis_plan_review.md`

**Date:** 2026-05-11
**Status:** All 8 findings accepted. F2+F5 are the load-bearing
catches — using `SetWindowSize()` after `InitWindow(DEFAULT...)`
would have left `Demo3D` constructed with stale 720p dimensions
(viewport, FOV-fit camera preset, GI blur target). Correct approach
is to defer `InitWindow` until AFTER parsing the `--window-size`
flag and pass the user-chosen dimensions directly. That's the
plan's main revision. Other findings are doc accuracy (line refs,
dispatch-group count, sequencing notes, scaling caveat).

---

### F2 + F5 — `SetWindowSize()` after `InitWindow` would leave Demo3D with stale dims (MEDIUM + MEDIUM, plan revision)

You're right, and this is the load-bearing catch. The current init
order in `main3d.cpp` is:

```
InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, ...)   // line 430 -- 720p hardcoded
glewInit()                                       // line 449
Demo3D* demo = new Demo3D()                      // line ~490
                                                 //   - reads GetScreenWidth/Height for viewport
                                                 //   - reads GetScreenWidth/Height in applyOBJViewPreset
                                                 //     for FOV-fit math (demo3d.cpp:4993-4998)
                                                 //   - GI blur FBO sized to current screen dims
```

If I parsed `--window-size=1920,1080` AFTER `InitWindow` and called
`SetWindowSize(1920, 1080)` to resize, then constructed `Demo3D`,
two things go wrong:

1. The window resize is asynchronous on some platforms — the next
   `GetScreenWidth()` call may still return 1280 until the OS
   actually completes the resize.
2. Even if synchronous, the FBO/viewport setup inside `Demo3D`
   captures the dims at construction time. A subsequent
   `--window-size` change inside the constructor would be too late
   for the cascade probe-grid + blur FBO allocation that already
   happened.

**Plan revision** — flip the init order:

```cpp
// 1. Parse argv FIRST (currently done after InitWindow)
int wWidth  = DEFAULT_WIDTH;
int wHeight = DEFAULT_HEIGHT;
for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--window-size=", 0) == 0) {
        std::sscanf(arg.substr(14).c_str(), "%d,%d", &wWidth, &wHeight);
    }
    // (other flags can stay parsed where they are -- they don't affect window init)
}

// 2. THEN initializeApplication() with parsed dims (replaces the
//    hardcoded DEFAULT_WIDTH/HEIGHT call inside it)
initializeApplication(wWidth, wHeight);

// 3. THEN Demo3D construction -- now picks up the right dims
//    everywhere via GetScreenWidth/Height
```

`SetWindowSize()` is not called at all. The window is born at the
right size. F5's viewport / aspect / blur-FBO concerns dissolve
because nothing is "updated after" — everything reads the correct
dims at construction time.

This is also a SIMPLER fix than my original plan (no resize call,
no after-init reconciliation) and matches how the codebase already
treats `InitWindow` as the single source of dimensional truth.

---

### F1 — Wrong line ref for `rdocForceRebuildCount` (LOW, doc fix)

You're right. `rdocForceRebuildCount=2` is at
[demo3d.cpp:4721](src/demo3d.cpp#L4721), not `:2721-area`. I
truncated the leading "4". The forceCascadeRebuild + renderFrameIndex
sustain logic is at lines 869-875.

**Doc fix.** Reference corrected.

---

### F3 — 1080p raymarch scaling estimate may not be linear (MEDIUM, doc fix)

You're right that pixel-count linear scaling (`2.25×`) is a first
approximation. At higher resolution:

- More pixels hit surface (proportional to window area)
- BUT marched step count per surface-hitting pixel stays roughly
  the same (geometry is fixed; step size is per-voxel, not
  per-pixel)
- Empty-space pixels early-terminate the same way
- Depth complexity (multi-surface alpha-composite) doesn't change

So the LINEAR pixel-count scaling should hold to within ~10%
unless GPU-architectural effects (warp coherence, texture-cache
locality) kick in differently at higher res. The 2.25× estimate
is probably correct ±15%.

**Doc fix.** Replaced "1080p raymarch ~2.25×" with "**~2.25×
estimated**, actual scaling will be confirmed/corrected by the
1080p capture (real raymarch could land ±15% of this estimate
depending on warp coherence and texture-cache behavior at the new
resolution)".

---

### F4 — Config C depends on Phase 1 (LOW, doc fix)

You're right. Phase 2's Config C says "1080p headless" but that
needs the `--window-size` flag from Phase 1. My plan implies they're
independent steps, but they're sequential.

**Doc fix.** Phases are now explicitly numbered with prerequisite
notes: Phase 1 builds + tests the flag → Phase 2 runs all 3 captures
(all of which now require the new build) → Phase 3 extracts → Phase 4
analyzes. Cannot reorder.

---

### F6 — Existing `cascadeTimeMs` CPU-side timing is a free cross-check (LOW, doc add)

You're right. [demo3d.cpp:885](src/demo3d.cpp#L885) already does
`cascadeTimeMs = (GetTime() - t0) * 1000.0;` — CPU wall-clock around
the cascade dispatch loop. Less precise than RenderDoc's per-dispatch
GPU timer (CPU wall-clock includes driver overhead, doesn't
distinguish per-cascade), but a useful sanity cross-check: if
RenderDoc says total cascade GPU time is X ms, the CPU `cascadeTimeMs`
should be ≥ X ms (CPU includes GPU + driver wait). A wild divergence
would mean either RenderDoc's timer is wrong or the cascade loop
has CPU-side overhead worth investigating.

**Doc add.** Cross-check step added to Phase 4: read existing
`cascadeTimeMs` from app log lines and compare to RenderDoc cascade
total. They should be in the same ballpark.

---

### F7 — Parser pattern reference (LOW, doc add)

You're right that I should reference the existing comma-separated
parsing precedent. Step 10's `--camera-pos=x,y,z` parser at
[main3d.cpp:236-244](src/main3d.cpp#L236) uses
`std::sscanf(arg.substr(13).c_str(), "%f,%f,%f", &x, &y, &z) == 3`
with explicit error logging on parse failure. The `--window-size`
flag follows the same pattern with `%d,%d`.

**Doc add.** Reference to the Step 10 precedent included in the
plan's reuse section.

---

### F8 — "6 dispatch sites" should be "10" (LOW, doc fix)

You're right. I miscounted. The 10 `glPushDebugGroup` sites are:

1. GPU JFA SDF (line 1694)
2. GPU triangle voxelize (line 1811)
3. sdf_analytic (line 1997)
4. radiance_3d (line 2147)
5. reduction_3d (line 2168)
6. temporal_blend × 2 (lines 2210, 2220)
7. raymarch (line 2434)
8. gi_blur (line 2530)

The "6 GI passes" framing in cerebrum.md only counts the
per-frame steady-state passes (sdf_analytic + radiance_3d +
reduction_3d + temporal_blend + raymarch + gi_blur), not the
mesh-SDF-bake one-shot passes (GPU JFA, GPU voxelize) that fire
on OBJ load.

**Doc fix.** Updated to "10 `glPushDebugGroup` sites; 6 are
per-frame GI passes, 4 are mesh-SDF-bake one-shots that fire on
load only".

---

### Summary

| # | Sev | Action | Result |
|---|---|---|---|
| F1 | Low  | Doc fix  | `:2721-area` → `:4721` |
| F2 | Med  | Plan revision | `InitWindow(W, H)` directly, no `SetWindowSize` after-init resize |
| F3 | Med  | Doc fix  | 2.25× labelled as estimate; actual scaling confirmed by capture |
| F4 | Low  | Doc fix  | Phases now explicitly sequential (Phase 1 → Phase 2 prerequisite) |
| F5 | Med  | Plan revision (= F2) | Demo3D construction now reads correct dims at init time, no after-init reconciliation needed |
| F6 | Low  | Doc add  | Cross-check RenderDoc cascade total vs existing `cascadeTimeMs` CPU log |
| F7 | Low  | Doc add  | Parser pattern references Step 10 `--camera-pos` precedent |
| F8 | Low  | Doc fix  | "6 dispatch sites" → "10 `glPushDebugGroup` sites; 6 per-frame + 4 mesh-bake one-shots" |

**Bottom line.** F2+F5 was the structural catch — `SetWindowSize`
after `InitWindow` would have left `Demo3D` with stale 720p
dimensions for FOV-fit math, viewport, and GI blur FBO. The
correct fix is simpler: parse `--window-size` BEFORE `InitWindow`
and pass the user dims directly. The other six are doc accuracy
(line ref, scaling caveat, phase sequencing, dispatch count,
parser-pattern reference, CPU-timing cross-check). Plan is now
implementation-ready.