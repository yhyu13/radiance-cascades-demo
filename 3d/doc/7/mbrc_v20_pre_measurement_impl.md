# MBRC v2.0-pre Measurement — Implementation Notes

Companion to [mbrc_v20_pre_measurement_plan.md](mbrc_v20_pre_measurement_plan.md) (rev 2). Documents the engine-side instrumentation landed in this session and the deferred Python pipeline.

Date: 2026-05-21. Build: Release, MSVC, no new warnings.

## 1. Scope landed

Engine-side instrumentation only. The capture matrix can be driven end-to-end from the CLI:

- **Mode 20** — Error Decomposition Heatmap with 4 sub-modes (uniform `uErrorDecompMode`)
- **Mode 21** — Cascade Dominance Heatmap (camera-distance proxy)
- **Leave-one-out cascade attribution** (`--cascade-exclude=N`) via bake-chain skip+rewire
- **Per-run noise independence** (`--noise-seed-offset=N`)
- **Pinned measurement cameras** (`--measurement-camera=N` + `--measurement-cameras-file=path`) with all camera-mutating input suppressed
- **Cascade-config dump** (`--cascade-config-dump`) alongside the next screenshot
- **Capture harness stub** ([tools/v20_pre_measurement/run_v20_pre.ps1](../../tools/v20_pre_measurement/run_v20_pre.ps1))

Out-of-scope this session (documented in §6 Deferred):
- PT-cache EXR dumper
- `analyze.py` (RMSE + leave-one-out attribution + spatial-vs-angular decomposition)
- Verdict-rule evaluator (the §1 quality-gap diagnosis)

## 2. Architectural divergence from plan

The plan and critic-06 review were written under a **consume-time merge** mental model: "in the raymarch frag, when reconstructing GI, skip cascade N's contribution and renormalize weights." That mental model does not match the actual codebase.

The actual codebase merges cascades **at bake time**: each cascade's compute shader ([res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp)) reads the upper cascade's atlas via `uUpperCascadeAtlas` and blends `smoothstep(l)` *into* the lower cascade's stored result. The raymarch frag ([res/shaders/raymarch.frag](../../res/shaders/raymarch.frag)) then consumes only the lowest active cascade — it cannot independently choose to "skip cascade N."

Implication for leave-one-out: the only place a cascade can be excluded is during bake. I reinterpreted §3 M1 as:

