# Reply — Critique 01 on Surface-Attached ShaderToy Refactor Plan

**Date:** 2026-05-28  
**Critique:** `doc/9_shadertoy2/critic/01_critique_surface_attached_refactor_plan.md`  
**Plan updated:** `doc/9_shadertoy2/surface_attached_shadertoy_refactor_plan.md`  
**Disposition:** Mostly accepted. The critique identifies real spec drift from ShaderToy and real contract conflicts with v3 locks.

---

## 0. Summary

The critique is correct on the high-order issues:

1. The plan accidentally mixed two incompatible atlas models:
   - ShaderToy ring-packed cascade bands, and
   - a conventional `surfaceAtlas * dirRes` directional tile atlas.
2. It under-specified persistent self-feedback, which is essential to ShaderToy's multi-bounce closure.
3. It under-specified point-light direct lighting / NEE, which is mandatory because the Cornell point light is not a chart.
4. It softened the v3 retirement criteria too much.
5. It left the measurement harness underspecified.

The plan has been amended to make these corrections binding before Phase 2 implementation.

---

## 1. Item-by-Item Response

| ID | Severity | Verdict | Plan update |
|---|---:|---|---|
| C1 | Critical | **Accepted** | Plan now commits to ShaderToy ring-packed cascade-band layout for first implementation. Separate `dirRes` atlas rejected for first wave. |
| C2 | Critical | **Accepted** | Added persistent ping-pong atlas and Phase 2.5 self-feedback closure. Metrics require warm-up/convergence. |
| C3 | High | **Accepted** | Added mandatory point-light NEE at probe-ray surface hits. No reliance on stochastic emissive-source hits. |
| H1 | High | **Accepted with clarification** | Cornell-only is allowed only because Sponza remains on unchanged volumetric path. Sponza lock remains binding for retirement/default claims. |
| H2 | High | **Accepted** | Ratio targets are now progress gates only. Retirement remains strict `|p95| <= 0.50` on both Cornell and Sponza-class baselines. |
| M1 | Medium | **Accepted** | Interval constant must be derived from chart world extent/resolution before tuning. |
| M2 | Medium | **Accepted** | Normalization text updated to ShaderToy ring solid-angle + cosine placement; no ambiguous duplicate `/π`. |
| M3 | Medium | **Accepted** | WeightedSample is chart-local first. Cross-chart merge is out of scope. |
| M4 | Medium | **Accepted** | Added Measurement Protocol with EXR-backed gate requirement and `baseline_lock_surface_rc.json`. |
| M5 | Medium | **Accepted** | Phase order now includes NEE and persistent feedback before meaningful quality gates. |
| L1 | Low | **Accepted** | Phase 1 requires chart extents to be numerically bound to actual Cornell scene bounds. |
| L2 | Low | **Accepted** | Added stop-loss: 2 implementation attempts + 1 diagnostic round per phase. |
| L3 | Low | **Accepted** | WeightedSample is part of the Phase 5 production candidate; simple bilinear is debug fallback only. |
| L4 | Low | **Accepted** | Diagnostics grouped into shader/debug views vs CPU readback/JSON metrics with timing discipline. |

---

## 2. Critical Corrections

### C1 — Atlas layout drift

**Critique position:** The plan's `surfaceAtlasWidth * dirRes` layout is not ShaderToy. ShaderToy uses a ring-packed cascade band where `probeSize` controls both probe spacing and directional tile size.

**Reply:** Accepted.

The plan now states:

```text
The first surface RC implementation uses the ShaderToy ring-packed cascade-band layout,
not a surfaceAtlasWidth * dirRes outer-product layout.
```

Binding implementation decision:

```text
gRes = chart resolution in atlas texels
probeSize = 2^(cascade + 1)
probePositions = gRes / probeSize
within chart rectangle:
  modUV / probePositions selects the direction coordinate within the probe tile
  mod(modUV, probePositions) selects the surface probe coordinate
```

The separate `dirRes = 8` path is explicitly rejected for the first wave.

Reason:

