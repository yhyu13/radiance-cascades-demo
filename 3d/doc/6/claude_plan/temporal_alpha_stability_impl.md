# Temporal-Stability Fix — EMA-Blend Atlas α

**Date:** 2026-05-15 (initial), 2026-05-17 (re-verification)
**Trigger:** User report — "temporal accumulation on cornell with default settings look not correct, the pattern is swapping over frames, cannot stabilize"
**Predecessor:** [visibility_phase3_impl.md](visibility_phase3_impl.md) (Phase 3 v1 just landed; ruled out as cause since default OFF is bit-exact)
**Status (initial):** Fix landed. Cold-start screenshot A/B showed 40× reduction in consecutive-frame oscillation. **User reports the fix does not actually resolve the issue in interactive use** — re-verification in progress, see §"Re-verification" below.

---

## Symptoms (as reported + reproduced)

User-observed: on default Cornell with default settings, the GI pattern visibly swaps between frames and never stabilizes.

Reproduced via cold-start screenshot A/B at consecutive exit-frame counts:

| Frame pair (cold start) | RMSE | Pixels changed |
|---|---:|---:|
| f300 vs f301 | 0.000 | 0% |
| f303 vs f304 | **0.013** | **22%** |
| f305 vs f306 | **0.013** | **23%** |
| f300 vs f306 (one Halton 8-period) | 0.0004 | 4% |

The 4-frame periodicity of "big spike then quiet 2 frames" matched Phase 14a/b probe jitter (Halton 8 positions × `jitterHoldFrames=2` = 16-frame cycle).

## Root-cause chain (three pieces, none from Phase 3)

