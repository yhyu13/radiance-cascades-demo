# Cornell Point-Light Constraint — Volumetric Cascade Topology Limitation

**Date:** 2026-05-28  
**Source:** v4 ShaderToy adoption Phase 1B  
**Status:** Documented limitation — NOT a code bug. Fix requires Path B (surface-attached) or hybrid correction.

---

## 1. The Constraint

The volumetric cascade pipeline under-emits Cornell GI by ~2× when the light source is a point light inside enclosed geometry.

| Light type | cascade_gi / pt_gi | Gap |
|------------|-------------------|-----|
| Point light (default, at ceiling center (0,0.8,0)) | **0.49** | 51% under-bright |
| Directional light (from above, (0,-1,0)) | **0.93** | 7% under-bright |
| Hybrid correction (point light) | **0.83** | 17% under-bright |

## 2. Mechanism

The cascade bake works by casting probe rays from grid positions over S², sampling the SDF, and recording the outgoing radiance of whatever surface each ray hits. The upper cascade then reads this as "incoming radiance from the far field" — providing the indirect bounce.

When lighting is **directional**:
- ALL surfaces are uniformly lit
- Every probe ray hits a surface with ~the same outgoing radiance
- The atlas fills uniformly → merge captures correct energy
- Cascade-vs-PT ratio ≈ 0.93

When lighting is a **point light** inside the Cornell box:
- Floor center is brightly lit (close to the ceiling light)
- Floor edges/corners are dimly lit (farther, cosine falloff)
- Probe rays are cast uniformly over S²
- Probability of hitting the bright floor patch ≈ solid angle of patch / 4π
- Most rays hit dimly-lit surfaces → atlas records low radiance
- Merge produces biased-low indirect bounce → cascade-vs-PT ratio ≈ 0.49

```
Point light at (0, 0.8, 0):
  ┌─────────────────┐
  │  ● light (ceiling)
  │                 │   Walls: directly lit → bright
  │  ░░░░░░░░░░░░░░░│   Floor: ██=bright patch, ░░=dim
  │  ░░██████████░░░│
  │  ░░██████████░░░│   Probe rays (uniform S²):
  │  ░░██████████░░░│     → 3% hit bright patch
  │  ░░██████████░░░│     → 97% hit dim or ceiling
  └─────────────────┘
```

## 3. Why This Is NOT Fixable in Volumetric Topology

The v2.x program tried every available fix axis:

| Attempt | Verdict | Reference |
|---------|---------|-----------|
| Increase C0 dirRes 8→16 | DEAD | v2.4 |
| Merge-formula reshape | DEAD | v2.2 |
| Output luminance clamp | DEAD | v2.4.b |
| Per-corner gated trilinear (#3) | DEAD | v3 M1 Stage 1 |
| Geometric cone widening (#6) | DEAD | v3 M1 Stage 1 |
| MB gain tuning | NO FIX | v3 M1 Stage 9 (still 0.49 at every gain) |

The constraint is topological: **volumetric probes sample the radiance field uniformly, but the lit patch is non-uniform.** More probes don't help (the probability of hitting the bright patch is proportional to solid angle, not probe density). Different merge formulas don't help (the atlas already contains biased-low radiance). Cone widening doesn't help (rejected corners contribute MORE dim radiance, not less).

## 4. How ShaderToy/Path B Avoids This

The ShaderToy reference avoids this entirely by placing probes ON surfaces:

```
Surface-attached probes:
  ┌─────────────────┐
  │P●██████████████●P│  Each probe sits ON the surface
  │                  │  Probes on bright floor → capture full energy
  │P ●████████████● P│  Probes on dim floor → capture dim energy  
  │  ░░██████████░░░│  Cascade hierarchy merges correctly
  │P ░░██████████░░░P│  across spatial scales
  └─────────────────┘
```

A surface-attached probe on the brightly-lit floor center captures the FULL outgoing radiance of that surface point into its atlas bins. The cascade merge then propagates that energy to nearby probes correctly. There is no "probability of hitting the bright patch" problem because the probe IS on the bright patch.

## 5. Fix Options

| Option | Effort | Cornell ratio | Status |
|--------|--------|--------------|--------|
| **Keep hybrid ON for Cornell** | 0 (already works) | 0.83 | Recommended |
| **Use directional light** | 1 CLI flag | 0.93 | Works, changes scene lighting |
| **Path B (surface-attached)** | 3-6 sessions | ~1.0 (by construction) | Deferred decision |
| **Volumetric fix** | Unknown, probably impossible | ≤0.93 in theory | Not recommended |

## 6. Decision (LOCKED 2026-05-28)

The hybrid per-pixel correction remains ON for Cornell-class enclosed-geometry point-light scenes. The cascade pipeline is correct for directional/broad-lit scenes (Sponza, Cornell-directional). The two together cover all tested configurations at acceptable quality. Path B is deferred until a new scene requirement makes the constraint a blocking issue.