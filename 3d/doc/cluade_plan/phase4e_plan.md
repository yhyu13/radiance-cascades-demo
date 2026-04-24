# Phase 4e Plan — Packed-Decode Fix + Debug Polish

**Date:** 2026-04-24  
**Branch:** 3d  
**Depends on:** Phase 4a–4d complete  
**Goal:** Close out Phase 4 with three targeted improvements: (A) fix the packed-alpha CPU decode (slider ceiling stays at 8 — RGBA16F storage is the real limit), (B) add blend-zone metadata to the cascade interval table so Phase 4c can be validated in-panel, and (C) add a cross-cascade mean-luminance bar chart and per-cascade probe-coverage bars for Phase 4a and merge coherence readability.

---

## Why Now

Phase 4a–4d are functionally correct, but two readback/UI gaps limit validation:

1. **Float decode is imprecise:** The packed-alpha CPU decode uses `int(packed/255 + 0.5)` which rounds incorrectly for certain integer boundaries. The integer arithmetic decode (`packed_int / 255`, `packed_int % 255`) is strictly better and removes a second rounding layer on top of the storage quantization.

   **Note:** The slider ceiling stays at base=8. The deeper limit is RGBA16F precision — half-float is exact for integers up to 2048, but `packed = surfH + skyH * 255` exceeds 2048 once `skyH ≥ 9` (i.e., 9 × 255 = 2295). Python scan of all valid C3-at-base=8 pairs found 1,184 of 2,145 combinations already corrupt under RGBA16F when both skyH and surfH are non-zero. Stats are exact when env fill is OFF (skyH = 0). The proper fix (separate `GL_RG32UI` buffer) is deferred to a cleanup pass.

2. **No 4c readback feedback:** After implementing the distance-blend merge there is no in-panel display of where the blend zone is. The cascade stats row shows `[tMin, tMax]` but not `[blendStart, tMax]`.

3. **Hit-type and luminance data underused:** `probeMeanLum`, `probeSurfaceHit`, `probeSkyHit` are computed every bake but only surfaced as text. Visual bars make per-cascade comparisons instantaneous.

---

## Part A — Packed-Alpha Integer Decode

### Problem

`src/demo3d.cpp:410–411`:
```cpp
int skyH  = static_cast<int>(packed / 255.0f + 0.5f);
int surfH = static_cast<int>(std::fmod(packed, 255.0f) + 0.5f);
```

For any `packed` value that falls on an odd integer boundary above 2048, the float division introduces an extra rounding step on top of the RGBA16F quantization. Integer arithmetic avoids this:

```cpp
int packed_int = static_cast<int>(packed + 0.5f);
int skyH  = packed_int / 255;
int surfH = packed_int % 255;
```

This is strictly more correct — it removes the float rounding on the CPU side. It does not fix the RGBA16F storage precision, which is the dominant source of error for mixed surf/sky probes at C3.

### HelpMarker update

Replace the ceiling explanation with an honest description of the RGBA16F limit:

```
"Hit counts (surf%/sky%) are packed into the probe texture alpha (RGBA16F).\n"
"Half-float is exact for integers up to 2048; packed = surfH + skyH*255\n"
"exceeds this when skyH >= 9 (9*255=2295). Stats are most reliable when\n"
"env fill is OFF (skyH=0 throughout, packed stays <= 64).\n\n"
"The slider ceiling stays at 8: the storage format, not the CPU decode,\n"
"is the real limit. A separate integer buffer is needed to go higher safely."
```

---

## Part B — Blend Zone in Cascade Interval Table

The per-level stats row currently shows `[tMin, tMax]`. Add a second indented line with blend zone annotation:

For cascades C0–C2: `  blend=[blendStart, tMax]m  (outer N%%)`  
For C3: `  blend: GUARDED (no upper cascade)`  
When `blendFraction < 0.01`: `  blend: OFF (binary mode)`

Pure CPU math — no readback needed:
```cpp
float d      = cascades[ci].cellSize;
float tMin_c = (ci == 0) ? 0.02f : d * std::pow(4.0f, float(ci - 1));
float tMax_c = d * std::pow(4.0f, float(ci));
float bw     = blendFraction * (tMax_c - tMin_c);
float bStart = tMax_c - bw;
```

| Cascade | tMin | tMax | blend zone at f=0.5 |
|---|---|---|---|
| C0 | 0.02 | 0.125 | [0.073, 0.125] m (outer 50%) |
| C1 | 0.125 | 0.500 | [0.313, 0.500] m (outer 50%) |
| C2 | 0.500 | 2.000 | [1.250, 2.000] m (outer 50%) |
| C3 | 2.000 | 8.000 | GUARDED |

---

## Part C — Visual Bars

### C1: Cross-cascade mean luminance chart

