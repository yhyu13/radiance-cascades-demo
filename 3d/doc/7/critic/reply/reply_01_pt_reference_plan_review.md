# Reply: PT Reference Plan Critic 01 — `01_pt_reference_plan_review.md`

**Date:** 2026-05-18
**Status:** All 9 findings addressed. **W2 (no ambient floor) reversed my rev 1 + early-rev 2 position**: the right framing is unbiased PT default + opt-in `uPtCascadeMatch` for cascade-target mode — gives us TWO answers (is cascade converging? is the rendering physically correct?) instead of one ambiguous one. **W1 (NEE terminology) corrected**: v1 already does NEE for point lights; v2 adds MIS for area lights, not "NEE." Other findings (W3-W9) are doc/spec tightening. Plan revised in-place; this reply summarizes the changes.

---

## How each finding was addressed

### W1 (HIGH) — NEE terminology + v2 framing

**Accepted, terminology corrected.** I had labeled the shadow-ray-at-every-bounce as "implicit direct" / "Lambertian direct" and then claimed v2 would "add NEE." The critic correctly points out this is wrong: shadow-ray-at-every-bounce IS explicit NEE for zero-area point lights. v2 needs MIS for nonzero-area lights (because then the hemisphere bounce CAN hit the light and produces double-counting without MIS weighting).

**Plan rev 2 fix**:
- TL;DR now says "v1 already does NEE for point lights via shadow-ray-at-every-bounce. v2 adds MIS for nonzero-area lights."
- §11 v2 criteria: "MIS (multiple importance sampling) for nonzero-area lights — replaces shadow-ray NEE."
- §4.7 (the misleading "Lambertian direct" framing) removed.

The point-light zero-area property explains why v1 is unbiased even though it lacks MIS — the probability of a cosine hemisphere ray hitting a point light is exactly 0, so no double-counting can happen. The critic's analysis here was sharper than my original.

### W2 (HIGH) — Ambient floor in PT breaks ground-truth framing

**Accepted, REVERSED my position.** My rev 1 said "add ambient because cascade adds it." My early-rev 2 narrowed this to "add ambient only at primary hit" (per H3 in my self-critic). Critic-01's W2 goes further: **don't add ambient at all by default**. PT should be UNBIASED to serve as a true reference. The cascade's `uAmbientBakeStrength` is a bias (constant radiance regardless of light reach); including it in PT defeats the "ground truth" purpose.

**The two-mode design** the critic proposed gives us both answers:
- **Unbiased PT** (default, `uPtCascadeMatch=0`): NO ambient. True reference. Answers "is the rendering physically correct?"
- **Cascade-match PT** (opt-in, `uPtCascadeMatch=1`): adds ambient at primary hit only. Answers "is cascade converging to its own biased target?"

**Plan rev 2 fixes**:
- TL;DR: "TWO shading modes" — default unbiased, opt-in cascade-match.
- §4.3 (algorithm spec): `if (uPtCascadeMatch != 0 && bounce == 0) accumulation += ambient;` — gated, with the per-bounce-inflation warning kept in the comment.
- §4.3b: added `uPtCascadeMatch` uniform spec.
- §7.3 (quantitative comparison): three-way diff (cascade vs cascade-match-PT vs unbiased-PT) — separates integration error from ambient bias. This is the methodologically correct way to use the two references.
- §10 open question #2: rewritten with the two-mode rationale.

This is the most substantive change from critic-01 — it changes what PT outputs by default, and how downstream comparisons are structured.

### W3 (MEDIUM) — Camera basis derivation unspecified

**Accepted, concrete snippet added.** Plan rev 2 §5.1b now includes:
- The exact CPU-side derivation from `Camera3D` (forward = normalize(target - pos); right = normalize(cross(forward, up)); re-orthogonalize up).
- Column convention specified (right / up / -forward in mat3 columns).
- Y-up caveat with raylib + cornell/sponza convention noted.
- Cross-check method: render world-axis debug pattern in mode 16, compare with mode 1 (normals).

Should prevent the "garbage PT output from inverted basis" failure mode the critic warned about.

### W4 (MEDIUM) — Inconsistent self-intersection offsets

**Accepted, standardized on two values.** Plan rev 2 §4.3c now defines:
```glsl
const float kSurfaceEps    = 0.002;  // surface hit threshold
const float kShadowRayBias = 0.004;  // = 2 × kSurfaceEps (small safety margin)
```
Used uniformly across `traceSDF`, `isDirectlyLit`, and `tracePath` self-intersection offset. The 2× factor explained: "wider than kSurfaceEps because the normal estimate has finite-difference error."

Critic-01 suggested three options (0.002 everywhere, 2× everywhere, or justify the inconsistency). Chose option B (2×) — small margin, explicit rationale.

### W5 (MEDIUM) — Viewport sourcing mismatch risk

**Accepted, explicit specification added.** Plan rev 2 §5.1c specifies:
```cpp
GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
int viewportW = vp[2], viewportH = vp[3];
int ptW = viewportW / 2, ptH = viewportH / 2;
```
The `glGetIntegerv(GL_VIEWPORT)` source matches whatever `raymarch.frag` sees implicitly via `gl_FragCoord`. If a future feature changes viewport handling (e.g., render-target multiplexing), PT and cascade share the same source of truth.

### W6 (MEDIUM) — Accumulator precision at high spp

