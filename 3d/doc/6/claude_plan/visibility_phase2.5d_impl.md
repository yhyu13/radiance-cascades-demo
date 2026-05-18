# Phase 2.5d — Implementation Notes: Critic-11 Polish Bundle

**Date:** 2026-05-15
**Status:** All four items (M1, L2, L3, L5 from critic 11) landed + verified. **Critic 12 fixes also folded** (H2 shader-load fail-banner; M4 combined-flag warning; L3 setter early-return; L4 named constants; H1 doc tightening). Default rendering bit-exact match to Phase 2 baseline (RMSE 0.000000). M1 produced empirical histogram of SDF-before-hit values; analysis tightened per critic 12 H1 (the histogram supports the operational claim about over-darkening but does NOT prove the geometric origin of the small-α distribution).

**Critic chain extends:** [critic 12](critic/12_visibility_phase2.5d_impl_review.md) — 2 HIGH (H1 wording, H2 shader-load fail-fast) + 5 MEDIUM (M1 GUI verify, M2 hard-coded coords, M3 L5 incomplete, M4 combined flags, M5 workflow patterns) + 4 LOW (L1 histogram bins, L2 closed-attribution, L3 redundant-rebuild, L4 named constants). H1, H2, M4, L3, L4 applied; M1, M2, M3, M5, L1, L2 acknowledged.

**Critic chain extends further:** [critic 10 (re-review)](critic/10_visibility_phase2.5d_impl_review.md) → [reply 10](critic/reply/reply_10_visibility_phase2.5d_impl_review.md). 8 findings (1 HIGH, 3 MEDIUM, 4 LOW). **W1 elevated to actual code** (per-shader criticality + nonzero exit on critical-shader-load failure). W2 cross-verified the 2.5b formula. W3 increased histogram from 16 to 64 bins (revealed bimodal structure: ~19% at α-floor, ~78% main cluster at α∈(0.06, 0.14), with a gap between). W4 added scene-validation to `--cam-preset=alcove` (verified: warns + skips when used with sponza-master). W5 promoted to cerebrum entry (RGBA16F denormal-flush as project-wide constraint). W6 added `data_kind` JSON field. W7 cited shader code; W8 wording tightened.

**Plan source-of-truth:** chosen interactively from critic 11 deferred items list. No standalone plan doc — this impl doc is also the plan.

**Predecessors:**
- [visibility_phase2.5_impl.md](visibility_phase2.5_impl.md) — Phase 2.5 (a/b reverted/c) shipped
- [11_visibility_phase2.5_impl_review.md](critic/11_visibility_phase2.5_impl_review.md) — critic flagged M1, L2, L3, L5 as deferred items
- [reply_09_visibility_phase2_impl_review.md](critic/reply/reply_09_visibility_phase2_impl_review.md) — Phase 2 critic chain history

---

## Summary

| Item | Severity | What it does | Outcome |
|---|---|---|---|
| **M1** | MEDIUM | SDF-before-hit distribution measurement (was: conceptual diagnosis only; should have been measured) | **Confirms 2.5b failure root cause**: mean α = 0.088, 99% < 0.25. The smoothstep `[0, voxelSize]` maps these to α ≈ 0.025, explaining the over-darkening. |
| **L2** | LOW | `--cam-preset=alcove` CLI for the cornell-orig-alcove view | View focused on the right alcove; previously had to specify `--camera-pos` + `--camera-target` manually |
| **L3** | LOW | Atlas debug viewer label/tooltip | Warns users that the Atlas mode shows raw RGB ignoring α; bake-time leaks visible there are expected |
| **L5** | LOW | Bake-leak deadlock warning at app shutdown | Stderr warning if `--bake-leak-test` was scheduled but never fired (cascade never ready, OR `--exit-frames` too short) |

Net code delta: ~+50 lines (M1 shader uniform + C++ histogram + CLI; L2 CLI + cliCameraPos staging; L3 ImGui label; L5 shutdown check).

---

## M1 — SDF-Before-Hit Distribution Measurement

### Goal

Confirm or refute the conceptual diagnosis from Phase 2.5b: "the SDF-half-voxel-before-hit metric returns small values for ALL hits, not just head-on; the smoothstep `[0, voxelSize]` therefore mapped most surface bins to α near zero, causing 32% over-darkening."

### Implementation