One 4-bar `ImGui::PlotHistogram` using `probeMeanLum[0..cascadeCount-1]`. Range ceiling is the maximum `probeMeanLum` across all active cascades × 1.5 (not anchored to C0, which can be unrepresentative):

```cpp
float lumCeil = 1e-4f;
for (int i = 0; i < cascadeCount; ++i)
    lumCeil = std::max(lumCeil, probeMeanLum[i]);
lumCeil *= 1.5f;

float meanLums[MAX_CASCADES];
for (int i = 0; i < cascadeCount; ++i)
    meanLums[i] = probeMeanLum[i];
ImGui::PlotHistogram("##meanlum", meanLums, cascadeCount, 0,
                     nullptr, 0.0f, lumCeil, ImVec2(180, 50));
ImGui::SameLine();
for (int i = 0; i < cascadeCount; ++i)
    ImGui::Text("C%d %.4f\n", i, probeMeanLum[i]);
```

### C2: Per-cascade probe-coverage bars

`probeSurfaceHit[ci]` and `probeSkyHit[ci]` count probes with *at least one* hit of that type — they are overlapping "any-hit" flags, not exclusive ray fractions. Present as two independent coverage bars, not a partition:

```cpp
float probeF  = float(probeTotal);
float surfF   = float(probeSurfaceHit[ci]) / probeF;
float skyF    = float(probeSkyHit[ci])     / probeF;

ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
ImGui::ProgressBar(surfF, ImVec2(80, 6), "");   // green — surf coverage
ImGui::PopStyleColor();
ImGui::SameLine(0, 4);
ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
ImGui::ProgressBar(skyF, ImVec2(80, 6), "");    // blue — sky coverage
ImGui::PopStyleColor();
ImGui::SameLine();
ImGui::TextDisabled("surf-cov  sky-cov  (probe any-hit; may overlap)");
```

No miss bar — `1 - surfF - skyF` is not a valid miss fraction since surf and sky sets overlap.

---

## Files Touched

| File | Change |
|---|---|
| `src/demo3d.cpp` readback loop | Integer decode (2 lines) |
| `src/demo3d.cpp` HelpMarker tooltip | RGBA16F precision honest explanation; slider ceiling stays 8 |
| `src/demo3d.cpp` cascade stats | Blend zone annotation (second line per cascade) |
| `src/demo3d.cpp` cascade panel | Mean-lum bar chart with max-across-cascades ceiling |
| `src/demo3d.cpp` cascade stats | Surf-cov / sky-cov probe bars (no miss bar) |

No shader changes. No new textures. No new uniforms.

---

## Validation

| Test | Method | Expected |
|---|---|---|
| Decode correctness | Env fill OFF: sky% should read 0 | 0% sky |
| RGBA16F stats with env fill ON | sky% may show imprecise values at C3 | Approximate; tooltip explains |
| Blend zone table | `blendFraction=0.5`: C0 shows `[0.073, 0.125]m`, C3 shows `GUARDED` | Matches Python math |
| Probe coverage bars | Env fill OFF: sky-cov bar empty | 0 width |
| Probe coverage bars | Env fill ON: sky-cov bar fills | > 0 width |
| Mean-lum chart | All 4 bars visible after merge | No clipping |

---

## 4c A/B Validation (Required — Not Yet Run)

Before declaring Phase 4 complete, run and record:

1. Bake with `blendFraction = 0.0` (binary mode). Note any visible cascade boundary bands.
2. Bake with `blendFraction = 0.5` (default). Compare with step 1.
3. Document result in `phase4c_impl_learnings.md` under "A/B Result".

Acceptable outcomes:
- **Visible smoothing** at cascade boundaries → 4c is effective.
- **No visible difference** → banding is directional-mismatch driven (Phase 5 target). Record explicitly.

---

## Phase 4 Completion Gate

- [x] 4a: Env fill toggle
- [x] 4b: Per-cascade ray scaling
- [x] 4c: Distance-blend merge
- [x] 4d: Filter verification (no-op — confirmed)
- [x] 4e: Packed-decode fix + debug polish
- [x] 4c A/B result: **no visible difference, mean-lum unchanged** — 4c confirmed no-op for this scene

## 4c A/B Validation Result

**Outcome:** No visible difference. Mean luminance per cascade unchanged between `blendFraction=0.0` (binary) and `blendFraction=0.5` (blended).

**Interpretation:** The banding in this scene is driven by directional mismatch — the upper cascade contributes its isotropic average along the specific missed-ray direction, not the actual radiance for that direction. Smoothing the binary boundary handoff has no effect because the underlying value being blended toward is already wrong directionally. This is the known Phase 5 target.

**4c is a correct implementation of a no-op for this scene.** The code is forward-compatible: when Phase 5 provides per-direction `upperSample`, the blend formula is unchanged and will become meaningful.