**Accepted, mix-form already adopted + v2 note added.** Plan rev 2's TL;DR already mentions `mix(prev, frameMean, k/(k+N))` (precision-stable form) — that change predated this critic. New §10 "Precision considerations" section now also documents:
- Float32 is fine for visual reference (per-frame contribution stays ~0.01% of running mean at 10k spp, well above precision floor).
- For sub-0.001 RMSE work, v2 should consider storing running sum + sample count in separate channels and computing mean only at display time.

The critic-01 W6 framing as "not a blocker for v1" matches my treatment.

### W7 (LOW) — Quantify cascade pipeline overhead

**Accepted; corrected my earlier under-estimate.** Plan rev 2 §8 now has explicit cost analysis:
- Cascade bake at 1080p: ~16.5 ms/frame (staggered)
- Raymarch.frag in mode 16 branch (just reads PT accum + tonemap): ~3-5 ms/frame
- GI blur skipped in mode 16: 0 ms
- Total cascade-side overhead: ~20-22 ms/frame
- PT dispatch at v1 settings (half-res, 1 tile/frame, 1 spp): ~30-60 ms/frame
- **Overhead ratio: 25-40% of PT-side cost**, NOT 0.5% as I initially claimed (that was for full-screen PT, not tile-dispatched)

Worth a bypass commit in v2 (~20 ms savings) but acceptable for v1.

### W8 (LOW) — SDF helper duplication

**Accepted, contract documented.** Plan rev 2 §4.3d adds an explicit maintenance contract that will live in `pt_reference.comp`'s header:
```
// SDF helpers (sampleSDF, INF constant, kSurfaceEps stepping) are DUPLICATED from
// radiance_3d.comp. Any change to SDF intersection in radiance_3d.comp MUST be
// mirrored here. The PT reference would silently diverge from cascade output
// otherwise — and "silently diverge" is the worst failure mode for a reference
// renderer (looks right, is wrong).
```
Same shape as critic-16 W1's `sampleProbeDir`/`sampleProbeDirWithLeak` lesson — explicit duplication with a contract beats GLSL `#include` machinery we don't have.

### W9 (LOW) — RNG correlation at low spp

**Accepted, documented as v2 polish.** Plan rev 2 §10 "RNG correlation note" lists:
- Hash-LCG works at the 10k+ spp target (correlation vanishes by ~1k spp)
- For interactive preview at <100 spp, patterns may be visible
- v2 options: import ShaderToy's blue-noise texture (~16 KB asset), PCG-3D, or Owen-scrambled Sobol

Not a v1 blocker.

---

## Cross-cutting concern: v1 split

The critic's "Cross-cutting concern: the plan's v1 is too ambitious" matched what I'd already arrived at independently in my early rev 2 — split into v1a (direct, 2 days) + v1b (indirect, 4-5 days). The two milestones ship independently:
- v1a delivers immediate value as "cascade mode 4 ground truth" with external Cycles validation
- v1b adds bounces + the `uPtCascadeMatch` two-mode design

Total realistic budget: 6-7 days (was 3-4 in rev 1). Day 6-7 explicitly reserved as buffer because PT debugging is rarely first-time-right.

---

## What I added beyond critic-01

The critic didn't flag two things that are still concerns; I kept them in the plan:

1. **Tile-based + half-res dispatch from day 1**: critic-01 implicitly accepted "PT is offline-style" but didn't flag the UI freeze problem. The cost analysis (~1.3-2.7 seconds per frame full-screen at 1080p) is unworkable for interactive work. v1 mitigation = half-res (4× cut) + tile dispatch (16× cut) → ~30-60 ms/frame. Convergence is slower wall-clock (~45 min for 10k spp) but UI stays responsive.

2. **External validation against Blender Cycles** (§7.2): originally my M3 in the self-critic before the user replaced it. The replaced critic-01 doesn't have this specifically as a finding, but the W2 logic ("PT must be a reference, not a cascade copy") implicitly demands external validation. Kept in plan rev 2 as load-bearing: PT validated only against cascade is circular.

---

## Summary

| Critic 01 ID | Severity | Action |
|---|---|---|
| W1 | HIGH | Rewrote NEE terminology; v2 = MIS not NEE |
| W2 | HIGH | **Reversed my position** — default unbiased PT; opt-in cascade-match. Two-mode design enables 3-way comparison |
| W3 | MEDIUM | Concrete camera-basis CPU snippet in §5.1b |
| W4 | MEDIUM | Two-value offset standardization (kSurfaceEps + kShadowRayBias = 2× kSurfaceEps) |
| W5 | MEDIUM | Viewport from `glGetIntegerv(GL_VIEWPORT)` to match `gl_FragCoord` source |
| W6 | MEDIUM | Mix-form accumulator (already done); v2 split-channel note added |
| W7 | LOW | Quantified: cascade overhead ~20-22 ms/frame = 25-40% of v1-PT cost (corrected from "0.5%") |
| W8 | LOW | Duplication contract in shader header |
| W9 | LOW | Blue-noise listed as v2 polish |

**Most impactful finding: W2.** It changed PT's default behavior + the structure of downstream A/B work. Without W2 I'd have built "cascade with more samples" instead of "ground truth that lets us measure the cascade." That's the difference between PT being a sanity check and PT being a quality baseline.

**Critic value: high.** W1 caught a real terminology bug that would have produced confused v2 work. W2 caught a foundational framing error. The 7 MEDIUM/LOW findings are all real spec tightening. Round well-earned.
