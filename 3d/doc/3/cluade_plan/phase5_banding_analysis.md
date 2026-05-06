# Phase 5 — Banding Analysis and Quality Improvement Brainstorm

**Date:** 2026-04-29
**Branch:** 3d
**Context:** This analysis assumes Phase 5h (shadow ray, default ON) and Phase 5g
(directional GI, default OFF — must be enabled in the Cascade panel) are both active
in mode 0 as an evaluation configuration. As of the bug fix that changed
`useCascadeGI` to `true`, cascade GI is ON by default; `useDirectionalGI` remains
OFF by default. Enable both for the full Phase 5 quality target before interpreting
this document.

The main visual defect under this configuration is banding — spatially discrete
stepping artefacts in both the indirect GI term and the direct shadow edge.

---

## How the Probe Bake Works (and Why It Bands)

Each probe at grid position `probePos` casts `D×D = 16` rays (D=4) in the interval
`[tMin, tMax]`. Each ray calls `raymarchSDF()`, which marches until it hits a surface,
then returns:

```glsl
color = albedo * (N·L * inShadow() + ambient)
```

This is **full direct shading at the probe-visible surface**. The probe's radiance
is therefore the *direct illumination that arrived at the primary hit point from that
probe ray direction*. This is correct radiance cascade semantics: C0 captures what
light is arriving from [0.02, 0.125m] away.

**Three independent sources of banding follow from this architecture:**

### Source 1 — Spatial probe grid resolution

C0 has 32 probes per side → 0.125m spacing. Each probe represents 1/32 of the 4m
volume. The isotropic path gets hardware trilinear for free; Phase 5g's
`sampleDirectionalGI()` uses 8-probe trilinear, matching the same spatial quality.
However, the underlying probe signal already has 0.125m precision — trilinear
interpolation cannot recover sub-cell detail. Any surface feature smaller than a probe
cell will produce visible banding.

**Severity:** High. This is the fundamental resolution limit of the current setup.

### Source 2 — Direct lighting baked into probes (the user-reported issue)

The bake shader computes N·L + binary shadow **at the probe-ray surface hit**.
A probe whose ray hits a surface in shadow produces a dark bin value. Its neighbor
probe may hit the same surface in light — producing a bright bin value. The resulting
probe-to-probe discontinuity is as sharp as the shadow edge itself (no penumbra).
This causes banding that closely follows the shadow boundary shape in the indirect term.

**Key observation:** This banding is distinct from the direct shadow (Phase 5h) —
it is the shadow boundary *as seen by probes*, re-expressed as indirect GI banding.
The direct term shows a single hard line; the indirect term shows the same hard edge
at slightly wrong positions due to probe placement.

**Severity:** High when shadow cuts across probe cells. Low when shadow falls between
walls (Cornell Box walls in full shadow or full light → low banding there).

### Source 3 — Binary shadow in the bake shader

`inShadow()` in `radiance_3d.comp` returns 0 or 1 — no penumbra. The hit point's
contribution is either 0% or 100% of the light. At a probe ray that just grazes a
shadow boundary, a 1mm shift in ray origin produces a 100% change in baked radiance.
This makes probe-level noise large near shadow edges.

**Severity:** Medium. Affects only probes near shadow boundaries.

### Source 4 — Directional resolution D=4

D=4 means 16 bins per probe. Each bin covers ~1/16 of the sphere. `binToDir()` uses
center-sample convention, so two normals that differ by less than half a bin-width
map to the same bin. `sampleProbeDir()` excludes back-half bins and weights by N·L,
which reduces this, but cannot produce angular detail finer than 1/16 sphere.

**Severity:** Medium-low. D=4 is reasonable for C0 (short range, spatial density
matters more). More noticeable in C2/C3 (Phase 5e D-scaling addresses this).

---

## Improvement Strategies

### A — SDF-based smooth shadow in direct term (High ROI, low cost)

Replace the binary `shadowRay()` in `raymarch.frag` with Inigo Quilez's SDF cone
soft shadow:

