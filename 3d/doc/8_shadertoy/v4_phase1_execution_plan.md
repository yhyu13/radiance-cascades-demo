# v4 Phase 1 Execution Plan

**Date:** 2026-05-28  
**Scope doc:** `doc/8_shadertoy/v4_shadertoy_adoption_scope.md`  
**Goal:** Ship Sponza per-scene MB-gain preset + confirm and document Cornell constraint

---

## Phase 1A — Sponza Per-Scene MB-Gain Preset

### Step 1: Add CLI flag (`--mb-gain-per-scene`)

**File:** `src/main3d.cpp`

After existing CLI parse block for `--multi-bounce-gain`:
```cpp
// After the --multi-bounce-gain handler (~line ~3700):
else if (arg == "--mb-gain-per-scene") {
    g_usePerSceneMbGain = true;  // file-scope static bool, default false
}
```

**File:** `src/demo3d.h` — new accessor:
```cpp
void setUsePerSceneMbGain(bool v) { usePerSceneMbGain = v; }
// member:
bool usePerSceneMbGain = false;
```

### Step 2: Apply per-scene gain on scene load

**File:** `src/demo3d.cpp` — after `loadOBJMesh` block (~line 7149):

```cpp
// v4 Phase 1A: per-scene MB-gain preset (ShaderToy adoption closeout)
if (usePerSceneMbGain) {
    if (isSponza) {
        setMultiBounceGain(0.10f);
        std::cout << "[v4] Per-scene MB gain: Sponza → 0.10\n";
    } else {
        // Keep default 1.0 for Cornell and other scenes
        std::cout << "[v4] Per-scene MB gain: " << sceneType << " → 1.0 (default)\n";
    }
}
```

**Important:** This runs AFTER `loadOBJMesh` sets `isSponza` flag and configures the default gain. Must use the setters (which trigger cascadeRebuild). Ordering:
1. `loadOBJMesh` sets `isSponza`, resets light params, sets default MB gain
2. Stage 11c/11d CLI re-apply block (light-direction/position)
3. **NEW:** v4 per-scene MB gain apply

### Step 3: ImGui status display

**File:** `src/demo3d.cpp` — add to status bar (~line 6350):

```cpp
if (usePerSceneMbGain) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
        "| MB-gain: %.2f (per-scene auto)", multiBounceGain);
}
```

Also: when `usePerSceneMbGain` is true, gray out the manual `multiBounceGain` slider in the GI Quality Presets section. Add a tooltip: "Controlled by --mb-gain-per-scene; disable to manually tune."

### Step 4: Capture and verify

```powershell
# Build
cmake --build build --config Release --target RadianceCascades3D

# Sponza cascade-OFF at gain=0.10 with per-scene flag
.\build\RadianceCascades3D.exe `
    --load-obj=sponza `
    --measurement-cameras-file=tools/v20_pre_measurement/sponza_cam.json `
    --measurement-camera=0 `
    --use-multi-bounce=1 --mb-gain-per-scene=1 `
    --cascade-scaled-dir-res=1 --noise-seed-offset=0 --use-probe-jitter=1 `
    --render-mode=17 --screenshot-exr=1 --auto-capture-delay=0 `
    --exit-frames=2048 --screenshot=phase1a_sponza_g010_pscene.png

# Verify |p95| ≤ 0.30
python tools/v3_baseline/analyze_baselines.py --scene=sponza --hybrid=0 --n 2048 `
    --out tools/v3_baseline/phase1a_sponza_metrics.json

# Regression: Sponza without per-scene flag (should match M0 baseline)
.\build\RadianceCascades3D.exe `
    --load-obj=sponza `
    ...same... --use-multi-bounce=1 `
    ... --exit-frames=2048 --screenshot=phase1a_sponza_g100_default.png

# Verify output matches M0 baseline within 5%
python tools/v3_m1_delta36/analyze_matrix.py  # reuse existing analyzer
```

**Gate:** |p95| ≤ 0.30 AND default-mode metrics within ±5% of M0 baseline_lock.json.

---

## Phase 1B — Cornell Confirm & Document

### Step 5: Cornell directional confirmation

```powershell
# Cornell directional (Stage 11c reproduce)
.\build\RadianceCascades3D.exe `
    --load-obj=cornell `
    --light-direction=0,-1,0 `
    --use-multi-bounce=1 --multi-bounce-gain=1.0 `
    --measurement-camera=0 `
    --cascade-scaled-dir-res=1 --noise-seed-offset=0 --use-probe-jitter=1 `
    --render-mode=17 --screenshot-exr=1 --auto-capture-delay=0 `
    --exit-frames=2048 --screenshot=phase1b_cornell_dir_m17.png
```

Run `analyze_baselines.py` on the output. Verify ratio_self ≥ 0.85.

### Step 6: Document Cornell constraint

Write `doc/8_shadertoy/cornell_point_light_constraint.md` covering:
- The volumetric topology limitation (probe rays under-sample non-uniform lit surfaces)
- The 2× under-emit is structural, not a code bug
- Proved by: directional light fixes it (ratio 0.49 → 0.93)
- Fix ceiling: Path B (surface-attached) or hybrid (correction layer)
- Decision: hybrid stays ON for Cornell-class scenes

### Step 7: Update baseline_lock.json

Add entries:
```json
"phase1a_sponza_g010_pscene": {
    "tag": "v4_sponza_cam0_mbon_g010_hyb0_N2048_m17_pscene",
    "N": 2048,
    "status": "complete",
    "metrics": { "ratio_self": 1.040, "abs_p95": 0.253, ... }
},
"phase1b_cornell_dir_m17": {
    "tag": "v4_cornell_cam0_dir_hyb0_N2048_m17",
    "N": 2048,
    "status": "complete",
    "metrics": { "ratio_self": 0.935, ... }
}
```

---

## Verification Checklist

| Check | Command | Expected |
|-------|---------|----------|
| Build | `cmake --build build --config Release` | No errors, no new warnings |
| Sponza pscene | capture + analyze | |p95| ≤ 0.30, ratio ≈ 1.04 |
| Sponza default regression | capture + analyze | metrics within ±5% of M0 lock |
| Cornell directional | capture + analyze | ratio ≥ 0.85 |
| Lock update | `python tools/v3_baseline/build_baseline_lock.ps1` | All entries present with SHA256 |

---

## Rollback Plan

If Sponza gain=0.10 with per-scene flag produces |p95| > 0.50:
1. Verify the gain slider function: log `multiBounceGain` at shader uniform setup time
2. Check build mode (Release vs Debug might change timing → different temporal accumulation)
3. If persistent, delay Phase 1A and investigate configuration drift

If any build step fails:
1. Fix the build error (check API changes in `setMultiBounceGain`)
2. Re-run all captures in this phase