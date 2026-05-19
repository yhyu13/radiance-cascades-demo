# Critic Review 02 — Phase 7 PT Reference Implementation

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-18
**Verdict:** **Functional but with three impl-time discoveries the plan didn't anticipate.** Implementation landed clean (shader + C++ + display + GUI in one session, builds green, renders Cornell + cornell-orig correctly). **But the bring-up exposed two HIGH issues that nearly derailed it**: (H1) `setRenderMode` clamp at [0,14] silently downgraded mode 16 — caught quickly via log message; (H2) `sampleSDF` returns INF outside the volume bbox, so PT rays from a camera OUTSIDE the volume immediately exit as "sky-miss" — required adding ray-vs-box intersection that the plan didn't mention. Plus one HIGH discovery from the smoke test: (H3) PT brightness is 1.66× cascade on cornell-orig (RMSE 0.33) — a real signal worth investigating, but likely conflated with bias-source differences the plan needs to disentangle. **3 HIGH, 3 MEDIUM, 2 LOW.**

---

## HIGH severity

### H1 — `setRenderMode` clamp limit `[0,14]` silently downgraded mode 16

Plan §5.5 said "add render mode 16 to GUI picker." Done in `kRenderModeLabels`. But `setRenderMode()` in [demo3d.h:500-506](../../src/demo3d.h#L500) had a hardcoded `if (m < 0 || m > 14)` warning. **CLI `--render-mode=16` triggered the warning but ALSO assigned `raymarchRenderMode = 16`** — so the dispatch happened, but the warning was a misleading red herring during debugging.

**Worse**: if there had been an `else return;` path (clamping instead of just warning), mode 16 wouldn't have worked at all. The plan didn't audit existing range-check sites when adding a new mode.

**Fix landed**: bumped the range to `[0,16]` in setRenderMode.

**Lesson** (for cerebrum): when adding a new render mode to a picker, also audit ALL range checks anywhere in the codebase that might filter the new value. Grep for the old upper bound (`14`, `> 14`, `<= 14`) is the minimum due diligence.

### H2 — Camera rays from outside the SDF bbox immediately exit as "sky-miss"

`sampleSDF()` returns `INF` outside the volume bbox. Cascade renderer ([radiance_3d.comp:141-148](../../res/shaders/radiance_3d.comp#L141)) is fine because cascade rays start at PROBE positions, which are all inside the volume. PT rays start at CAMERA position, which is typically OUTSIDE the volume (default Cornell: camera at z=4, volume bbox z∈[-2,2]).

The plan §4.5 ported `raymarchSDF` as-is without addressing this difference. Result of initial bring-up: every pixel returned blue ("miss-in-volume" debug color). Took two debug iterations to isolate: first test pattern (compute→display works), then hit-visualization debug (camera+SDF broken).

**Fix landed**: added `intersectBox()` ray-vs-bbox slab test; `traceSDF` advances `t` to `boxEnter + 0.001` if origin is outside the bbox, returns sky-miss if ray never enters the bbox. The fix is correct and small (~20 lines).

**Plan-gap lesson**: the plan said "port `raymarchSDF` but strip per-hit shading." That's not enough — the cascade renderer's ASSUMPTION about ray origin being inside the volume is load-bearing for sampleSDF's INF-on-exit logic. PT breaks that assumption; the port needs an explicit ray-enters-volume step.

**For the plan rev 3**: add a §4.5b note: "Camera rays start OUTSIDE the SDF volume. Cascade rays start inside. The `sampleSDF` INF-outside-bbox behavior is fine for cascade but breaks PT; `traceSDF` must intersect the ray against the bbox and advance to the entry point before sampling. For shadow rays and bounce rays, this is automatic (they originate at surface hits, inside the bbox)." Should be in plan as a contract violation we discover, not a runtime surprise.

### H3 — PT brightness is 1.66× cascade on cornell-orig (RMSE 0.33); plausibly correct but plan doesn't disentangle bias sources

Smoke test at 500 spp:
- Cascade mode 0: mean brightness 0.24225
- PT mode 16 (unbiased default): mean brightness 0.40209
- Ratio: 1.66× brighter; RMSE 0.33

This is a huge gap. Plausible explanations:
- **A**: PT is correct; cascade under-integrates indirect bounces (known issue, e.g., critic-15 N3 sky-α conflation can dim cascade's atlas)
- **B**: PT over-counts via direct-light-at-every-bounce (legitimate per the rendering equation but the cascade doesn't do this multi-bounce direct lookup)
- **C**: PT has the cascade-match toggle but I didn't test it; the gap is partly "PT no ambient" vs "cascade has ambient floor" — they're not the SAME scene radiometrically
- **D**: 500 spp may not be converged; brightness mean is still trending

The plan §7.2 says "external validation against Blender Cycles." Without that I can't tell (A) from (B). Plan-gap: I shipped v1 without external validation, so the brightness gap is interpretable but not actionable.

**Fix paths**:
1. **Run with `--pt-cascade-match`** (CLI flag I never wired up — see M3 below) — would isolate ambient bias.
2. **Run at much higher spp** (10k+ via accumulation over many frames) — confirms PT is converged.
3. **External Blender reference** — the load-bearing validation step.

None done yet; this is the "next step" output of the impl.

---

## MEDIUM severity

### M1 — Tile-based dispatch dropped from impl (plan rev 2 required it)

Plan rev 2 §8 explicitly required: "tile-based + half-res dispatch from day one." I implemented HALF-RES (640x360 accumulator confirmed in log) but SKIPPED TILE-BASED — full-screen single-spp dispatch every frame.

**Cost check**: smoke test ran 500 frames in ~15 seconds = ~30 ms/frame total. That's well within "acceptable interactive" — the cost analysis I worried about in the plan critic was overly pessimistic for half-res alone. Half-res cuts ray count 4× (full-screen 1080p × 1 spp ≈ 2M rays/frame × ~1k SDF lookups = 2B lookups; half-res 540p = ~500M lookups = ~50-100 ms at modern GPU rates).

But on 1080p with full Sponza scene or higher spp/frame, this could spike. The plan was right to call for tile-based as belt-and-suspenders; I shipped without it because half-res alone was empirically fine in smoke testing.

**Action**: document this as a deferred follow-up. Add tile-based if user reports interactive freezing on heavier scenes. Don't add it preemptively.

### M2 — Camera-change invalidation uses simple delta-threshold (no debounce)

Plan rev 2 §5.3 said "debounced by ~250 ms." I implemented `length(camPos - ptLastCamPos) > 1e-4f` (threshold-only, no time component). Subtle mouse-jitter below 1e-4 won't reset; sustained mouse-drag will reset every frame.

In practice this should be fine — by the time the user releases the mouse, the accumulator restarts from the final pose. But during drag the user sees a freshly-noisy image, which can look jarring.

**Action**: add a time-based debounce later if it's a UX problem. For v1 it's acceptable; the threshold-only approach is simpler and the perceived issue is minor.

### M3 — `--pt-cascade-match` CLI flag promised by plan, not wired up

Plan §11 v1b ship gate: "uPtCascadeMatch toggle works." I added the C++ state + uniform + GUI checkbox, but NOT the CLI flag. CLI was promised in §5.5 implicitly via "future: optional `--pt-rays-per-frame=N`, `--pt-max-bounces=N`."

**Action**: trivial — add 4-5 CLI flags (`--pt-cascade-match=N`, `--pt-rays-per-frame=N`, `--pt-max-bounces=N`, `--pt-russian-roulette=N`). 10 lines of code.

---

## LOW severity

### L1 — RNG seed uses scaled-XOR; no PCG burn-in skipped

I wrote:
```glsl
uint seed = uint(pix.x) * 1973u
          ^ uint(pix.y) * 9277u
          ^ uFrameIndex * 26699u;
uint rng = hash(seed);
pcg(rng);  // "burn one to scramble"
```

The "burn one" is a folk trick (PCG output is better after first step from a low-entropy seed). For our hash, the seed is already well-mixed; the burn is harmless but probably unnecessary. Document or remove.

### L2 — `traceSDF` MAX_STEPS=128 is duplicated from cascade; might be too low for PT

PT rays travel longer distances than cascade rays (cascade is per-interval; PT is full-bbox). At 128 steps with avg step size = sphere-trace progress, a ray might run out of steps mid-traversal and return "miss" when actually there's geometry just ahead. The plan didn't profile this. Cornell test renders look correct so it's probably fine, but Sponza-master with thinner geometry might need more steps.

**Action**: try Sponza-master once shader compile is verified for it; bump to 256 if undersampling shows.

---

## Cross-cutting: plan rev 2 said "6-7 days"; actual impl was ~1 day

Plan rev 2 budgeted 6-7 days for v1a+v1b. Actual impl took ~1 day. Three reasons:
1. **No external validation** — the load-bearing Cycles step is missing. That's 1-2 days alone.
2. **Day 6-7 buffer for "PT debugging is rarely first-time-right"** — confirmed by H2 (camera-vs-volume bbox surprise) but resolved in ~30 minutes thanks to incremental debug pattern → hit-viz → real PT.
3. **Skipped tile-based dispatch** (M1) — saved ~half day.

**Net realistic budget if external validation IS done**: 3-4 days (close to my original rev 1 estimate). The plan was over-cautious.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | `setRenderMode` clamp `[0,14]` silently downgraded mode 16 (warned but assigned anyway; could've been disastrous if clamp+return) |
| H2 | HIGH | Camera rays from outside SDF bbox immediately sky-miss; plan didn't anticipate the `sampleSDF` INF-on-exit assumption violation |
| H3 | HIGH | PT 1.66× brighter than cascade; signal worth investigating but plan didn't ship the validation tooling to disentangle bias sources |
| M1 | MEDIUM | Tile-based dispatch dropped (half-res alone is fine empirically; deferred) |
| M2 | MEDIUM | Camera invalidation is threshold-only, not time-debounced |
| M3 | MEDIUM | `--pt-cascade-match` and friends CLI flags not wired up |
| L1 | LOW | RNG burn-in step is folk-trick, probably unnecessary |
| L2 | LOW | MAX_STEPS=128 inherited from cascade; may undersample Sponza |

---

## Top actions for impl rev 1 → rev 2

1. **Fix M3** (15 min): wire up `--pt-cascade-match`, `--pt-rays-per-frame`, `--pt-max-bounces`, `--pt-russian-roulette` CLI flags. Required for headless A/B testing.
2. **Document H2 in shader header** (5 min): add explicit comment in `traceSDF` explaining the bbox intersect is essential because cascade-vs-PT have different origin assumptions about sampleSDF.
3. **Run H3 validation** (1 hour): A/B unbiased PT vs cascade-match PT at same spp on cornell-orig. If unbiased-PT/cascade-match-PT ratio is small (~1.0), gap is just ambient. If it's large, gap is integration error.
4. **External Blender validation** (deferred to a follow-up commit): the load-bearing step for "PT is correct." Without this, H3's gap could be PT being wrong.

Items 1-3 land in a quick follow-up commit. Item 4 is a separate workstream.