**Shader** ([radiance_3d.comp](../../../res/shaders/radiance_3d.comp)):
- New uniform `int uDiagAlphaMode` (default 0).
- When `uDiagAlphaMode == 1`, surface-hit bins write `clamp(sdfBefore / voxelSize, 1e-3, 0.999)` to α INSTEAD of the binary `0`. Sky/miss bins are unchanged (α=0/1).
- The clamp range `[1e-3, 0.999]` is strictly inside the (sky=0, miss=1) sentinels so a CPU-side histogram filter `(1e-4 < α < 0.9995)` cleanly identifies "diagnostic surface bin."

**Two non-obvious bugs found and fixed during implementation** (worth noting for future reference):

1. **Wrong uniform names**: I initially copied `uVolumeMax` / `uVolumeMin` from `raymarch.frag` — those aren't defined in `radiance_3d.comp` (which uses `uGridSize` / `uGridOrigin`). Shader compile FAILED silently — the demo continued running but with the radiance_3d kernel non-functional, producing garbage output. The error went to stderr but the demo's main loop didn't notice (the existing shader-load error path doesn't propagate to abort). **Fix**: use `uGridSize.x` and `textureSize(uSDF, 0).x` to derive SDF voxel size in the bake shader's local naming convention.

2. **RGBA16F denormal flush**: my first attempt clamped to `[1e-6, 1.0]`. Half-float minimum normal is ~6.1e-5; values below are denormals which some drivers flush to zero. The histogram showed C0/C1/C2 with zero entries (everything flushed), only C3 showed any data. **Fix**: clamp to `[1e-3, 0.999]` (well inside half-float normal range; `1e-3` is 16× larger than the min-normal threshold).

**C++** ([demo3d.cpp](../../../src/demo3d.cpp)):
- New member `int diagAlphaMode = 0` + `setDiagAlphaMode(int)` setter that triggers cascade rebuild.
- Extended `computeBakeLeakMetric()` to populate a 16-bucket histogram of α values (when `diagAlphaMode==1`). JSON output adds `diag_surface_total`, `diag_surface_mean`, `diag_histo_16` per cascade.
- Histogram filter `1e-4 < α < 0.9995` excludes sky/miss; bucket `i` covers α ∈ (i/16, (i+1)/16].

**CLI** ([main3d.cpp](../../../src/main3d.cpp)):
- New `--diag-alpha-mode=N` flag. Combine with `--bake-leak-test=path` to get histogram in the metric JSON.

### Result (cornell-orig-alcove, alcove probes only)

```
./RadianceCascades3D --load-obj=cornell-orig-alcove --diag-alpha-mode=1 \
                    --bake-leak-test=tools/phase2.5d_sdf_distribution_alcove.json \
                    --exit-frames=400
```

Output: [tools/phase2.5d_sdf_distribution_alcove.json](../../../tools/phase2.5d_sdf_distribution_alcove.json)

**C0 histogram** (32³ probes, 6144 in alcove, 44925 surface bins counted):

Initial 16-bin (Phase 2.5d v1, original):

| Bin | α range | Count | % | Bar |
|---|---|---:|---:|---|
| 0 | (0, 0.0625] | 8727 | 19.4% | `#########` |
| **1** | **(0.0625, 0.125]** | **30843** | **68.7%** | `##################################` ← peak |
| 2 | (0.125, 0.1875] | 5003 | 11.1% | `#####` |
| 3 | (0.1875, 0.25] | 351 | 0.8% | |
| 4–15 | > 0.25 | 1 | 0.0% | |

Refined 64-bin (post-critic-10 W3, current):

| Bin | α range | Count | % | Bar |
|---|---|---:|---:|---|
| 0 | (0.0000, 0.0156] | 8352 | 18.6% | `##################` ← **clamp-floor spike** |
| 1–3 | (0.0156, 0.0625] | 375 | 0.8% | (very sparse — near-empty gap) |
| 4 | (0.0625, 0.0781] | 4961 | 11.0% | `###########` |
| 5 | (0.0781, 0.0938] | 2498 | 5.6% | `#####` |
| 6 | (0.0938, 0.1094] | 10157 | 22.6% | `######################` |
| **7** | **(0.1094, 0.1250]** | **13227** | **29.4%** | `#############################` ← main peak |
| 8 | (0.1250, 0.1406] | 4197 | 9.3% | `#########` |
| 9–16 | (0.1406, 0.2656] | 1158 | 2.6% | (declining tail) |
| 17–63 | (> 0.2656) | 0 | 0% | |

**Mean α = 0.0885** (C0); 0.088–0.102 across all cascades. **99.2% of surface bins have α ≤ 0.1875.**

