# Phase 4b-1 Debug & UI Clarity Learnings

**Date:** 2026-04-24  
**Branch:** 3d  
**Status:** Complete  
**Follows:** `phase4b_impl_learnings.md` (core scaling) + Codex critic `05_phase4b_impl_learnings_review.md`

---

## What Was Added

### 1. Per-cascade probe-luminance distribution histogram

16-bin histogram computed per cascade during GPU readback:

```cpp
// 16-bin probe-luminance distribution — spatial histogram across all res^3 probes.
float histMax = std::min(mean * 4.0f, maxLum);  // adaptive upper bound
int rawBins[BINS] = {};
for (int i = 0; i < probeTotal; ++i) {
    float lum = (buf[i*4+0] + buf[i*4+1] + buf[i*4+2]) / 3.0f;
    int bin = static_cast<int>(lum / histMax * BINS);
    bin = std::max(0, std::min(BINS - 1, bin));
    ++rawBins[bin];
}
// normalize: tallest bin = 1.0
```

Stored in `float probeHistogram[MAX_CASCADES][16]`. Displayed with `ImGui::PlotHistogram`, labeled `C%d r=%d` to show the ray count per cascade.

### 2. Cascade-wide luminance distribution variance

```cpp
// Cascade-wide luminance distribution variance: E[X^2] - E[X]^2 over all res^3 probes.
// NOT per-probe Monte Carlo variance — includes scene spatial structure. Heuristic only.
probeVariance[ci] = sumLum2 / float(probeTotal) - mean * mean;
```

Shown in the stats row as `dist_var=` (renamed from `var=` to signal it is a distribution metric, not a noise estimate). Hover tooltip spells out the distinction.

### 3. Hit-type heatmap — radiance_debug.frag mode 4

New visualization mode decoding the packed probe alpha into surf/sky/miss spatial fractions:

```glsl
// radiance_debug.frag — mode 4
uniform int uRaysPerProbe;  // new uniform, pushed from cascades[selC].raysPerProbe

float N    = float(max(uRaysPerProbe, 1));
float skyF  = floor(packed / 255.0 + 0.5) / N;
float surfF = mod(packed + 0.5, 255.0) / N;
float missF = max(0.0, 1.0 - surfF - skyF);
fragColor = vec4(missF, surfF, skyF, 1.0);  // R=miss  G=surf  B=sky
```

`radiance_debug.frag` previously had no mode > 3 defined. Mode 3 ("Direct") was a placeholder that just called `sampleSlice` without any special handling — mode 4 is the first genuinely new branch.

### 4. Radiance debug mode cycling — KEY_F and radio buttons

`radianceVisualizeMode` was initialized to 0 but had no runtime setter — the `[F] Cycle` note in the overlay UI was aspirational only. Fixed:

- `processInput()`: `KEY_F` cycles `radianceVisualizeMode` 0–4 with console log
- `renderCascadePanel()`: radio buttons for all 5 modes appear when `showRadianceDebug` is ON

### 5. `(?)` helper markers across the Cascades panel

Added a local `HelpMarker` lambda at the top of `renderCascadePanel()`:

```cpp
auto HelpMarker = [](const char* desc) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", desc);
};
```

`ImGuiHoveredFlags_DelayShort` adds a brief hover delay before the tooltip appears, matching the ImGui demo convention and preventing tooltip flicker while moving the mouse across the panel.

Markers added to every section:

| Section | Key content of tooltip |
|---|---|
| Cascade Count | Per-level distance bands, merge chain direction |
| Disable Merge | Merge ON = correct RC; OFF = per-level debug isolation |
| Environment Fill | OFF = honest black; ON = sky propagates down merge chain |
| Ray Count Scaling | Doubling formula, slider ceiling reason (packed-alpha decode) |
| Render using cascade | Which cascade drives modes 3 and 6; per-level description |
| Radiance debug mode | What each of the 5 modes shows; HitType color key |
| Probe Fill Rate | any%/surf%/sky% definitions; color threshold explanation |
| Probe-Luminance Distribution | Spatial distribution vs Monte Carlo noise; heuristic caveat |
| C0 Spot Samples | Which world positions; what non-zero values confirm |

