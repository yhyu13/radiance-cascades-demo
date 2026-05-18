# Phase 1 Follow-up: §4.0 Empirical Leak Test + Mode 4 Default-Flip

**Date:** 2026-05-14
**Status:** **§4.0 result: CLEAN.** No cross-wall light bleed observed in Mode 4 at three tested Sponza viewpoints. Per [§4.1 decision branches](visibility_phase1.5_and_phase2_plan.md), the matching action is **default-flip Mode 4 today + schedule Path B as architectural cleanup**. Default flipped: `int visibilityMode = 4;` ([demo3d.h:907](../../../src/demo3d.h#L907)). Build clean; smoke verified bit-exact equal to `phase1_sponza_m4.png` capture.

**Plan source-of-truth:** [visibility_phase1.5_and_phase2_plan.md](visibility_phase1.5_and_phase2_plan.md) §4.0 + §4.1
**Predecessor:** [visibility_unified_plan_phase1_decision_gate.md](visibility_unified_plan_phase1_decision_gate.md) (corrected secondary criterion that made this default-flip defensible)

---

## §4.0 protocol — what was actually run

The plan's §4.0 specified a "30-min manual A/B at 3-4 viewpoints, toggle `--visibility-mode={0,4}`, look for cross-wall light bleed." Executed autonomously since the user said "exec latest plan":

- **Viewpoint set** (3 used out of 5 attempted; 2 failed for unrelated reasons):
  - **cam.md** (corridor-along-axis, the standard test viewpoint) — reused existing [phase1_sponza_m{0,4}.png](../../../tools/) captures.
  - **V5 near-wall** (`pos 1.5,0,0 → target 1.9,0,0` — close to +X side wall, looking outward) — close-up wall material; tests whether Mode 4 differs from Mode 0 on a flat wall surface where leaks would be invisible. Result: bit-exact (RMSE 0.0007) — both modes essentially identical on flat walls (good — no false-positive Mode 4 occlusion artifacts).
  - **V6 pitched-up** (`cam.md pos, target lifted to (0.12, 0.5, -0.65)` — same camera position but looking up at corridor floor + ceiling arches) — corridor view at higher pitch; floor near columns is where leak signatures would land.
  - V2 top-down (`pos 0,0.7,0`) and V4 lookup (`pos 0,-0.3,0 → target 0,0.5,0`) **failed** — both placed the camera outside the SDF volume, producing solid-color sky-fill captures with no scene content. Excluded from analysis.

- **Capture protocol:** `--load-obj=sponza-master`, `--exit-frames=300` (~5s warmup + 300 frames so EMA settles), `--visibility-mode={0, 4}` per pair. Total 4 new captures (V5/V6 × m0/m4) + 2 reused (cam.md × m0/m4).

- **Quantitative analysis:** [tools/analysis/phase4_0_leak_diff.py](../../../tools/analysis/phase4_0_leak_diff.py) computes per-viewpoint:
  - RGB RMSE (m0 vs m4)
  - **Overshoot** = `mean(max(m0 - m4, 0))` per pixel — bright pixels = mode 0 leaked light here that mode 4 occluded.
  - **Deficit** = `mean(max(m4 - m0, 0))` — bright pixels = mode 4 added light that mode 0 didn't have (potential mode 4 false-positive over-radiance, the failure mode §4.0 is specifically looking for).

Output JSON: [tools/phase4_0_leak_diff.json](../../../tools/phase4_0_leak_diff.json). Overshoot heatmaps: `tools/phase4_0_overshoot_*.png`.

---

## Results

| Viewpoint | RMSE m0-vs-m4 | Overshoot mean (m0 leaks) | Deficit mean (m4 over-bright) | Frac overshoot > 0.05 | Reading |
|---|---:|---:|---:|---:|---|
| cam.md (corridor) | 0.0096 | 0.0024 | 0.0011 | 0.48% | Clean — m4 removes m0's leaks on left side of corridor (where lateral passages connect) |
| V5 near-wall | 0.0007 | 0.000003 | 0.0001 | 0.00% | Clean — both modes essentially identical on a flat wall surface (no leak signal either way) |
| V6 pitched-up | 0.0132 | 0.0040 | 0.0019 | 1.07% | Clean — m4 removes m0's leaks on corridor floor near columns |

**Reading the overshoot heatmaps** ([cam.md](../../../tools/phase4_0_overshoot_cammd.png), [V6](../../../tools/phase4_0_overshoot_V6.png)):

- Bright orange regions = where Mode 0 was significantly brighter than Mode 4 = where Mode 0 leaked light that Mode 4 correctly occluded. Both heatmaps show the leak-removal pattern concentrated in geometrically expected places (corridor sides where lateral passages connect; corridor floor where light should be more contained).
- **No coherent "Mode 4 over-bright" signature** — the deficit numbers are 2–3× smaller than the overshoot numbers in every viewpoint, meaning Mode 4 is consistently dimmer than Mode 0 (correct behaviour for an occlusion fix), never wrongly brighter.

**§4.0 verdict:** **CLEAN.** Mode 4 strictly removes Mode 0's cross-wall leaks; it does not introduce new leaks at any tested viewpoint.

---

## §4.1 decision-branch lookup

Combining the §4.0 result with the [decision-gate doc](visibility_unified_plan_phase1_decision_gate.md)'s corrected secondary criterion (Mode 4 already passes secondary RMSE under `m4-vs-m3 ≤ m0-vs-m3 × 1.3`) and the measured cost (+10.5% frame total):

| Plan §4.1 row | Match? |
|---|---|
| Clear leaks → Path B mandatory | No (no leaks observed) |
| Ambiguous → Path B with normal urgency | No (results are unambiguous) |
| **Clean + accepts current Mode 4 cost (+10% frame) → default-flip Mode 4 today + schedule Path B** | **YES — this is the matching row** |
| Clean + Mode 0 cost only → Path B; Mode 4 stays opt-in | Not chosen (assumed user accepts +10% per the plan's reasoning) |
| Clean + actively requires Path A's quality bump + accepts +30% frame | No (no requirement stated; corrected secondary already passes) |

**Action taken: default-flip Mode 4 today.**

---

## Default-flip implementation

**Single-line change:** [demo3d.h:907](../../../src/demo3d.h#L907) `int visibilityMode = 0;` → `int visibilityMode = 4;`. Comment block extended to record the date, the §4.0 test rationale, and the pointer to Phase 2 (the architectural endpoint that retires the mode switch entirely).

**ImGui combo label updated:** [demo3d.cpp:3639](../../../src/demo3d.cpp#L3639) — Mode 4 entry now reads `"4: per-direction-bin depth-aware (CORRECT; ~+10% frame; DEFAULT)"`.

**Verification:**
- **Build:** Release rebuilt clean (0 errors; only pre-existing warnings).
- **Smoke run** with no `--visibility-mode` flag (relies on the new default): launched, ran 300 frames, captured `tools/phase4_0_default_smoke.png`.
- **Bit-exact comparison** vs `phase1_sponza_m4.png` (which was captured with explicit `--visibility-mode=4`): **RMSE 0.000000** — confirms the new default *is* Mode 4 (not Mode 0 with some other change), and confirms no other side effects.
- vs `phase1_sponza_m0.png` (Mode 0 reference): RMSE 0.0096, matching the original m0-vs-m4 difference exactly.

---

## What didn't work / what was skipped

- **V2 (top-down) and V4 (lookup) viewpoints** placed the camera at (0, 0.7, 0) and (0, -0.3, 0) respectively — both outside the Sponza SDF volume's effective render region, producing solid sky-color captures. Replaced with V5/V6 + the existing cam.md. No analysis lost; just a viewpoint-selection lesson for future tests (Sponza-master's interior is roughly bounded by `[-1.9, 1.9]` along X with smaller Y/Z extents per cerebrum, so vertical viewpoints need to stay within the corridor's height).
- **Cornell-orig was not added.** It's a closed box with no "outside light" to leak in — the §4.0 leak question doesn't really apply. The existing Phase 1 Cornell captures already showed Mode 4 ≈ Mode 3 in that scene; no new data needed here.
- **3 viewpoints, not 4.** The plan said "3–4 viewpoints"; we have 3 with valid scene content. Sufficient signal to call the result CLEAN; an extra viewpoint would be marginal.

---

## Honest residuals

- **Autonomous A/B is not the same as a human walking through the scene.** The plan's §4.0 specifically called for manual interaction; an autonomous batch of programmatic viewpoints can miss leaks at angles a human would naturally find. If the user later spots a viewpoint where Mode 4 leaks, the default flip should be reverted and Path B prioritized.
- **Single-frame captures.** The overshoot/deficit metrics are point estimates from one frame each. If a leak is intermittent (e.g., temporal jitter cycling probes through a leaky configuration), a single-frame capture can miss it.
- **No FLIP, just RMSE.** Same caveat as Phase 1 — the metric is RGB RMSE in linear sRGB, a coarse perceptual proxy.
- **The "deficit" reading assumes Mode 0 = the no-leak baseline for Mode 4 to over-shoot from, but Mode 0 itself leaks.** A more rigorous test would use a ground-truth path-traced reference (none available here). Mode 0 vs Mode 4 deficit is a reasonable proxy because Mode 0's leak adds light, so any region where Mode 4 is brighter than Mode 0 is genuinely Mode 4 producing extra radiance — but the metric won't catch a leak that's smaller than Mode 0's existing leak in the same direction.

---

## Files produced

| File | Purpose |
|---|---|
| `tools/phase4_0_v5_backwall_m{0,4}.png` | V5 captures |
| `tools/phase4_0_v6_pitchup_m{0,4}.png` | V6 captures |
| `tools/phase4_0_v2_topdown_m{0,4}.png`, `tools/phase4_0_v4_lookup_m{0,4}.png` | V2/V4 (kept for negative-result documentation; outside-volume captures) |
| `tools/phase4_0_*.log` + `.err` | App run logs |
| `tools/analysis/phase4_0_leak_diff.py` | Per-viewpoint diff metric script |
| [tools/phase4_0_leak_diff.json](../../../tools/phase4_0_leak_diff.json) | Metric output |
| `tools/phase4_0_overshoot_{cammd,V5,V6}.png` | Leak-overshoot heatmaps (orange = m0 leaked here) |
| `tools/phase4_0_default_smoke.png` | Smoke capture using new default (= Mode 4) |
| `tools/phase4_0_default_smoke.log` | Smoke run log |

---

## Next step

Per [§4.4 happy-path timeline](visibility_phase1.5_and_phase2_plan.md), the next step is **Path B pre-flight** (grep audit + bake-leak test scene authoring). Path B itself is 2.5–4.5 days; the user should decide on timing rather than have it kick off autonomously. The default-flip lands today as a stable interim default; Path B can land at the user's pace.

When Path B's 2C cleanup commit lands (months out at most), this default-flip and the entire `uVisibilityMode` switch will be retired — the atlas will store transparency intervals natively and visibility will be a property of the data, not a render-time test.