1. **Skip** the excluded cascade's `updateSingleCascade()` dispatch entirely ([demo3d.cpp:2434](../../src/demo3d.cpp#L2434))
2. **Rewire** the cascade immediately below it (`excluded - 1`) to bind cascade `excluded + 1` as `uUpperCascadeAtlas` ([demo3d.cpp:2483-2486](../../src/demo3d.cpp#L2483-L2486))
3. **Extend** that cascade's `uCnMinRange` to `pow(4, excluded) × baseInterval` so its tMax covers the bracket the excluded cascade would have owned ([demo3d.cpp:2469-2473](../../src/demo3d.cpp#L2469-L2473))
4. **Adjust scale** factor (non-colocated only): the probe-grid scale ratio between cascade i and cascade i+2 is 4× instead of 2× ([demo3d.cpp:2494-2502](../../src/demo3d.cpp#L2494-L2502))
5. **Special-case `cascadeExclude == 0`**: cascade 0 is the consume atlas the frag shader reads directly. Override consume binding to read cascade 1 instead ([demo3d.cpp:2818](../../src/demo3d.cpp#L2818))
6. **Top-cascade edge** (`cascadeExclude == cascadeCount-1`): the cascade below has no `i+2` to fall back to. `hasUpper5d` correctly resolves false; `upperToCurrentScale=0`; the cascade gets no upper merge (closed top, tMax still extended).

Semantically equivalent to the plan's "remove cascade N's contribution to the final GI estimate," but mechanically different. The plan's "weight renormalization" phrasing does not apply because cascades aren't summed in the consume path — they're hierarchically blended at bake. See §5 self-critique item C1 for the analysis-tool implication.

## 3. Files touched

| File | Change |
|---|---|
| [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag) | +4 uniforms (`uCascadeExclude`, `uErrorDecompMode`, `uCascadeBaseInterval`, `uCascadeCountConsume`); Modes 20 & 21 dispatch |
| [src/demo3d.h](../../src/demo3d.h) | 5 state fields + 5 setters + `kMeasurementCameraSlots` constant + 3 slot arrays + `setMeasurementCameraSlot` |
| [src/demo3d.cpp](../../src/demo3d.cpp) | Skip-in-merge bake-chain rewiring (5 edits in `updateSingleCascade`, 1 in `updateAllCascades`); consume-side `selC` override; uniform binds in raymarch dispatch; `noiseSeedOffset → uMBFrameSeed`; jitter gating + camera pinning in `processInput`; `cascade-config.json` emitter in screenshot path |
| [src/main3d.cpp](../../src/main3d.cpp) | 6 new CLI flags; minimal hand-rolled cameras.json loader (no JSON dep added) |
| [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json) | NEW — 3 starter cameras for cornell-orig-alcove |
| [tools/v20_pre_measurement/run_v20_pre.ps1](../../tools/v20_pre_measurement/run_v20_pre.ps1) | NEW — 240-capture sweep driver |

## 4. CLI surface

```
--measurement-cameras-file=tools/v20_pre_measurement/cameras.json   # populate slots
--measurement-camera={-1|0|1|2}                                     # pin to slot or interactive
--cascade-exclude={-1|0|1|2|3}                                      # leave-one-out attribution
--noise-seed-offset={0..N}                                          # per-run PCG offset
--error-decomp-mode={0|1|2|3}                                       # Mode 20 sub-mode
--cascade-config-dump                                               # dump alongside next screenshot
```

Existing flags relevant to the harness: `--load-obj=cornell-orig-alcove`, `--render-mode={16,18,20,21}`, `--screenshot=<tag>`, `--exit-frames=N`, `--use-hybrid={0,1}`.

When `--measurement-camera >= 0`:
- Probe jitter is forced to zero ([demo3d.cpp:946](../../src/demo3d.cpp#L946))
- Camera mouse-look / WASD / wheel are short-circuited in `processInput` ([demo3d.cpp:562-573](../../src/demo3d.cpp#L562-L573))
- Camera position/target are restored from the slot every frame (defence-in-depth against any code path that might nudge them)

## 5. Self-critique (V/G/F)

**V — Verified**
- Build clean (Release, MSVC). No new warnings, no errors. Existing C4819 codepage warnings preserved.
- Top-cascade edge case (`cascadeExclude == cascadeCount-1`) handled: `hasUpper5d` correctly evaluates false when `upperIdx5d == cascadeIndex+2 >= cascadeCount`. Verified by inspection of [demo3d.cpp:2487](../../src/demo3d.cpp#L2487).
- `cascadeExclude=0` consume-side override only fires when `cascadeCount > 1`, avoiding a single-cascade-config crash.
- Cascade-ready reset semantics in `setCascadeExclude` and `setNoiseSeedOffset` force EMA seed + hybrid reset so the next capture is fresh — not contaminated by the previous camera's history.

**G — Gaps**
- **C1: leave-one-out semantics drift from plan intent.** The plan envisioned attributing per-cascade error contribution by "removing the cascade and observing the residual." My bake-time skip+rewire does that — but the cascade `excluded-1` now also has an extended tMax and binds a different upper, so its OWN bake result changes. The measured "delta from baseline" is therefore the sum of (cascade N's missing contribution) + (cascade N-1's bake-result shift from rewiring). Analysis must account for this; pure subtraction overstates cascade N's contribution.
- **C2: Mode 21 is a proxy.** True per-pixel cascade dominance would require a separate pass that, per pixel, records which cascade's atlas the consume path actually read. The frag shader currently reads only cascade `selC` (uniform-decided, not per-pixel cascade switching). I substituted camera-hit distance binned against cascade tMax brackets — defensible (matches the bake-time tMin/tMax allocation logic) but not the same metric. Useful for "which cascade should be carrying this pixel" but not "which cascade IS carrying this pixel post-merge."
- **C3: cameras.json coordinates are starters.** Cornell-orig-alcove bounds are not pinned in source; the three positions are best-guesses calibrated against the existing `--cam-preset=alcove` anchor. First capture run must include a sanity-pass to confirm framing, then refine in-place.
- **C4: noise floor estimate stride.** Plan calls for 64 frames × 4 runs. The harness uses `--exit-frames=$WarmupFrames` (default 32) — short of 64. I left it parameterizable; the docstring says "warmup," but for a proper noise-floor run the user must increase it to 64 (or change the script to accumulate post-warmup samples, which requires a frame-counter mode the engine does not expose).
- **C5: no input validation between flags.** `--measurement-camera=5` with only 3 slots loaded is clamped silently by the setter; user gets the print but no exit-with-error. Same for `--cascade-exclude=99` (clamps to `cascadeCount-1`). Acceptable for a manual harness; fragile if scripts misorder flags.

**F — Fixes applied this pass**
- Added defence-in-depth camera restore inside `processInput` even when input is suppressed (covers the case where any ImGui edit / R-key handler in a debug hotkey block nudges the camera).
- Documented architectural divergence explicitly in the impl doc rather than burying it as a TODO.
- Made cameras.json a starter file with `_note` and `_purpose` fields so the next captures session reads the intent without re-reading the plan.
- Cascade-config dump records `useHybrid` so a hybrid-on baseline capture is self-identifying — addresses critic-06 H3 "hybrid baseline ambiguity" tangentially (the capture itself can't be confused for an MBRC-only run).

**F — Fixes deferred to follow-up**
- Surface C1 in the analyze.py docstring + the verdict-rule logic ("delta-from-baseline is an upper bound on cascade N's contribution; treat as ordinal not cardinal").
- Promote Mode 21 from proxy to ground-truth by adding a per-pixel `firstHitCascade` write-out in the frag shader (requires a second render target; defer to v2.0a or skip if the proxy proves adequate).
- Tighten `--cascade-exclude` to error-exit on out-of-range when running under the harness.

## 6. Deferred (follow-up sessions)

- **PT-cache EXR dump**: Mode 16 (PT reference) needs to serialize its accumulator to OpenEXR so analyze.py can read float radiance. Current path only emits PNG. Requires linking against a small EXR writer (tinyexr is header-only; least-friction add).
- **`tools/v20_pre_measurement/analyze.py`**:
  - Load (camera, exclude, seed) screenshot grids
  - Compute luminance-RMSE per (camera, exclude) against PT reference
  - Average across seeds; report stderr
  - Leave-one-out attribution table: per-cascade delta-RMSE
  - Spatial-vs-angular decomposition: combine Mode 20 sub-modes 1/2 (direct vs indirect)
- **Verdict rule** (plan §6):
  - `MBRC_RMSE < best_hybrid_arm_RMSE - 2 × stderr_across_arms` → "retire hybrid" verdict
  - Per-cascade attribution > X% → "this is the cascade to focus v2.0a on"

When these land, the harness emits a `verdict.json` consumable by the v2.0 sign-off gate.

## 7. Capture protocol

Once the above scaffolding works:

```powershell
# 1. Build
./build.ps1

# 2. Sanity-check cameras (1 capture per slot)
./build/Release/RadianceCascades3D.exe --load-obj=cornell-orig-alcove `
    --measurement-cameras-file=tools/v20_pre_measurement/cameras.json `
    --measurement-camera=0 --render-mode=18 --exit-frames=32 --screenshot=cam0_sanity
# Inspect captures/, refine cameras.json if framing wrong

# 3. Full sweep (240 captures, ~50 min)
pwsh tools/v20_pre_measurement/run_v20_pre.ps1

# 4. (deferred) analyze
python tools/v20_pre_measurement/analyze.py
```

## 8. Open questions for next session

1. Should C1 (leave-one-out semantics drift) downgrade the planned leave-one-out attribution to a coarser "per-cascade-tier-removed" metric, or do we accept the upper-bound interpretation and document it in the report?
2. Mode 21 as proxy vs. promotion to per-pixel `firstHitCascade` — wait for first capture to see if the proxy is informative enough?
3. cameras.json starter coordinates — does the user want to drive a sanity-pass interactively (open the GUI, dump camera state via `--screenshot` with stats) before the sweep, or pin coordinates now and refine after first sweep?