**The 64-bin histogram reveals BIMODAL structure** the 16-bin couldn't show:
- **Spike at bin 0 (α ≈ 0.001)** = 18.6% — these are the values that hit the lower clamp at `kDiagAlphaMin = 1e-3`. They represent surface bins where `sdfBefore` was near-zero or negative (true distance-to-wall basically zero at the half-voxel-back point).
- **Main cluster at bins 4–8 (α ∈ (0.0625, 0.1406])** = 78.0% — these are the "typical" hits where `sdfBefore / voxelSize` lands around 0.1.
- **Near-empty gap at bins 1–3** (α ∈ (0.0156, 0.0625]) = only 0.8% combined — almost nothing between the floor spike and the main cluster.

This bimodal shape suggests TWO populations of surface bins: ones that hit "into" the wall (clamp floor) and ones that hit "near" the wall surface (main cluster). The 16-bin histogram lumped them into a single "bin 1 = 68.7%" peak and missed this distinction. **For Phase 2.6 design, the floor-spike vs main-cluster distinction is exactly the kind of "head-on vs grazing" signal soft α was supposed to capture — but at α < 0.15 even the main cluster is too transparent for the smoothstep range Phase 2.5b chose.**

### Diagnosis result (revised per critic 12 H1)

**What the histogram measures**: the CLAMP OUTPUT `clamp(sdfBefore / voxelSize, 1e-3, 0.999)` for surface bins, NOT the smoothstep+mix output that the failed Phase 2.5b attempt actually wrote to α. The two differ by a smoothstep transformation.

**Walking the math forward from the histogram data** to what 2.5b would have produced:

For the typical bin with histogram-α = 0.088 (the diagnostic clamp output):

    Phase 2.5b actually wrote: mix(1e-3, 1.0, smoothstep(0, voxelSize, sdfBefore))
    where sdfBefore / voxelSize = 0.088
    → smoothstep(0, 1, 0.088) ≈ 3·(0.088)² − 2·(0.088)³ ≈ 0.0212
    → mix(0.001, 1, 0.0212) ≈ 0.0223

**Conclusion**: ~98% of surface bins WOULD have been written with α ≈ 0.022 by Phase 2.5b. The 32% over-darkening from 2.5b is consistent with this magnitude (visibility weight 0.022 vs Phase 2 binary α=0/1).

**What this conclusion DOES support**:
- The Phase 2.5b shader's smoothstep produces near-zero α for most surface bins.
- Mathematically, the over-darkening is explained by the near-zero α distribution.

**What this conclusion DOES NOT prove** (per critic 12 H1):
- Why `sdfBefore / voxelSize` is small for typical hits is itself unexplained. Naive expectation: a half-voxel back from a head-on hit, SDF ≈ voxelSize/2 → ratio ≈ 0.5. Observed ratio is 0.088 — 6× smaller than the naive expectation.
- Possible mechanisms (none verified): (a) hits are mostly grazing (unexpected for a closed Cornell-orig-alcove); (b) the conservative-SDF-band offset (per cerebrum 2026-05-07) interacts with the half-voxel-back sampling; (c) the SDF gradient near walls is nonlinear in the relevant range.
- A future Phase 2.6 would benefit from understanding WHY (so a different metric can be designed); M1 only confirms the WHAT.

**Bottom line**: M1's empirical histogram does support the operational claim "the smoothstep produced over-darkening" but the geometric origin of the small-α distribution remains an open question. Filed for future investigation.

### Implication for future Phase 2.6

