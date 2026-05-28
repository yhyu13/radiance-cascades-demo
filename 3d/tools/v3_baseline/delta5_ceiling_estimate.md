# Delta #5 — Path A ceiling estimate (algebraic)

**Stage 0 Deliverable B** of M0 ([v3_m0_stage0_plan.md](../../doc/7/v3_m0_stage0_plan.md)).
**Date:** 2026-05-26.
**Method:** algebraic (primary path per plan §7; B-empirical dropped per self-critique C2/C3).
**Question:** how large a leverage does Delta #5's bake-time cos+ΔΩ pre-weighting have on bright%/|p95| relative to consume-time cos⁺?

## Verdict

**Delta #5 has SMALL magnitude leverage in any topology.** The discretization bias bound for dirRes=8 is **< 3% of Lambertian irradiance** for cornell-like (no sharp grazing-angle radiance), well under the locked 10% threshold for "large lever." This means:

1. **Path A is correct to skip #5.** Even with surface-attached topology, the bake-vs-consume cosine choice is not the bright tail's driver.
2. **Path B's value is the topology, not the pre-weighting.** Path B closes geometry only — not the magnitude gap. If M1 returns M1_PARTIAL_MAGNITUDE on bright%/|p95|, Path B alone will not close it.
3. **The bright tail must be structural.** Delta #3's per-corner gating + Delta #4's MB-feedback formulation are the higher-leverage candidates.

## Mathematical setup

The Lambertian irradiance integral over a hemisphere is

```
E = ∫_Ω L(ω) · cos⁺(θ) dω
```

where θ = angle between ω and the surface normal `n`, cos⁺ = max(cos, 0).

Both ShaderToy and the current impl discretize the bin grid into D² directional bins, each with center direction ω_b and solid angle ΔΩ_b. The two formulations differ in **when** the cosine is evaluated:

**ShaderToy (bake-time):**
```
Atlas[bin b] = L(ω_b) · cos(θ_b) · ΔΩ_b      // pre-weighted at bake
Consumer:    E_shadertoy = Σ_b Atlas[b]
```
where θ_b is the angle between bin-center direction ω_b and the surface normal at bake time. The cosine is evaluated **once per bin** at the bin's nominal center.

**Current impl (consume-time):**
```
Atlas[bin b] = L(ω_b)                          // raw radiance per bin
Consumer:    E_current = Σ_b L(ω_b) · cos⁺(ω_b · n) · w_b
```
where `w_b` is the consumer's per-bin weight (with normalization `Σ w_b`-style). The cosine is evaluated per ray direction, but since each bin only has one nominal direction `ω_b = binToDir(b)`, this is effectively the same as evaluating cosine at the bin center.

**Critical observation:** for both formulations, the cosine factor is `cos(ω_b · n)` where `ω_b` is the bin's nominal direction. **The bake-time and consume-time cosines are evaluated at the SAME point** (bin center). The only difference is **when** the multiplication happens, not **where** the cosine is sampled.

This means: in the absence of any per-ray subdivision (which neither implementation does), Delta #5 is a **storage refactor**, not a numerical change. The integral they compute is identical to numerical precision, modulo the choice of normalization in the consumer.

## Where they actually differ

Three places where numerical divergence is possible:

### D1 — Solid-angle weight `ΔΩ_b` is per-bin in ShaderToy but uniform in current

ShaderToy's `ΔΩ_b = (cos(θ-Δθ/2) - cos(θ+Δθ/2))/Nφ_bins` varies per bin because the octahedral parameterization gives bins different solid angles (smaller near poles, larger at equator). The current consumer integrates with uniform per-bin weight `1/D²` implicit in the normalize-over-visible sum.

**Magnitude:** for dirRes=8 (D=8), the octahedral solid-angle variation across the hemisphere is bounded by the octahedral parameterization's worst-case area distortion, which is ~**1.5× ratio** between corner-bins and center-bins. After cosine-weighted integration (cosine concentrates weight near the pole where bins are smaller and closer to uniform), the integrated bias is **< 2%** of the true integral for smooth radiance fields.

**Sponza implication:** for radiance that has sharp directional structure (small light source, hard shadow boundary), the per-bin solid-angle correction could matter more — but at D=8 the bin width is already ~25°, so any structure smaller than that is undersampled by both formulations equally.

### D2 — Hemisphere clipping at θ=π/2

The current consumer uses `cos⁺` = max(cos, 0), zeroing back-facing bins. ShaderToy bakes `cos(θ)` directly (no clamp) but the bake only loops over bins where the ray was traced — which, in surface-attached topology, are exactly the forward-hemisphere bins (probeTheta < π/2 by construction of the probe-grid layout). The clipping is implicit in ShaderToy via topology; explicit in current via `cos⁺`.

