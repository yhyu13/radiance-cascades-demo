# 03 — Phases 1 to 4: Building the Foundation

**Purpose:** Understand what was working before Phase 5 and what problems remained.

---

## Phase 1–2: Getting the scene and SDF pipeline working

Phases 1 and 2 established the basic render loop:
- Analytic SDF computed by GPU compute shader into a 64³ texture.
- Sphere-march raymarcher in the fragment shader displaying the scene.
- Cornell Box scene with 7 primitives.

No GI yet — just direct lighting with shadows.

---

## Phase 3: First cascade GI

The first real GI system: 4-level cascade grid, each probe fires N rays, stores
the direction-averaged color. The merge runs C3→C2→C1→C0.

**What it produced:** Rough, isotropic GI. Probe colors were averages of all directions
— no per-direction storage. Every ray that missed pulled the same averaged color from
the upper cascade, regardless of which direction the ray was going.

**Key limitation:** The isotropic average blends all directions together. A probe near
the red wall seeing red in one direction and green in another stores the blend (orange).
When another probe queries it in the "green" direction, it still gets orange.

This causes **color bleeding** (wrong colors) and **banding** at cascade boundaries
(sudden shift when a surface moves from one cascade's interval to another).

**Phase 3 numbers:**
- 8 rays per probe, all cascades (Fibonacci sphere directions)
- No per-direction storage
- Binary miss: hit = local color, miss = upper cascade average

---

## Phase 4a: Environment fill toggle

Added `useEnvFill` toggle. When a ray exits the SDF volume without hitting anything,
it can either return black (honest transport) or return a sky color (fill).

Without env fill: probes near the volume boundary see zero far-field contribution.
With env fill: a sky color propagates through the merge chain, providing ambient fill.

Default: OFF (honest transport). The sky fill toggle exists for debugging open scenes.

---

## Phase 4b: Ray count scaling per cascade

**Problem:** All cascades used the same 8 rays per probe. C3 covers [2, 8 m] — a huge
solid angle range with only 8 samples. C0 covers [0, 0.125 m] with the same 8.

**Fix:** Scale rays per cascade: `raysPerProbe_i = baseRays × 2^i`.
Default baseRays=8 gives C0=8, C1=16, C2=32, C3=64. Total 120 rays vs 32 previously.

**Implementation:** The ray direction scheme was still Fibonacci sphere at this point
(Phase 5a replaces it with octahedral bins).

**Result:** More rays on far-field cascades. Better coverage at large distances.
A slider lets you tune `baseRays` from 4 to 8.

---

## Phase 4c: Blend zone at cascade boundaries

**Problem:** Hard binary merge. A surface at t = 0.124 m gets 100% C0 direct color.
A surface at t = 0.126 m causes C0 to miss, pulling 100% from C1's averaged color.
At t = 0.125 m the cascade boundary, there's a hard visible step.

**Fix:** Define a "blend zone" near tMax where a surface hit is lerped with the upper
cascade value. Controlled by `blendFraction` (default 0.5 = blend over the last 50% of
the interval).

Formula:
```glsl
blendWidth = (tMax - tMin) × blendFraction
l = 1 - clamp((hitDistance - (tMax - blendWidth)) / blendWidth, 0, 1)
result = localColor × l + upperColor × (1 - l)
```

At t = tMax: l = 0, full upper cascade. At t = tMax − blendWidth: l = 1, full local.

**Critical guard:** C3 has no upper cascade. The formula must force l = 1 for C3
(no blending toward black). Guard: only blend when `uHasUpperCascade != 0`.

**Result:** In isolation, 4c had no visible effect because `upperColor` was still an
isotropic average (same value for all directions). Blending toward the wrong value
doesn't help. 4c becomes effective once Phase 5 provides a correct directional upper value.

---

## Phase 4 — State before Phase 5

What was working:
- 4-level cascade bake, C3→C0 merge order
- 32³ probes per level, co-located (same world positions)
- Blend zone at cascade transitions (ineffective without directional merge)
- 120 rays total (8+16+32+64), Fibonacci directions
- Reduction pass averaging all rays into one color per probe
- `probeGridTexture` 32³ driving GI in `raymarch.frag`

What was missing:
- **Per-direction storage**: every probe stores one averaged color; direction is discarded
- **Directional merge**: miss pulls same color regardless of ray direction
- **Root cause of banding/bleeding**: the isotropic average in the upper cascade is wrong
  for any specific direction

Phase 5 fixes all of this.

---

## The ShaderToy reference approach (gap analysis)

The ShaderToy 2D reference implementation (which inspired this project) uses:

| Feature | ShaderToy reference | Our Phase 4 state |
|---|---|---|
| Direction storage | Per-bin (2D octahedral) | None (isotropic average) |
| Merge lookup | Directional, bilinear, 4-neighbor spatial | Isotropic, nearest probe |
| Ray scaling | C0=2, C1=4, C2=8, ... (doubles) | 8, 16, 32, 64 (doubles) |
| Blend zone | Continuous by hit.t | ✓ Phase 4c |
| Levels | 6 | 4 |

The biggest gap: **directional storage and directional merge**. This is the Phase 5 work.