```text
If we use a conventional per-probe directional tile atlas, the ShaderToy merge math no longer indexes the same semantic texels. That would reproduce the v3 mistake: porting formulas into the wrong topology.
```

---

### C2 — Persistent self-feedback ambiguity

**Critique position:** ShaderToy samples its own previous buffer (`iChannel3`) for recursive bounce closure. The plan described a direct-only/transient bake and would likely stall below the target.

**Reply:** Accepted.

The plan now adds a binding Phase 2.5:

```text
surfaceAtlasPrev -> surface_radiance.comp -> surfaceAtlasNext
swap(prev, next)
```

At every probe-ray surface hit, the shader must:

```text
1. classify the hit chart/UV,
2. sample previous-frame surface atlas at that hit,
3. evaluate current-frame point-light NEE at the hit,
4. combine them into outgoing radiance.
```

Metrics are no longer allowed on frame 1. Any ratio claim must use the warm-up/convergence rule.

This correction is important because Stage 11d showed the Cornell issue is not only bright-patch sampling. It is also multi-bounce under-counting. Surface attachment fixes receiver placement; persistent feedback fixes recursive closure.

---

### C3 — Point-light direct-source treatment

**Critique position:** ShaderToy has a sun direction and explicitly evaluates sunlight at the hit. Cornell's point light is not a surface chart. Without NEE, surface RC would rely on randomly hitting a tiny emitter or previous history.

**Reply:** Accepted.

The plan now mandates point-light NEE at every probe-ray surface hit:

```text
lightDir = normalize(lightPos - hitPos)
visibility = shadowTrace(hitPos + hitNormal*bias, lightPos)
Li_direct = lightColor * max(dot(hitNormal, lightDir), 0) * visibility / distance^2
```

This is the point-light generalization of `CubeA.glsl:172-177`.

This is not optional. It is part of Phase 2.

---

## 3. Contract Corrections

### H1 — Sponza-first-class lock

**Critique position:** The v3 lock says Sponza is first-class. The plan says Cornell-only until Phase 9.

**Reply:** Accepted with scope clarification.

New interpretation:

```text
Surface RC may be Cornell-only during proof-of-topology because Sponza remains on the unchanged volumetric path.
```

This does not violate the Sponza lock because no Sponza behavior is changed by the early surface path. However:

```text
Any hybrid-retirement or production-default claim must include Sponza evidence.
```

Therefore Phase 8 can only make Cornell selection changes. It cannot claim global hybrid retirement.

---

### H2 — Retirement bar drift

**Critique position:** The plan replaced the locked `|p95| <= 0.50` both-scene retirement gate with ratio-only/visual language.

**Reply:** Accepted.

The plan now distinguishes:

```text
ratio >= 0.90  -> progress target
|p95| <= 0.50 -> retirement criterion
```

Binding retirement criterion remains:

```text
|p95| <= 0.50 on both Cornell and Sponza-class baselines
```

The phrase `clearly better than hybrid with acceptable visuals` has been removed as a retirement gate.

---

## 4. Medium Corrections

### M1 — Interval scaling

Accepted.

The plan now says the interval constant must be derived from chart world size and chart resolution before tuning. ShaderToy's baseline is:

```glsl
tInterval = (1.0 / 64.0) * probeSize * 2.0;
```

For Cornell, the first implementation must compute equivalent physical scale from chart bounds/resolution and document the C0 interval numerically.

---

### M2 — Hemisphere normalization

Accepted.

The plan now specifies ShaderToy's placement:

```text
binWeight = (cos(theta - deltaTheta) - cos(theta + deltaTheta)) / binsInRing
weightedRadiance = Li * binWeight * cos(theta)
```

Albedo is applied at the hit branch, matching ShaderToy line 180 behavior. No duplicate `/π` is allowed unless the consumer-side convention is changed and documented.

---

### M3 — WeightedSample 3D ambiguity

Accepted.

The plan now says:

```text
WeightedSample is chart-local first.
Cross-chart WeightedSample is out of scope.
```

This matches ShaderToy's actual behavior: upper cascade lookup stays inside the same chart band. The previous cross-chart 3D wording was too broad and would have required a new derivation.

---