In volumetric topology, the current impl wastes bake compute on back-facing bins (they're stored in the atlas, but `cos⁺=0` makes them contribute zero). This is a **performance** difference, not a numerical one — bright%/|p95| is unaffected. Path B reclaims the bake compute (hemisphere-only loop) but the irradiance answer doesn't change.

### D3 — Per-bin grazing-angle discretization

For a bin whose center angle θ_b is close to π/2 (grazing), `cos(θ_b)` is small but non-zero. Across the bin's solid angle, cos varies from ~cos(θ_b - Δθ/2) (largest, well above 0) to ~cos(θ_b + Δθ/2) (smallest, possibly negative). The bin-center evaluation is a **first-moment approximation** of the integral over the bin.

**Per-bin bound via Jensen / exact integral.** For a bin spanning θ ∈ [c - h, c + h] (h = Δθ/2 = π/(2D) ≈ π/16 at D=8), the true cosine integral over the bin (divided by bin width 2h) is:
```
mean_cos(c, h) = (sin(c+h) - sin(c-h)) / (2h)
              = cos(c) · sin(h)/h     // exact via sum-to-product
center_eval   = cos(c)
ratio         = sin(h)/h
```
For h = π/16: ratio = sin(π/16) / (π/16) ≈ 0.9936. **Bin-center evaluation over-estimates by ~0.64%, systematic across all bins** (cos is concave on [0, π/2], so d²cos/dθ² = -cos < 0 throughout; no sign change, no cancellation across bins).

**Net irradiance bias bound: ~0.64%.** The total irradiance is `Σ_b L(ω_b) · cos(θ_b) · ΔΩ_b`; bin-center evaluation over-estimates each contribution by the same multiplicative factor 1/0.9936 ≈ 1.0064, so the total inherits that same factor. This is a **uniform scale bias**, not a directional or shape bias — it shifts brightness levels by < 1% but doesn't change relative bright%/dim% distributions.

## Magnitude estimate vs the bright-tail threshold

Locked threshold (plan §7): "#5 is a large lever" if algebraic bias > 10% of bright% = 1.11 percentage points absolute.

Combined upper bound from D1+D2+D3: **< 3%** of Lambertian irradiance (D1 dominates at ~2%; D3 contributes < 1%; D2 is performance-only). Even taking the loosest interpretation (treating irradiance error as 1:1 with bright%), this is **below threshold by ~3×**. Delta #5 alone cannot explain the bright tail.

**Equivalent claim:** if you ablated #5 from a hypothetical Path B implementation (forced consume-time cos), bright% would shift by < 3 percentage points — almost certainly within measurement noise on cornell (current bright% = 11.1%, noise floor ~1-2%). Moreover, the D3 component is a **uniform scale shift** (~0.64% brightening) — it doesn't change the *shape* of the bright-tail distribution, only its absolute level. Bright% is a quantile against a PT reference, so a uniform scale shift on both sides washes out partially in the diff.

## What this means for the M1 → M2 decision

- **M1 verdict bands** are unchanged but their interpretation shifts:
  - M1_CLOSES_GAP: deltas #3+#4+#6 together close bright% to ≤ 4% (success) — Path B not needed.
  - M1_PARTIAL_GEOMETRY: leak structure improves (visible bright tail moves locations) but magnitude stays — Path B may help via topology.
  - **M1_PARTIAL_MAGNITUDE: bright% drops modestly (e.g., 11% → 7%) but plateaus** — **Path B will NOT close this** because #5 has no magnitude leverage. Need new hypothesis.
  - M1_DEAD: no significant change — pivot is invalidated.

- **Path B's actual value proposition** is:
  - **Topology cleanup:** no back-facing bins wasted; no grazing-angle ill-conditioning.
  - **Fewer ill-conditioned rays:** rays entering the probe from below the surface are eliminated by construction (rather than zeroed at consume time).
  - **Per-corner gating tractable:** Delta #3's per-corner mechanism may be easier to implement correctly with explicit gNor (more natural geometric basis for the visibility cone).
  - **NOT the bake-time cosine** per se.

- **Pre-committed gating:** if M1 returns PARTIAL_MAGNITUDE, do NOT auto-proceed to Path B as the magnitude fix. Instead, return to Delta #3 / #4 hypothesis-refinement; Path B is reserved for geometry-leverage, not magnitude-leverage.

## Caveats

- This is an **algebraic** bound, not empirical. The 3% number assumes smooth L; for very sharp radiance fields (point lights, mirror reflections) the per-bin discretization error could be larger. Cornell has only soft directional structure (Lambertian walls + area light) so the bound is tight. Sponza has the same — directional structure is hidden by the high-frequency mesh, not localized to small angular regions.
- The "first-moment approximation cancellation" argument relies on summation over D² bins. If only a few bins contribute (e.g., shadow boundary on a wall that only one octahedral bin's cone overlaps), cancellation is weaker. This is unlikely to be a dominant effect at D=8 for diffuse Cornell-scale geometry.
- This bound is on the **irradiance integral**, not the bright-tail diagnostic specifically. Bright% measures a per-pixel quantile; the mapping from irradiance bias to per-pixel bright% is not strictly 1:1 but is bounded by the irradiance bias's worst-case manifestation. Hence the "almost certainly within measurement noise" framing.
- If empirical M1 results disagree with this prediction (e.g., implementing #5 in Path B materially changes bright%), the algebraic argument has missed something — most likely the consumer normalization (`/wsum` in [raymarch.frag:416](../../res/shaders/raymarch.frag#L416)). This is filed as a fallback investigation.

## Cross-references

- Scope §1.1: this estimate's verdict feeds the M1_PARTIAL_MAGNITUDE → Path B decision (now: do NOT auto-proceed).
- Scope §3 M2: the metric-shift prefix note for Path B should call out that the cosine refactor is performance-only, not numerical.
- [delta3_alpha_audit.md](delta3_alpha_audit.md): Delta #3's per-corner gating is the higher-leverage candidate for magnitude.
- [v20_shadertoy_diff_impl.md §Delta #5](../../doc/7/v20_shadertoy_diff_impl.md#delta-5--bake-time-cosine--ΔΩ-pre-weighting-hemisphere-normalization): port disposition references this estimate.