```glsl
float softShadow(vec3 origin, vec3 ldir, float distLight, float k) {
    float res = 1.0, t = 0.02;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float h = sampleSDF(origin + ldir * t);
        if (h >= 1e9) return res;        // exited volume — unoccluded
        if (h < 0.002) return 0.0;       // hit — fully in shadow
        res = min(res, k * h / t);       // penumbra factor: smaller h/t = wider shadow
        t  += max(h * 0.9, 0.01);
    }
    return res;
}
```

`k` controls penumbra width (lower = wider soft shadow). `k ≈ 8` gives tight
shadow; `k ≈ 2` gives wide soft shadow.

**Cost:** Same 32 SDF-march iterations as the current binary shadow. Zero extra
texture reads.

**Trade-offs and limitations:**
- This is **not physically equivalent** to a point-light binary shadow — it introduces
  a smooth penumbra without changing the light model to an area light. It is an
  approximation for hiding the hard edge, not a physically accurate improvement.
- The visual result depends on the artistic parameter `k`, which must be tuned per
  scene. There is no analytically correct `k`.
- It does not address the RC-side banding described in Sources 2 and 3 — the probe
  signal near shadow edges remains binary and discrete. The indirect GI term will
  still band even after the direct shadow is soft.

**Best described as:** The fastest way to improve the direct shadow's *appearance*,
not a fix for the underlying RC probe banding problem.

**Phase:** Could be implemented as Phase 5h variant or standalone Phase 5i.

---

### B — Soft / stochastic shadow in the bake shader (Medium ROI, higher cost)

Replace `inShadow()` in `radiance_3d.comp` with the same SDF cone soft shadow or
a jittered multi-ray version. This smooths the probe-baked signal at shadow
boundaries, reducing Source 2 banding.

**Option B1 — SDF cone shadow in bake:** Same as Strategy A but applied inside
`raymarchSDF()`. Cost: 32 extra SDF samples per probe ray hit (when a hit occurs).
Total bake cost increase: proportional to hit rate × ray count.

**Option B2 — Multiple jittered shadow rays:** Cast 4-8 shadow rays with small
random offsets in a cone toward the light. Average binary results → smooth 0-1.
Stochastic — use temporal accumulation (Strategy D) to reduce noise.

**Impact on probe banding:** High. Probes near shadow edges would accumulate smooth
0-1 shadow values instead of binary 0/1, and neighboring probes would show similar
smooth values → interpolation works correctly.

---

### C — Increase C0 probe resolution

Change `cascadeC0Res` from 32 to 64. Probe spacing drops from 0.125m to 0.0625m.
All spatial banding halves. The atlas grows from 32³ = 32k probes to 64³ = 262k
probes → 8× more memory and ~8× longer bake time.

**Current VRAM at D=4, cascadeC0Res=32:**
- C0 atlas: 64×64×32 RGBA16F ≈ 1 MB
- C0 at 64: 128×128×64 RGBA16F ≈ 8 MB

**Trade-off:** Pure brute-force fix. No algorithmic improvement, but always works
and is easy to implement (one integer change). Good for quality evaluation before
investing in A/B/D.

---

### D — Temporal probe accumulation

Instead of recomputing the atlas from scratch each bake, blend into the previous
frame's atlas:

```cpp
newAtlas = lerp(previousAtlas, freshBake, alpha)  // alpha ≈ 0.05 to 0.2
```

In the shader, after `imageStore(oAtlas, atlasTxl, ...)`:
```glsl
vec4 prev = imageLoad(oPrevAtlas, atlasTxl);
vec4 blended = mix(prev, newVal, 0.1);
imageStore(oAtlas, atlasTxl, blended);
```

When combined with stochastic sampling (random ray jitter, stochastic shadow), this
converts banding into convergent noise that integrates to the correct result over
many frames. Static scenes converge fully; dynamic scenes need a higher alpha.

**Impact:** Converts probe-level noise into smooth signals over time. Essentially
free quality improvement for static scenes at the cost of one extra 3D texture.

---

### E — Area light (sphere light)

Replace the point light with a sphere of radius r. Shadow rays are cast toward a
random point on the light sphere surface (or the disk facing the hit point). Average
`N` binary shadow rays.

