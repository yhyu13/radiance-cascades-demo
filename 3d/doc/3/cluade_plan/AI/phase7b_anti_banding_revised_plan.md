# Phase 7b — Anti-Banding Revised Plan

**Date:** 2026-04-30
**Supersedes:** `phase7_banding_fix_plan.md` (original), revised after Review 03
**Baseline configuration confirmed:** non-colocated + directional GI + D-scaling (D4/D8/D16/D16)

---

## Confirmed baseline

The screenshot `frame_17775455579415942.png` was captured with all three quality options
already enabled:

| Option | State | Effect |
|---|---|---|
| `useColocatedCascades` | **false** | C0=32³, C1=16³, C2=8³, C3=4³ probes |
| `useDirectionalGI` | **true** | `sampleDirectionalGI()` — cosine-weighted 8-probe trilinear over C0 atlas |
| `useScaledDirRes` | **true** | D4/D8/D16/D16 via `std::min(16, dirRes << i)` |

These are now the constructor defaults (changed in `src/demo3d.cpp`).

The reviewer's recommended E2 experiment ("toggle useScaledDirRes ON") is already the
baseline — the screenshot is that state. E2 is moot.

---

## Root cause — confirmed hypothesis

With this configuration, the cascade blend crosses a **4× angular resolution jump**
at the C0→C1 boundary:

- C0 probes: D4 = 16 bins (~36° per bin)
- C1 probes: D8 = 64 bins (~18° per bin)

The bake shader blends between a D4-quality directional sample (current cascade) and a
D8-quality sample (upper cascade via `sampleUpperDirTrilinear`). The blend weight is
**linear** (`clamp`), which means it has a non-zero first derivative everywhere inside
the blend zone and a hard kink (derivative discontinuity) at both endpoints:

```
blend weight l:
  1.0 ─────────────────╮                 ← kink here: clamp hits 1.0
                         \
                          \  ← linear ramp (constant slope)
                           \
                        0.0 ╰────────── ← kink here: clamp hits 0.0
                tMax-blendWidth    tMax
```

With non-colocated cascades, the spatial resolution also halves at C0→C1 (32³→16³).
**Two quantization steps coincide at the same boundary distance**, amplifying the
rectangular contour visible on the back wall and ceiling.

---

## Experiments (in order)

### Experiment 1 — Smoothstep blend weight
**Type:** Shader edit | **Cost:** Negligible | **Risk:** Zero

Replace the linear `clamp` with `smoothstep` in `res/shaders/radiance_3d.comp`:

```glsl
// Before (~line 349):
float l = 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);

// After:
float t = clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
float l = 1.0 - smoothstep(0.0, 1.0, t);
```

`smoothstep` applies `3t²−2t³`: derivative is **zero at both endpoints**, removing the
kink at `tMax - blendWidth` and `tMax`. The angular-resolution transition from D4→D8
becomes a smooth S-curve instead of a V-shaped kink.

**Expected evidence of success:** Rectangular contour lines on back wall/ceiling soften
into a continuous gradient. Hard steps replaced by a broad smooth transition.

**Falsification:** If contours persist with the same sharpness, the linear ramp is not
the dominant contributor — move to Experiment 2.

---

### Experiment 2 — Widen blendFraction (UI A/B, no code change)
**Type:** UI slider | **Cost:** Zero | **Risk:** Zero

`blendFraction` is already exposed in the UI. Test at 0.5, 0.75, 0.9 interactively:

1. Run app, drag `blendFraction` slider
2. Press `P` at each value to capture + compare screenshots
3. If banding reduces at higher values: set `blendFraction(0.75f)` as the new default
4. If banding does not respond: the blend zone width is not the limiting factor

**Why it helps:** A wider blend zone means the D4→D8 angular transition is spread over
a larger fraction of C0's interval. At blendFraction=0.9, only 10% of C0's range is
"pure D4" before the blend begins.

**Trade-off:** Very wide blend (→1.0) dilutes C0's fine spatial resolution benefit —
the near-field appears slightly softer. 0.75 is a reasonable starting target.