Also added `r=N rays` inline in the per-level interval table lines so ray counts are visible without scrolling to the readback section.

---

## Codex Critic Corrections Applied to Code (not just doc)

The Codex `05_phase4b_impl_learnings_review.md` identified that the histogram and variance were described as noise metrics. The doc was corrected first; then the code UI labels were aligned:

| Change | Before | After |
|---|---|---|
| Stats row label | `var=0.00000` | `dist_var=0.00000` |
| Stats row hover | (none) | Tooltip: "cascade-wide distribution variance… NOT per-probe Monte Carlo" |
| Histogram section header | `"Probe luma histograms (16 bins…)"` | `"Probe-Luminance Distribution:"` + `(?)` marker |
| Variance code comment | `// Var = E[X^2] - E[X]^2` | 3-line comment explaining spatial scope and heuristic-only status |
| Histogram code comment | `// 16-bin luma histogram…` | `// 16-bin probe-luminance distribution — spatial histogram across all res^3 probes.` |

---

## Key Learnings

### Spatial distribution ≠ per-probe Monte Carlo variance

`probeVariance[ci]` is `E[lum²] - E[lum]²` computed across all `res³` probes in the cascade texture — one value per cascade, not one per probe. It measures how spread out the probe luminances are across the scene, which is dominated by real scene structure (light gradients, wall albedo, ceiling vs floor brightness). True per-probe sampling variance would require either:

- A second per-probe buffer storing `E[X²]` separately
- Running the cascade with two different random seeds and differencing the results

Neither is currently in scope. The existing metric is useful **only** as a heuristic when comparing the same scene at different ray counts — scene structure cancels out, leaving the sampling noise difference as the residual signal. A narrow distribution is consistent with reduced noise but not proof.

### Why the slider ceiling is 8, not 32

The packed alpha decode `int(packed / 255.0f + 0.5f)` returns wrong `skyH` for `surfH >= 128` because `packed/255 ≥ 0.5` rounds up to 1. At base=8, C3 fires 64 rays maximum → `surfH ≤ 64` → `64/255 ≈ 0.25 < 0.5` → rounds to 0 → decode correct. Safe. Python-verified before implementation.

The correct decode for arbitrary N is integer arithmetic:
```cpp
int packed_int = static_cast<int>(packed + 0.5f);
int skyH  = packed_int / 255;
int surfH = packed_int % 255;
```
This is exact for all `surfH ≤ 254`. Fixing this is deferred to a debug-encoding cleanup pass, not 4b scope.

### ImGui helper marker pattern

The standard pattern from the ImGui demo:
```cpp
auto HelpMarker = [](const char* desc) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", desc);
};
```

Using `ImGuiHoveredFlags_DelayShort` prevents tooltips from popping immediately while the user's mouse is in motion — important for dense panels where cursoring to a control crosses many `(?)` markers. Using `SetTooltip("%s", desc)` (not `SetTooltip(desc)`) avoids format-string injection from tooltip content.

### `radianceVisualizeMode` was a dead control

The radiance debug UI overlay showed `[F] Cycle visualize mode` but `processInput()` had no `KEY_F` handler and no other code ever mutated `radianceVisualizeMode` at runtime. The control was initialized to 0 in the constructor and stayed there. This pattern (aspirational UI text backed by no implementation) is worth watching for in future panels.

---

## Observed at Runtime

- Build: 0 errors, 30 pre-existing warnings (unchanged)
- `(?)` markers appear correctly with hover delay across all sections
- Mode 4 (HitType) is reachable via radio button and via `[F]` key cycling
- `dist_var=` label and tooltip visible on stat row hover

---

## Open Questions

- Should the packed-alpha decode be fixed to integer arithmetic now (before 4c) so the slider ceiling can be safely raised to 16?
- Would a dedicated per-probe variance buffer (second RGBA16F texture) be worth the memory cost for 4c validation?
- The 30 pre-existing warnings include signed/unsigned mismatch in `obj_loader.h` and unused parameter — worth a cleanup pass?