- **The SDF-half-voxel-before-hit metric is geometrically the wrong input for soft α.** It measures "how close the half-voxel-back point is to a wall" — and for ANY hit, that's small. **The SDF SCALAR at a nearby point doesn't carry hit-angle information directly** (per critic 10 W8). The SDF GRADIENT at the hit point would (it's the surface normal — `dot(rayDir, normalize(grad SDF))` would give the hit angle), but Phase 2.5b sampled the scalar, not the gradient. Future Phase 2.6 attempts wanting hit-angle should compute the gradient via finite differences (cost: 6 SDF samples per bin to estimate the 3D gradient) — measurable cost adder, not in scope this round.
- **Three candidate metrics from critic 11 remain valid Phase 2.6 starting points**:
  1. Ray-vs-surface-normal angle (`dot(rayDir, surfaceNormal)`) — needs surface normal at hit, which `raymarchSDF` doesn't return.
  2. Distance from hit to nearest probe-cell boundary.
  3. Hit-distance fraction within cascade interval (`hit.a / tMax`).
- Each candidate would need its own diagnostic run before committing to a smoothstep range. The diagnostic infrastructure (`--diag-alpha-mode=1`) generalises: any future "test what gets written to α" can reuse it by adding a new `uDiagAlphaMode` value.

### Files touched (M1)

- `res/shaders/radiance_3d.comp` — uniform declaration + surface-bin α branch
- `src/demo3d.h` — `diagAlphaMode` member + `setDiagAlphaMode()`
- `src/demo3d.cpp` — uniform binding + histogram code in `computeBakeLeakMetric`
- `src/main3d.cpp` — `--diag-alpha-mode=N` CLI

---

## L2 — `--cam-preset=alcove` CLI

### Implementation

[main3d.cpp](../../../src/main3d.cpp): new `--cam-preset=NAME` flag. Currently supports `alcove` (cornell-orig-alcove right-side view at pos `(0.6, 1.0, 0.5)` → target `(0.6, 0.0, -0.5)`).

**Non-obvious detail**: my first attempt called `demo->setCameraPosition()` directly inside the CLI parser. The auto-fit camera in `loadOBJMesh` (which runs LATER in the startup sequence per the existing CLI deferred-action pattern) silently overrode it. **Fix**: stage the values into the existing `cliCameraPos{,Target}Set` flags, which apply after `loadOBJMesh` completes.

### Verification

```
./RadianceCascades3D --load-obj=cornell-orig-alcove --cam-preset=alcove \
                    --exit-frames=300 --screenshot=alcove_view.png
```

Captured [tools/phase2.5d_alcove_preset_v2.png](../../../tools/phase2.5d_alcove_preset_v2.png) — top-down view into the alcove showing the partition (white middle wall), red/green Cornell side walls, and the right alcove floor. Visually confirms the camera is positioned correctly.

### Files touched (L2)

- `src/main3d.cpp` — CLI parser branch (~10 lines)

---

## L3 — Atlas Debug Viewer Label

### Implementation

[demo3d.cpp:4455-4464](../../../src/demo3d.cpp#L4455): when the radiance debug viewer is in mode 3 (Atlas), an orange-text note appears below:

```
Atlas raw — each D×D block is one probe's directional bins
NOTE: shows raw atlas RGB; ignores α. Bake-time leaks visible here are expected
      (Phase 2: render-side α-gate hides leaks at render but atlas still contains them).
```

### Why it matters

Phase 2.5a.1 measured the bake-side leak as 4373.5 units (corrected v2). The atlas debug viewer displays this leakage directly. Without the label, a user toggling that mode would think the renderer is buggy — the label clarifies that the atlas IS leaking (Phase 2's known limitation) and the renderer correctly hides it.

### Verification (revised per critic 10 W7)

Build clean; in-app GUI behavior tested by code citation. The L3 label asserts the atlas debug viewer mode shows RAW RGB ignoring α. Verified against [res/shaders/radiance_debug.frag:140-145](../../../res/shaders/radiance_debug.frag#L140) (mode 3 path) and line 202 (`vec3 color = radiance.rgb * uIntensityScale;` — drops α before display). The shader does fetch `texture(uAtlasTexture, ...)` returning vec4 but uses only `.rgb` for rendering. **The label's claim is structurally correct in the current shader.**

### Files touched (L3)

- `src/demo3d.cpp` — ImGui::TextColored insertion in radiance debug panel

---

## L5 — Bake-Leak Deadlock Warning

### Implementation

[main3d.cpp](../../../src/main3d.cpp) — at app shutdown (after the main loop exits and before `delete demo`):

```cpp
if (demo->bakeLeakTestActive() && !demo->bakeLeakTestComplete()) {
    std::cerr << "[MAIN] WARN: --bake-leak-test was scheduled but never fired. "
              << "Either cascade never became ready (check --load-obj), or "
              << "--exit-frames was too short for the convergence wait "
              << "(default 240 frames). Increase --exit-frames and re-run.\n";
}
```

[demo3d.h](../../../src/demo3d.h) — added `bool bakeLeakTestActive() const { return bakeLeakTestPending; }` getter.

### Verification

```
./RadianceCascades3D --load-obj=cornell-orig-alcove --bake-leak-test=tools/test.json --exit-frames=20
```

(20 frames is too short — convergence wait is 240 frames.) **Stderr output**:

```
[MAIN] WARN: --bake-leak-test was scheduled but never fired. Either cascade never became
ready (check --load-obj), or --exit-frames was too short for the convergence wait
(default 240 frames). Increase --exit-frames and re-run.
```

JSON file `tools/test.json` correctly NOT created. Warning fires; user knows immediately why no JSON was written.

### Files touched (L5)

- `src/main3d.cpp` — shutdown warning (~6 lines)
- `src/demo3d.h` — `bakeLeakTestActive()` getter (1 line)

---

## Final Verification

- **Build**: Release rebuilt clean (0 errors after each commit).
- **Final regression**: post-2.5d default smoke run RMSE 0.000000 vs Phase 2 baseline (`phase2v5_post_sponza_cammd_m0.png`) — **bit-exact unchanged behavior** for the default render path.
- **M1 verification**: histogram data published to `tools/phase2.5d_sdf_distribution_alcove.json`. Mean α = 0.088 ± 0.014 across cascades.
- **L2 verification**: alcove view captured ([tools/phase2.5d_alcove_preset_v2.png](../../../tools/phase2.5d_alcove_preset_v2.png)).
- **L3 verification**: ImGui label code inspection; no GUI test framework but the conditional text is straightforward.
- **L5 verification**: stderr capture with `--exit-frames=20` shows the expected warning.

---

## Honest Residuals

- **The two bugs found during M1 implementation are noted in this doc but not separately filed.** Future shader work should: (a) match the local naming convention of the file being edited (don't blindly copy uniform names from another file); (b) avoid RGBA16F denormal range when storing diagnostic values.
- **The shader-compile-failed-silently bug is a pre-existing infrastructure issue.** When `loadShader` fails, the main loop continues with the broken shader. A "fail-fast on shader load failure" change would have caught my M1 bug immediately instead of producing garbage output that I then had to debug. **Filed**: improve shader-load error path to abort. Not in this commit.
- **L3 was a text-only change**; not subjected to actual GUI interaction testing. If the orange color is unreadable on the user's display, that's a UX gap.
- **L2 hard-codes `cornell-orig-alcove` coordinates inside main3d.cpp** instead of having the scene metadata define them. Same coupling concern as critic 11 L4 noted for the alcove filter constants. Acceptable scope; could refactor when scene metadata becomes a thing.
- **M1's SDF-before-hit metric was for the SPECIFIC failed Phase 2.5b derivation.** Other future Phase 2.6 candidates (ray-normal dot, cell-boundary distance, hit-distance fraction) would each need their own diagnostic — not measured here.
- **The diagnostic output overloads `bake-leak-test` JSON.** When `--diag-alpha-mode=1`, the leak metric numbers in the same file are NOT comparable to the Phase 2 baseline (because α is the diagnostic value, not the binary 0/1). **Important**: don't compare `phase2.5_bake_leak_baseline_v2.json` against `phase2.5d_sdf_distribution_alcove.json` — they measure different things.

---

## What's Next

- Default rendering unchanged (bit-exact). All four critic-11 deferred items closed.
- M1 result strengthens the Phase 2.6 design space: confirms NOT to use SDF-proximity alone.
- Phase 3 (bake-side leak fix) baseline still anchored at `tools/phase2.5_bake_leak_baseline_v2.json` (C0 leak 4373.5).
- Critic 11 leftovers: **M1, L2, L3, L5 closed**; **H1, H2, M2, M3, L4 closed in the prior critic-11 reply**; nothing else from critic 11 outstanding.

**Standby docs** (added 2026-05-15 after ShaderToy ground-truth review):
- [visibility_phase2.6_standby.md](visibility_phase2.6_standby.md) — **Phase 2.6 effectively closed.** ShaderToy uses BINARY visibility (no soft α anywhere); our pursuit of "soft α via smoothstep on a metric" was unmotivated by ground truth. Filed dead unless a SPECIFIC artifact appears that maps to a row in that doc's table.
- [visibility_phase3_standby.md](visibility_phase3_standby.md) — **Phase 3 has a concrete algorithm now**: ShaderToy `WeightedSample` adapted to 3D volumetric probes. ~1 week of implementation; ~0.5 ms bake cost; addresses the 4373.5 leak. Trigger conditions for actually starting it documented.

The visibility/GI subsystem is in a maintenance-only state. Phase 2.6 is filed dead per ground-truth analysis. Phase 3 is opt-in but no longer "research-level" — algorithm is identified.