**Change default only if improvement is confirmed** — do not change the default
speculatively.

---

### Experiment 3 — Raise base dirRes to reduce the D4→D8 angular jump
**Type:** Constructor default change | **Cost:** Significant (4× display fetches at C0) | **Risk:** Performance regression

Current D-scaling: `std::min(16, dirRes << i)` with `dirRes=4`
→ C0=D4, C1=D8, C2=D16, C3=D16

The angular resolution jump at C0→C1 is always 4× (D quadruples per cascade level).
The jump itself cannot be eliminated by raising `dirRes` while keeping the same scaling
formula, but raising `dirRes` to 8 makes the absolute bin resolution at C0 finer:

| dirRes | C0 | C1 | C2/C3 | Display fetches/px |
|---|---|---|---|---|
| 4 (current) | D4 (16 bins) | D8 | D16 | 8 × 16 = **128** |
| 8 | D8 (64 bins) | D16 | D16 | 8 × 64 = **512** |

At `dirRes=8`:
- C0 has 4× more directional bins — the cosine-weighted integral is significantly
  smoother at close range
- C1 and C0 both cap at D16 — the jump factor at C0→C1 becomes D8→D16 (still 4×
  in bin count, but both are fine enough that the perceptible difference is smaller)
- 512 atlas fetches/pixel is a **significant performance cost** — measure frametime
  before committing

**Implementation:**
```cpp
// demo3d.cpp constructor
, dirRes(8)   // was 4; requires atlas rebuild (D8/D16/D16/D16 with scaling)
```

**Only proceed after Experiments 1+2 have been evaluated.** If Experiments 1+2 resolve
the banding sufficiently, Experiment 3 is not needed.

---

### Experiment 4 — Blend zone dither (last resort, artifact masking only)
**Type:** Shader edit | **Cost:** Negligible | **Risk:** Stable noise pattern

If Experiments 1–3 leave residual structured banding, a per-probe hash jitter on the
blend boundary breaks coherent contours into noise:

```glsl
// In res/shaders/radiance_3d.comp, after blendWidth computation:
float jitter = (fract(sin(dot(vec3(probePos), vec3(127.1, 311.7, 74.4))) * 43758.5) - 0.5)
               * blendWidth * 0.1;
float t = clamp((hit.a + jitter - (tMax - blendWidth)) / blendWidth, 0.0, 1.0);
float l = 1.0 - smoothstep(0.0, 1.0, t);
```

**Important caveats:**
- This is **artifact masking, not a fix** — the underlying angular resolution mismatch
  remains
- Without temporal accumulation, the hash produces a **stable textured noise** at the
  boundary, not convergent smooth noise
- Only apply if a stable stipple is less objectionable than a coherent contour

---

## Verification protocol

After each experiment: press `P`, compare with `frame_17775455579415942.png`.

| Success criterion | Artifact | Expected change |
|---|---|---|
| Rectangular contour lines gone | Back wall, ceiling | Smooth gradient — no visible iso-luminance steps |
| Probe-grid fine pattern reduced | Back wall | No visible D4-period grid |
| Color bleeding smooth | Floor/walls | Continuous color gradient, no banded steps |
| Quality rating | — | Fair → Good |
| No frametime regression | — | Measure at D8 before committing |

---

## Files changed / to change

| File | Change | Status |
|---|---|---|
| `src/demo3d.cpp` | Default: `useColocatedCascades(false)`, `useScaledDirRes(true)`, `useDirectionalGI(true)` | **Done** |
| `res/shaders/radiance_3d.comp` | Experiment 1: smoothstep blend weight | Pending |
| `src/demo3d.cpp` | Experiment 2: `blendFraction` default (conditional) | Pending — UI A/B first |
| `src/demo3d.cpp` | Experiment 3: `dirRes(8)` (conditional) | Pending — measure cost |
| `res/shaders/radiance_3d.comp` | Experiment 4: blend zone dither (conditional) | Pending — last resort |