```glsl
float areaShadow(vec3 hitPos, vec3 normal, vec3 lightPos, float lightRadius, int N) {
    float occ = 0.0;
    for (int i = 0; i < N; ++i) {
        vec3 offset = sampleDisk(lightRadius, hash(i, hitPos));  // jittered disk sample
        occ += shadowRay(hitPos, normal, lightPos + offset);
    }
    return occ / float(N);
}
```

**Impact:** Physically correct penumbra whose size scales with light radius and
object distance. More expensive than SDF cone shadow (N × 32 SDF steps vs 32 steps).
N=4 with temporal accumulation is practical.

---

### F — Screen-space GI blur (post-process)

Apply a bilateral blur to the `indirect` value before adding it to `surfaceColor`.
The bilateral kernel preserves edges (uses depth/normal discontinuities as weights)
while smoothing flat regions.

**Cost:** One post-process full-screen pass. Needs a separate GI buffer.
**Downside:** Blurs sharp GI details (contact shadows, color bleeding). Works best
as a complement to Strategy A/B, not a standalone fix.

---

### G — Increase directional bins D per cascade (Phase 5e — implemented)

Phase 5e is already implemented on the live branch. The active scaling is:
- C0=D4 (16 bins — unchanged from fixed-D baseline)
- C1=D8 (64 bins)
- C2=D16 (256 bins)
- C3=D16 (256 bins, capped to avoid excessive VRAM)

The originally-planned C0=D2 was rejected as degenerate: all 4 D2 bin centers land
on the octahedral equatorial fold (z=0 plane), causing severe directional mismatch
and wall color bleed. The minimum safe D for C0 is 4, which is the current baseline.

**Implication for banding:** Phase 5e improves angular fidelity at C1/C2/C3, which
reduces directional banding in the upper cascade merge. C0 banding (Sources 1-3)
is unaffected — it is dominated by spatial resolution and binary shadow, not D count.
Phase 5e is therefore not a C0 banding fix, but it is already shipped.

---

## Priority Order

Two separate goals require separate priority lists: fixing the *direct shadow
appearance* and fixing the *RC-side probe banding*.

### Best immediate visual win (direct shadow appearance)

| Priority | Strategy | Cost | What it fixes |
|---|---|---|---|
| 1 | **A — SDF cone soft shadow in direct term** | Very low (same march count) | Hard edge in the direct term only (approximation, k-tunable) |
| 2 | **E — Area light** | Medium (N shadow rays × 32 steps) | Physically correct penumbra in direct term |

**Note:** Strategy A improves the *look* of the direct shadow quickly but is an
artistic approximation — it does not resolve Sources 2/3 (RC probe banding).

### Most direct fix for RC-side banding (probe signal quality)

| Priority | Strategy | Cost | What it fixes |
|---|---|---|---|
| 1 | **B1 — SDF cone shadow in bake shader** | Low-medium (per-hit in bake) | Binary shadow in probe signal — Sources 2 and 3 |
| 2 | **C — Increase cascadeC0Res to 64** | Medium (8× bake cost, 8× memory) | All spatial banding — Source 1 |
| 3 | **D — Temporal probe accumulation** | Low (one extra 3D texture) | Stochastic noise convergence when combined with B |

Strategies B and C address the root causes identified in the four-source breakdown.
Strategy A does not — it hides the direct term edge while leaving the indirect GI
banding unchanged.

**Recommended sequencing:** Apply Strategy A first for an immediate visual win, then
Strategy B1 to fix the underlying RC probe banding. Strategy C (resolution increase)
is always available as a brute-force complement.

---

## Why Direct Lighting in Probes Is Architecturally Correct

The user observation that "direct lighting is sampled into radiance cascade" is
correct and is not a bug — it is the intended semantics. A radiance cascade probe at
position P captures all light arriving from direction D over interval [tMin, tMax].
For C0, that means: the light visible from P in direction D from 0.02 to 0.125m away.
If a surface in that interval is directly lit by the point light, the probe bin records
that light. The final renderer adds this as "indirect GI" because it represents light
that first hit a nearby surface and then reaches the primary hit point — which is
one-bounce indirect illumination.

The banding is not from incorrect probe content but from the discrete spatial grid and
binary shadow in the bake. Strategies A, B, and D address this without changing the
probe semantics.
