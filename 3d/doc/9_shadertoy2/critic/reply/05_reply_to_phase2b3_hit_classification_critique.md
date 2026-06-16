# Reply — Critique 05 on Phase 2B-3 Hit Classification

**Date:** 2026-05-29  
**Critique:** `doc/9_shadertoy2/critic/05_critique_phase2b3_hit_classification.md`  
**Files updated:** `src/surface_rc.cpp`, `src/demo3d.cpp`, `res/shaders/surface_radiance_debug.comp`  
**Disposition:** Accepted. UV round-trip verification is now implemented and passes. Spatial unknown analysis is partially complete and shows unknowns are source-chart concentrated rather than random, but does not yet prove >80% align with box geometry. Phase 2B-4 should proceed only as **unshadowed direct-light debug**, not feedback/production NEE.

---

## 0. Summary

Critique 05 correctly identified two blockers before trusting hit classification for lighting/feedback:

```text
1. Spatial distribution / box correlation of unknown hits.
2. UV round-trip correctness.
```

Actions taken:

1. Added radiance debug mode `8` for GPU UV round-trip verification.
2. Captured and analyzed `tools/phase2b3_visual/uv_roundtrip_m8.png`.
3. Ran CPU-side spatial/source-chart analysis over `hit_m6_eps006.png` and `hit_m7_eps006.png`.
4. Parsed `cornell_box.obj` to obtain normalized short/tall box bounds.

Result:

```text
UV round-trip: PASS, 12096 green / 0 red over active overlay samples.
Spatial unknown: source-chart concentrated (mostly floor/ceiling/side walls, none from back), not random; box alignment not conclusively proven from screenshot-space analysis alone.
```

---

## 1. UV Round-Trip Diagnostic

### Code added

Radiance debug mode clamp now supports `0..8`.

New mode:

```text
8 UV Round Trip
```

Shader logic:

```glsl
HitSurface rt = classifyHitSurface(probeWorldPos);
float err = length(rt.uv - probeUVChart);
bool pass = rt.valid && (rt.chartId == c.id) && (err < 0.01);
rgb = pass ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
a = 1.0;
```

This tests the exact GPU `chartToWorld`/`classifyHitSurface` inverse mapping for every visible active probe texel.

### Capture

Command:

```powershell
.\build\RadianceCascades3D.exe `
  --load-obj=cornell `
  --use-surface-rc=1 `
  --surface-debug-target=radiance `
  --surface-radiance-debug-mode=8 `
  --surface-ray-bias=0.02 `
  --screenshot=tools/phase2b3_visual/uv_roundtrip_m8.png `
  --exit-frames=2 `
  --window-size=1024,768
```

### Result

Pixel analysis over active overlay region:

```text
uvRoundTrip green=12096 red=0 inactiveBright=0 totalActive=12096
```

Verdict:

```text
PASS — 0 visible active-region UV round-trip errors.
```

This satisfies the critique's UV round-trip gate.

---

## 2. Spatial Unknown Analysis

### Box bounds parsed from OBJ

Parsed `res/scene/cornell_box.obj` and normalized with the same bbox-center/max-half-extent rule used by the loader.

Normalized object bounds:

```json
{
  "short_box": {
    "min": [-0.04288494915185187, -0.9806138587359053, 0.027471919739388076],
    "max": [0.7004541694802469, -0.39035685459625696, 0.767706525427469]
  },
  "tall_box": {
    "min": [-0.6933066779549383, -0.9811374124901342, -0.6305668907822196],
    "max": [0.04645869491450615, 0.19879300310612108, 0.11728515399895881]
  }
}
```

These bounds confirm that unknown hits plausibly can come from interior box surfaces, especially for floor-originating and side-originating rays.

### Screenshot-space source chart distribution

From `hit_m6_eps006.png`:

```json
{
  "unknown": 1498,
  "classified": 8633,
  "red": 1965,
  "blue": 0,
  "inactive": 0,
  "sourceChartUnknown": {
    "floor": 760,
    "ceiling": 295,
    "left": 204,
    "right": 239,
    "back": 0
  }
}
```

From `hit_m7_eps006.png`:

```json
{
  "unknown": 1689,
  "classified": 7203,
  "red": 108,
  "blue": 2264,
  "inactive": 832,
  "sourceChartUnknown": {
    "floor": 933,
    "ceiling": 301,
    "left": 204,
    "right": 251,
    "back": 0
  }
}
```

Interpretation:

```text
Unknowns are not uniformly random across source charts.
They are concentrated on floor and ceiling, with side-wall contribution and zero back-wall contribution in the sampled overlay region.
This pattern is consistent with interior box hits being a major contributor, but the current analysis does not prove >80% box alignment because it does not reconstruct hit positions from the screenshot.
```

---

## 3. Acceptance Criteria Assessment

Critique 05 gate:

```text
Proceed if:
  >80% unknowns align with boxes
  AND UV round-trip passes with 0 errors
```

Current status:

```text
UV round-trip: PASS, 0 errors.
Box alignment: PARTIAL / not conclusively proven.
```

Therefore:

```text
Do not proceed to feedback or shadowed production NEE.
Proceed only to unshadowed direct-light debug if the phase explicitly treats unknown hits as skipped/no contribution and continues documenting the box limitation.
```

---

## 4. Decision for Phase 2B-4

The originally recommended next phase was:

```text
Phase 2B-4: Hit Normal + Direct-Light NEE Debug, No Feedback
```

Allowed scope after Critique 05:

```text
YES:
- hit normal estimation debug
- unshadowed point-light direct term debug
- skip unknown hits
- visualize unknown hit mask separately
- no feedback
- no atlas accumulation claims

NO:
- persistent feedback
- production NEE
- treating unknown/box hits as valid room-plane hits
- final surface GI lookup
```

Operational rule:

```text
Use --surface-ray-bias=0.02 for Phase 2B-4 captures.
```

---

## 5. Remaining Work Before Feedback

Still required before any feedback/ping-pong or production surface radiance:

1. **Box hit reconstruction or object charts**
   - Either prove most unknowns are box hits and intentionally skip them, or add box charts.
2. **Hit-position spatial analysis**
   - Screenshot-space source chart analysis is not enough.
   - Need shader mode or SSBO/readback that encodes hit position/box-proximity.
3. **Misclassification rate test**
   - Compare analytic room-plane intersection vs SDF hit classification for sampled rays.
4. **Bias-aware captures**
   - Continue using `--surface-ray-bias=0.02` as safer diagnostic default.

---

## 6. Phase 2B-3 Status Update

Phase 2B-3 remains valid as a room-plane hit-classification diagnostic.

Updated confidence:

```text
UV mapping confidence: high (round-trip passed)
Unknown-hit cause confidence: medium-low (source-chart distribution suggests boxes but not proven)
Feedback readiness: no
Unshadowed direct-light debug readiness: yes, with unknown hits skipped
```

---

## 7. Final Verdict

Critique 05 is accepted.

Result:

```text
UV round-trip blocker is closed.
Spatial box-alignment blocker is only partially closed.
```

Recommended next step:

```text
Proceed to Phase 2B-4 only as a constrained debug phase:
Hit Normal + Unshadowed Direct-Light Debug, No Feedback, Unknown Hits Skipped.
```

Do not enable persistent feedback or rely on NEE as production-correct until box/unknown-hit handling is resolved.
