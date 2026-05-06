# Action Plan — What I Will Do Next

Priority order follows the codex_plan_critic_critic rule:  
**prefer the option that keeps the project debuggable by one person in one evening.**

---

## Immediate (before calling Phase 2 complete)

### A. Add material/albedo color to both shading paths

This is the single highest-value change. Without it neither Phase 1 nor Phase 2 can be called a Cornell box result.

**Implementation plan:**

1. Add a second 3D texture `albedoTexture` (RGBA8, same 64³ resolution as SDF) to `Demo3D`.
2. In `sdf_analytic.comp`: bind `albedoTexture` as a second `image3D` alongside the SDF image. For each voxel inside a primitive, write the primitive's color. For voxels outside all primitives, write `(0.8, 0.8, 0.8)` as default.
3. In `raymarch.frag`: add `uniform sampler3D uAlbedo;`. At the surface hit, sample `albedo = texture(uAlbedo, uvw).rgb` and multiply: `surfaceColor = albedo * (diff * uLightColor + ambient)`.
4. In `radiance_3d.comp`: same — sample `uAlbedo` at the hit point UV and multiply.

**Expected visual result:** Red left wall, green right wall visible. The indirect bounce from those walls will tint nearby surfaces — the first sign of colored GI transport.

### B. Add shadow ray in `radiance_3d.comp` (probe shading only)

For each hit in `raymarchSDF()`, march a secondary ray from hit point toward `uLightPos` with a capped 32-step march. If the secondary ray hits something (SDF < 0.002) before reaching the light, set diffuse to 0.

```glsl
bool inShadow(vec3 hitPos, vec3 lightPos) {
    vec3 toLight = lightPos - hitPos;
    float dist = length(toLight);
    vec3 dir = toLight / dist;
    float t = 0.05;
    for (int i = 0; i < 32 && t < dist; ++i) {
        float d = sampleSDF(hitPos + dir * t);
        if (d < 0.002) return true;
        t += max(d * 0.9, 0.01);
    }
    return false;
}
```

Apply in `raymarchSDF()`:
```glsl
float diff = inShadow(pos, uLightPos) ? 0.0 : max(dot(n, lightDir), 0.0);
```

This does NOT need to be added to `raymarch.frag` primary rays yet — that requires a similar shadow march but is a separate change with visible primary-ray impact. Do probe shading first (lower risk, same quality gain for indirect result).

### C. Clean up status drift (cosmetic but important for trust)

1. `renderTutorialPanel()`: replace `✗ SDF generation (placeholder)` and `✗ Full raymarching (placeholder)` with correct status bullets.
2. `injectDirectLighting()`: delete the ~80 lines of unreachable code after `return;`, replace with a one-line comment explaining why it was disabled.
3. `destroyCascades()` and destructor: remove stale `// TODO: Implement` comments that are now wrong.
4. `PLAN.md` and `phase2_changes.md`: replace "Implemented" language with "implemented, build-verified, visually unconfirmed" until the smoke test is done.
5. Use "single probe-grid GI prototype" instead of "radiance cascade" when describing Phase 2's current level of completeness.

---

## After visual smoke test passes

### D. Shadow ray in `raymarch.frag` primary rays

Same 32-step capped march from primary hit point to `uLightPos`. This adds occlusion to direct shading visible in every pixel, not just indirectly via probes. Highest visual impact per line of code.

### E. Raise `raysPerProbe` from 4 to 8

One-line change in `initCascades()`: `cascades[0].initialize(32, ..., 8)`. Since the cascade dispatches once, the cost is 2× at startup only. If the indirect toggle effect is too subtle, this is the first knob.

---

## What I will NOT do before Phase 2 is visually confirmed

- Phase 3 (second cascade)
- Temporal reprojection
- OBJ mesh loading in the render path
- Multi-light support
- Voxelization or JFA SDF

These all add new interacting systems before the picture is trustworthy. The codex_plan_critic_critic is correct: remove uncertainty from the image path first.

---

## Revised phase labels

| Phase | Label |
|-------|-------|
| 0 | Done |
| 1 | Implemented, build-verified, visually unconfirmed |
| 2 | Implemented, build-verified, visually unconfirmed |
| 2.5 | Material color + probe shadow ray (immediate next work) |
| 3 | Blocked on Phase 2 visual confirmation |