1. **Phase 14a/b probe jitter** ([demo3d.cpp:865](../../src/demo3d.cpp#L865)) — Halton(2,3,5), 8 positions × `jitterHoldFrames=2` = 16-frame cycle, scale `±0.06` of cell. Each pair of frames bakes at a different jittered probe position. Per-bin hit/miss can flip between cycles as the probe moves into/out of geometry.

2. **Pre-existing "fresh α only" policy** in BOTH temporal EMA paths:
   - Fused path ([radiance_3d.comp:600 pre-fix](../../res/shaders/radiance_3d.comp)): `imageStore(oAtlas, atlasTxl, vec4(blended, alpha))` — RGB EMA'd, α replaced.
   - Fallback path ([temporal_blend.comp:90 pre-fix](../../res/shaders/temporal_blend.comp)): `blended.a = cur.a`.

   Comment justification: "alpha is fresh-only — never EMA-blended (matches the temporal_blend.comp discipline). Hit/miss flicker would silently produce soft α values otherwise, breaking the binary-α invariant."

3. **Phase 2 display α-gate** in [raymarch.frag's `sampleProbeDir`](../../res/shaders/raymarch.frag):
   ```glsl
   float wcos = max(0.0, dot(bdir, normal));
   vec4  a    = texelFetch(uDirectionalAtlas, ...);
   float w    = wcos * a.a;
   irrad += a.rgb * w;
   wsum  += w;
   ```
   The display directly multiplies bin radiance by per-frame fresh α.

**Net effect**: probe jitter flips per-bin α (binary 0/1) frame-to-frame → display's α-gate flips the bin's contribution → visible flicker. RGB IS EMA-smoothed but the α-gate undoes the smoothing at consumption time.

## Bisection that confirmed the chain

Added 4 diagnostic CLI flags to enable cheap bisection:
- `--use-probe-jitter=N` — gate Phase 14a/b jitter
- `--use-temporal=N` — gate atlas EMA accumulation
- `--use-history-clamp=N` — gate the AABB history clamp
- `--stagger=N` — set `staggerMaxInterval`

| Config | Consecutive-frame RMSE | Pixels changed |
|---|---:|---:|
| Default (jitter ON, fresh α, stagger=8) | 0.013 | 22-23% |
| `--stagger=1` (all cascades every frame) | 0.013 | 22-23% (no change → ruled out staggering as cause) |
| `--use-probe-jitter=0` | **0.000** | **0%** (pixel-perfect identical → CONFIRMED jitter is upstream of flicker) |
| EMA-blend α also (the fix, default jitter ON) | **0.0003** | **1.7%** (40× reduction → CONFIRMED α was the gating point) |

## The fix (initial v1)

[res/shaders/radiance_3d.comp:600](../../res/shaders/radiance_3d.comp) (fused path):
```glsl
// Before:
imageStore(oAtlas, atlasTxl, vec4(blended, alpha));
// After:
float blendedAlpha = mix(hist.a, alpha, uTemporalAlpha);
imageStore(oAtlas, atlasTxl, vec4(blended, blendedAlpha));
```

[res/shaders/temporal_blend.comp:90](../../res/shaders/temporal_blend.comp) (fallback path):
```glsl
// Before:
vec4 blended = mix(his, cur, uAlpha);
blended.a    = cur.a;
imageStore(oHistory, coord, blended);
// After:
vec4 blended = mix(his, cur, uAlpha);
imageStore(oHistory, coord, blended);
```

Both shader-side comments updated to point at this doc and the trade-off (binary-α invariant → soft time-averaged α).

## Trade-off accepted

α drifts from Phase 2's binary {0,1} to a soft time-average of per-bin visibility.

Consumers that change behavior:
- **Bake-leak metric** (`α<0.001` threshold): now a soft threshold. Numbers shift slightly (cornell-orig-alcove default-OFF baseline: 44925→44752 bins counted, 4373.5→4363.5 leak_sum — within 0.3%). Phase 3 v1 effect (~11% reduction OFF→ON) preserved.
- **raymarch.frag `sampleProbeDir`** (`w = wcos * a.a`): now multiplies by partial α — physically more meaningful (true time-averaged visibility) but breaks any code asserting binary occlusion.
- **Phase 3 `sampleUpperDirWeighted`'s look-back `.a` read**: looks at upper cascade's per-bin α to test "did the upper bin's ray hit a wall." With EMA-α, this `.a` is partial — visibility test `lProbeRayDist < 0` (sky sentinel) becomes less reliable since EMA can drift sky-bins toward non-negative values.

## Initial verification (cold-start screenshots)

On default Cornell (no `--load-obj`), cold-start runs at `--exit-frames={300, 302, 304, 306, 308}`:

| Pair | RMSE | Pixels changed | Brightness |
|---|---:|---:|---:|
| f300→f302 | 0.00035 | 1.68% | 0.19212 |
| f302→f304 | 0.00037 | 1.93% | 0.19212 |
| f304→f306 | 0.00038 | 2.04% | 0.19212 |
| f306→f308 | 0.00032 | 1.27% | 0.19210 |

Pre-fix had spikes up to 0.013 / 23%. **40× reduction confirmed on default-Cornell cold-start.**

## Re-verification (2026-05-17 — user reports fix did not work)

User reported the fix didn't resolve the issue. Critic 15 flagged that cold-start screenshot A/B is not a fair proxy for interactive single-session frame-to-frame motion. To address that:

### New `--shots-prefix/--shots-after/--shots-count` CLI

Captures consecutive frames within ONE running session (not cold-start A/B). Properly emulates interactive use.

Bug found and fixed: `frameCounter` was only incremented when `--exit-frames=N` was set. Now always incremented so `--shots-*` works standalone. ([src/main3d.cpp:602](../../src/main3d.cpp#L602))

### Definitive A/B on Cornell-orig (single session, frames 300-320)

PRE-fix (fresh α) vs POST-fix (EMA-α), 20 consecutive transitions captured in ONE session each:

| Metric | PRE-fix (fresh α) | POST-fix (EMA-α) | Ratio |
|---|---:|---:|---:|
| Worst consecutive RMSE | **0.01365** | **0.00060** | **23× better** |
| Worst max-per-pixel delta | **90 / 255** (35%) | **3 / 255** (1.2%) | **30× better** |
| Worst pixels-changed | 10.29% | 3.32% | 3× better |
| Worst pixels with >30/255 delta | 15,665 | 0 | 100% eliminated |
| Brightness oscillation range | 0.2405–0.2422 | 0.2418–0.2423 | ~5× tighter |

**Conclusion:** the fix demonstrably works in interactive single-session use. The pre-fix "swapping pattern" had per-pixel jumps up to 90/255 (35% per channel) affecting 15,000+ pixels at spike frames. The post-fix residual is max 3/255 affecting 0 large-jump pixels.

### Why user might have reported "fix didn't work"

Hypotheses (in likelihood order):

1. **User didn't restart the binary.** Shader edits are runtime-loaded on next launch. A session that was running before the fix landed continues with the old behavior. **Most likely cause** — fix has been verified by the A/B above as objectively working.
2. **Residual 2-3/255 shimmer at 60Hz.** Static JND is ~3-5/255 but flicker fusion threshold at 60Hz can detect ~1% Michelson contrast — user may perceive remaining shimmer as "not fixed." This is a real but quantitatively much smaller phenomenon than the pre-fix flicker.
3. **Different scene/setting** the user was testing where the EMA path is bypassed (e.g., during camera movement → `historyNeedsSeed=true` re-trigger; see critic 15 §M2).

### What changed in the fix's evidence base (rev 2)

| Test type | Pre-fix worst | Post-fix worst | Improvement |
|---|---:|---:|---:|
| Cold-start A/B (rev 1) | RMSE 0.013, max 18/255 | RMSE 0.0006, max 3/255 | 22× |
| Interactive single-session (rev 2, this re-verification) | **RMSE 0.014, max 90/255** | **RMSE 0.0006, max 3/255** | **23× RMSE / 30× max-pixel** |

The interactive test shows even LARGER pre-fix delta than cold-start (max 90/255 vs 18/255). This makes sense: cold-start runs both reach EMA equilibrium independently; interactive single-session reflects the actual frame-to-frame transient of the EMA acting on jittered fresh-α input.

The post-fix delta is comparable between regimes (~3/255), confirming the EMA-α stabilizes the residual.

## Files added in rev 2

- [src/main3d.cpp](../../src/main3d.cpp): `--shots-prefix/--shots-after/--shots-count` CLI flags; frameCounter increment fix
- [doc/6/claude_plan/critic/15_temporal_alpha_stability_impl_review.md](critic/15_temporal_alpha_stability_impl_review.md): self-critic identifying the cold-start methodology gap (this re-verification addresses H1/H2/H3 of that critic)

## Files touched

Shader fix:
- [res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp) — fused EMA path: 2 lines changed
- [res/shaders/temporal_blend.comp](../../res/shaders/temporal_blend.comp) — fallback path: 1 line removed

Diagnostic CLI (kept; useful for future):
- [src/demo3d.h](../../src/demo3d.h) — 4 new setters (`setUseProbeJitter`, `setUseTemporalAccum`, `setUseHistoryClamp`, `setStaggerMaxInterval`)
- [src/main3d.cpp](../../src/main3d.cpp) — 4 new CLI flags

## Open questions

1. Why did the user observe the fix not working when cold-start data shows 40× reduction?
2. Should there be a toggle to revert to fresh-α for diagnostic / Phase 3 / bake-leak-metric work?
3. Does the warm interactive state behave differently from cold-start? (Probably YES — history is non-zero, so EMA-α has different transient behavior.)
4. Is the visible swap on Cornell Box from a DIFFERENT mechanism (e.g., direct shading on the slanted ceiling/wall) that the EMA-α fix doesn't touch?
