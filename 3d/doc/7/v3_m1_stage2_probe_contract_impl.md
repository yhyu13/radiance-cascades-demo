# M1 Stage 2 Implementation - Probe Contract Audit

**Date:** 2026-05-27.  
**Plan:** `doc/7/v3_m1_stage2_probe_contract_plan.md`.  
**Result artifact:** `tools/v3_m1_probe_contract/probe_contract_results.json`.  
**Verdict:** the current failure is not explained by a global upper-cascade/probe-chain energy explosion. Next phase should prioritize final sampling or local screen/reference contract audit.

## What Changed

1. Added headless probe-stats export.
   - New CLI: `--probe-stats-json=PATH`.
   - Public method: `Demo3D::dumpProbeStatsJson(path)`.
   - Export happens on the clean screenshot exit frame, beside mode-17 EXR capture.
   - It writes already-existing readback fields:
     - per-cascade resolution and directional resolution;
     - `anyPct`, `surfPct`, `skyPct`;
     - `meanLum`, `maxLum`, `variance`;
     - timing and cascade config metadata.

2. Added repeatable tooling.
   - `tools/v3_m1_probe_contract/capture_contract.ps1`
   - `tools/v3_m1_probe_contract/analyze_contract.py`

3. Ran Cornell and Sponza contract captures at N=2048.
   - Cornell: `tools/v3_m1_probe_contract/captures_cornell/`
   - Sponza: `tools/v3_m1_probe_contract/captures_sponza/`
   - Each folder has 5 files:
     - PNG
     - cascade GI EXR
     - PT full EXR
     - PT direct EXR
     - probe-stats JSON

## Verification

Build:

```powershell
cmake --build build --config Release --target RadianceCascades3D
```

Result: passed. Existing MSVC warnings remain (`C4819`, `C4244`, `C4996`, `C4018`, `C4310`).

Captures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_probe_contract/capture_contract.ps1 -Scene cornell -N 2048
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_probe_contract/capture_contract.ps1 -Scene sponza -N 2048
python tools/v3_m1_probe_contract/analyze_contract.py
```

Result:

- Cornell files: 5/5.
- Sponza files: 5/5.
- `probe_contract_results.json`: parseable.

## Results

Screen-space metrics:

| Scene | ratio_self | abs_p95 | dim_pct | bright_pct | valid |
|---|---:|---:|---:|---:|---:|
| Cornell | 0.4845 | 0.8892 | 86.70 | 3.77 | 37866 |
| Sponza | 4.7148 | 4.5279 | 0.00 | 100.00 | 693 |

Probe mean-luminance chain:

| Scene | C0 | C1 | C2 | C3 |
|---|---:|---:|---:|---:|
| Cornell | 0.121084 | 0.116963 | 0.095819 | 0.045453 |
| Sponza | 0.156862 | 0.151476 | 0.125903 | 0.047212 |

Probe coverage:

| Scene | C0 any/surf/sky | C1 any/surf/sky | C2 any/surf/sky | C3 any/surf/sky |
|---|---:|---:|---:|---:|
| Cornell | 100.00 / 99.84 / 0.00 | 100.00 / 99.76 / 0.00 | 100.00 / 100.00 / 0.00 | 100.00 / 100.00 / 0.00 |
| Sponza | 100.00 / 99.11 / 0.00 | 100.00 / 99.66 / 0.00 | 100.00 / 100.00 / 0.00 | 100.00 / 100.00 / 0.00 |

Cross-scene comparison:

| Metric | Sponza / Cornell |
|---|---:|
| C0 probe mean | 1.2955 |
| screen cascade mean | 1.6095 |
| screen `ratio_self` | 9.7317 |

## Interpretation

The global cascade chain does not look like the source of the Sponza over-bright failure:

- Sponza C0 mean is only 1.30x Cornell.
- Sponza screen cascade mean is only 1.61x Cornell.
- But Sponza screen cascade/PT ratio is 9.73x Cornell.
- Both scenes show near-total surface coverage and zero sky coverage in probe stats.

That means the large Sponza failure is more likely local/screen-contract related than a simple "upper cascades too bright everywhere" issue.

The next candidate should not be another broad upper-merge brightness scalar. It should inspect:

1. which world/probe positions feed the Sponza valid PT pixels;
2. whether final GI sampling reads the intended probe/cascade/atlas region;
3. whether the PT indirect reference mask is dominated by local low-indirect regions where cascade interpolation is expected to overshoot;
4. whether C0 final sampling needs localized normal/direction-aware integration rather than isotropic probe-grid reads.

## Self-Critique

1. **Averages are not local proof.**
   - Per-cascade means can hide a small set of bad probes.
   - Improvement made: analyzer now includes cross-scene comparison and avoids claiming root cause.

2. **The Sponza mask is still narrow.**
   - Only 693 valid pixels are used by the screen metric.
   - This remains enough for a veto/stress signal, but not enough for tuning.

3. **Probe stats use existing readback cadence.**
   - The capture uses N=2048, so the jitter readback throttle had time to refresh.
   - Short captures should not be used for this contract audit without checking the stats are fresh.

4. **The export is diagnostic-only.**
   - It does not change shader output or runtime defaults.
   - The only rendering-adjacent change is the headless JSON dump call on the exit screenshot frame.

## Next Phase

Run a localized final-sampling audit:

- add or reuse world-position/normal/probe-index capture for mode-17 valid pixels;
- bin Sponza error pixels by C0 probe cell and selected cascade;
- compare cascade GI vs PT indirect per spatial bin;
- decide whether the next algorithmic candidate should target final sampling, atlas/world mapping, or PT-reference masking.