### M4 — Measurement harness

Accepted.

The plan now adds §10.1 Measurement Protocol:

```text
capture target: Cornell point-light cam0
reference: PT full and PT direct EXR at N=2048 unless a new lock says otherwise
surface outputs: surface_gi, final_composite, optional direct-only
metric JSON: doc/9_shadertoy2/baseline_lock_surface_rc.json
```

The exact script can reuse the existing v3/v4 chain, but any metric claim must include the command/path/hash in the resulting JSON. LDR-only verdicts are not valid.

---

### M5 — Multi-bounce closure scheduling

Accepted.

The new order is:

```text
Phase 2: ring-packed surface radiance + point-light NEE
Phase 2.5: persistent self-feedback closure
Phase 3: raymarch consumption and first EXR-backed metric
Phase 4: quality pass against hybrid-level target
Phase 5: chart-local cascade hierarchy + weighted merge
```

This makes the `>= 0.80` progress gate plausible instead of asking direct-only C0 to match hybrid.

---

## 5. Low / Process Corrections

### L1 — Cornell chart dimensions

Accepted.

Phase 1 now requires chart extents to be bound to actual Cornell scene world bounds before lighting work. Guessed `256x256` debug charts are not enough.

### L2 — Stop-loss

Accepted.

Added:

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if still failing: stop feature work, update this plan/reply with the new diagnosis
```

### L3 — WeightedSample ordering

Accepted.

Phase 5 now treats ShaderToy weighted merge as the production candidate. Simple bilinear is allowed only as a debug fallback.

### L4 — Diagnostics effort grouping

Accepted.

The plan now separates:

```text
shader/debug views -> expected in same phase as inspected feature
CPU readbacks/JSON metrics -> required before metric gate claims
```

---

## 6. Remaining Intentional Deferrals

The critique mentions several broader risks. These are acknowledged but not fully solved in the amended plan:

### General mesh / Sponza surface RC

Deferred until Cornell proves the topology. This remains intentional.

Reason:

```text
Surface RC is a new path. Sponza continues on the existing volumetric path. Generalizing surface charts before Cornell proof would delay the core algorithm validation.
```

### Cross-chart radiance transfer

Deferred.

ShaderToy's first-order merge is chart-local. Cross-chart transfer would require a new derivation and is not needed for the first Cornell proof. If Cornell seams become a visible issue, that becomes a later phase.

### Volumetric path sunset

Deferred.

The plan now prevents premature hybrid/volumetric retirement. A sunset plan should be written only after:

```text
surface RC clears Cornell gates
surface RC or unchanged volumetric path clears Sponza gates
measurement lock proves both
```

---

## 7. Updated Pre-Implementation Checklist

Before Phase 2 implementation begins, the following are now required:

- [x] Commit to ShaderToy ring-packed cascade-band layout.
- [x] Reject separate `dirRes` outer-product atlas for first wave.
- [x] Declare persistent ping-pong atlas semantics.
- [x] Schedule recursive self-feedback before quality metrics.
- [x] Add point-light NEE at probe-ray hit points.
- [x] Clarify Sponza lock scope.
- [x] Restore strict both-scene `|p95| <= 0.50` retirement bar.
- [x] Add EXR-backed measurement protocol.
- [x] Add stop-loss policy.

Before Phase 1 implementation begins:

- [ ] Verify actual Cornell world bounds and chart extents.
- [ ] Decide exact packed atlas dimensions for Cornell charts/cascade bands.
- [ ] Decide debug atlas viewer path.

Before Phase 3 metric claims:

- [ ] Add or document capture command.
- [ ] Create `baseline_lock_surface_rc.json`.
- [ ] Pin PT reference EXR paths and SHA256s.

---

## 8. Final Verdict

Critique 01 is accepted as a necessary correction to the plan.

The amended plan is now sharper:

```text
less generic surface-RC design
more direct ShaderToy topology port
ring-packed layout first
persistent self-feedback first
point-light NEE first
EXR-backed gates only
strict retirement bar preserved
```

This should reduce the risk of repeating the v3 error of copying ShaderToy-looking logic into a different data layout.
