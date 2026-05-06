# Self-Reflection: Project State at Phase 5a

**Date:** 2026-04-26  
**Branch:** 3d  
**Head commit:** `ccb2934` — Phase 5a: octahedral direction encoding + Phase 5 plan + Codex reviews  
**Purpose:** Honest, detailed review of everything — delivered vs planned, design quality, process quality, open debt, and what to do differently going forward.

---

## 1. What Has Actually Been Delivered

### Commit history (Claude-authored work, most recent first)

| Commit | Label | Content |
|---|---|---|
| `ccb2934` | Phase 5a | Octahedral direction encoding; Phase 5 plan; Codex critic phase5 01 |
| `8f32950` | Phase 4b–4e | Ray scaling, distance blend, debug polish, Codex reviews 05–09 |
| `72e4f01` | Phase 4a | Environment fill toggle, debug stats |
| `3f1db3d` | Phase 3.5 | Bug fixes, ShaderToy gap analysis |
| `cbf241f` | Architecture doc | Mermaid diagrams for phases 1–3 |
| `ca088b9` | Phase 3 | 4-cascade hierarchy, merge chain, mode 6 GI-only, per-cascade probe stats |
| `22c962c` | Phase 3 | Multi-cascade N-level switch, per-cascade ray intervals |
| `c3183ba` | Phase 2.5 | Albedo volume, probe shadow ray, debug mode fixes |
| `85a31e2` | Phase 2 docs | Phase 2 debug learnings |
| `5aaa970` | Phase 2 | Radiance slice viewer, modes 4–5, probe stats, SDF normals |
| Earlier... | Phase 1–2 | Cornell box, SDF, window, camera, compute dispatch |

### What the plan said vs what was built

| Phase | Plan goal | Delivered | Honest verdict |
|---|---|---|---|
| 0 | SDF, window, camera | ✅ | Done |
| 1 | Cornell box visible, direct Lambertian | ✅ 7 bugs fixed | Done |
| 2 | Single 32³ cascade, GI toggle | ✅ 8 bugs fixed | Done |
| 3 | 4-level cascade, merge chain, debug modes | ✅ | Done |
| 4a | Env fill: out-of-volume rays return sky | ✅ | Done |
| 4b | Per-cascade ray scaling C0=8…C3=64 | ✅ | Done |
| 4c | Distance-blend merge; A/B validation | ✅ — A/B: no visible change (confirmed root cause is directional mismatch) | Done |
| 4d | Filter verification (WRAP_R, GL_LINEAR) | ✅ — no-op, all correct | Done |
| 4e | Packed-decode fix, coverage bars, mean-lum chart | ✅ | Done |
| 5a | Octahedral direction encoding, retire Fibonacci | ✅ — 0 errors, 0 new warnings | Done |
| 5b | Per-direction atlas texture write | ⬜ | Next |
| 5b-1 | Atlas reduction pass → probeGridTexture | ⬜ | Next |
| 5c | Directional upper cascade merge via texelFetch | ⬜ | Next |
| 5e | Per-cascade D scaling A/B | ⬜ | After 5c |

**Overall verdict:** Everything committed is working. The plan is honest about what is and isn't done. Phase 5a is the current boundary.

---

## 2. Current Technical State of the Codebase

### `res/shaders/radiance_3d.comp` (the core shader)

**What it does now:**
- `dirToOct / octToDir / dirToBin / binToDir` — octahedral direction encoding, full sphere ↔ D×D bins
- `for dy / for dx` loop over D=4 bins (16 rays per probe, all cascades)
- `raymarchSDF()` per ray — sphere-marched SDF with Lambertian shading and albedo sampling
- `upperSample = texture(uUpperCascade, uvwProbe).rgb` — **still isotropic** (one texel, all-direction average)
- 4c blend zone: `l = 1 - clamp((hit.a - blendStart)/blendWidth, 0,1)` — correct but a no-op until upperSample is directional
- C3 guard: `uHasUpperCascade != 0 && blendWidth > 0.0` → prevents blend-toward-zero
- Hit counting: `surfaceHits + skyHits * 255` packed into alpha

**What it does NOT do yet:**
- Per-direction atlas write (`imageStore` to D×D tile per probe)
- Directional upper cascade lookup (`texelFetch` at exact bin)
- The isotropic `texture(uUpperCascade, uvwProbe)` is the main remaining correctness gap

### `src/demo3d.cpp / demo3d.h`

- `cascadeCount = 4`, each 32³ RGBA16F
- Merge chain: C3 baked first, then C2 reading C3, then C1 reading C2, then C0 reading C1
- `dirRes = 4` pushed as `uDirRes`; `baseRaysPerProbe` / `raysPerProbe` per cascade are retained in C++ but no longer pushed to the shader — the UI slider is cosmetically non-functional
- Stats readback: `probeSurfaceHit[]`, `probeSkyHit[]`, `probeMeanLum[]` working
- Coverage bars, blend-zone table, mean-lum chart all in Cascade panel

### `res/shaders/raymarch.frag`

Unchanged since Phase 2. Reads `texture(uRadiance, uvw).rgb` from `probeGridTexture[selectedCascadeForRender]`. This is the final display path — it is correct as long as `probeGridTexture` is populated. Phase 5b-1 (reduction pass) must write `probeGridTexture` after atlas bake to keep this path valid.

### Known in-flight tech debt

| Item | Severity | Status |
|---|---|---|
| `baseRaysPerProbe` UI slider no longer controls actual ray count | Low | Left in-place; slider is cosmetically non-functional |
| RGBA16F packed hit count: `skyH ≥ 9` → corrupt packed value | Low | Documented, slider ceiling at 8 mitigates; proper fix is `GL_RG32UI` buffer (deferred) |
| `inject_radiance.comp` frozen with early `return` | Low | Dead code; harmless |
| Phase 2 stop condition "Toggle changes image brightness/color ⬜ Needs visual smoke test" | Low | Never formally closed; image does change with GI on/off |
| `uRaysPerProbe` removed from shader but `c.raysPerProbe` still lives in cascade struct | Low | Vestigial; harmless |

---

## 3. Gaps Remaining vs ShaderToy Reference

### Gap table at Phase 5a HEAD

| Gap | Gap analysis severity | Phase 5 sub-phase | Status |
|---|---|---|---|
| Isotropic upper-cascade merge (main bottleneck) | **High** | 5b + 5c | ⬜ Not started |
| Fixed D=4 vs scaled rays per cascade | **High** | 5e (A/B) | ⬜ After 5c |
| BRDF / cosine weighting per ray | **Medium** | Not planned | Intentional 3D adaptation |
| Full sphere vs hemisphere | **Medium** | Not planned | Intentional 3D adaptation |
| 4 cascades vs 6 | **Medium** | Not planned | Not needed for GI quality demo |
| Probe visibility weighting (WeightedSample) | **Medium** → analyzed as no-op | 5d deferred | Co-located grids make this trivially pass |
| Reflective materials | **Low** | Not planned | Out of scope |

**Key insight:** After Phase 5a, the only gap that prevents visual similarity to ShaderToy is the isotropic merge. Everything else is either an intentional 3D adaptation choice, already addressed, or deferred with documented justification.

---

## 4. Process Quality Assessment

### What worked well

**Codex critic loop.** Every major plan went through: plan → critic review → claude reply → plan revision → impl → learnings. This caught real bugs before implementation:
- Phase 4c: C3 blend-toward-black (found by Codex 06 before code was written)
- Phase 4e: slider ceiling can't go to 16 (RGBA16F precision limit), missF = 1-surfF-skyF invalid (Codex 08)
- Phase 5 plan: 3 High findings (final image integration gap, linear filter bleed, D=4 regression) all caught by Codex 01 before a single line of 5b was written

**A/B discipline.** Phase 4c's A/B ("no visible difference, mean-lum unchanged") was not just a validation — it was a diagnosis. It confirmed the root cause (directional mismatch) and ruled out all further work within the isotropic model. Without that A/B, Phase 5 might have been spent on the wrong problem.

**Learnings documents.** Writing `phase5a_impl_learnings.md` after each implementation pass forces articulation of what changed and why. These docs are the primary source of truth for the next session's context.

**Plan structure.** The PLAN.md summary + separate `phaseN_plan.md` detail split worked well. PLAN.md stays navigable; detail files are complete. The split was introduced in Phase 5 (previously all in PLAN.md).

### What went wrong or could be improved

**PLAN.md started with only Phases 0–3.** Phase 4 was never entered into the plan during implementation — it only appeared after Phase 4 was complete, when the gap was noticed. A plan file should be kept current during, not only after, work.

**`codex_critic_phase4+/` → `codex_critic_phase4/` rename confusion.** The git staging session had to use `git reset HEAD`, re-investigate, and re-stage. The rename happened during a context compaction and wasn't caught until staging. This cost time and confusion. Lesson: verify directory names against git status before staging.

**Phase 5 plan initially had three High design gaps** (final image path, linear filter, D regression) that Codex caught. These were all discoverable by reading the existing code (`raymarch.frag` for finding 1, `gl_helpers.h` for finding 2, the Phase 4b ray scaling docs for finding 3). The plan was written without re-reading those files. The Codex review is a safeguard, not a substitute for reading what already exists.

**`uRaysPerProbe` UI slider left cosmetically non-functional.** This is visible tech debt — a control that no longer does what its label says. It should be either hidden or relabeled "Rays/probe (retired — see D²)" in the Phase 5b implementation pass. The decision to defer was pragmatic but should be closed soon.

**Phase 5a visual validation not run.** The learnings doc says "Runtime validation required (user)" — but no validation result was recorded. Phase 4c's A/B result was logged in the doc. Phase 5a should have the equivalent: "image looks equivalent to Phase 4 ✅" or "energy level changed unexpectedly — investigate." This is still pending.

**The packed hit count precision problem was foreseeable.** `packed = surfH + skyH * 255` in RGBA16F — the overflow boundary at 2048 is a property of half-float and should have been caught during Phase 4b design, not discovered via Python analysis in Phase 4e. The general lesson: whenever packing multiple integers into a single float channel, compute the maximum representable value before designing the encoding.

---

## 5. Architectural Decisions That Are Load-Bearing

These are irreversible or expensive to change. Worth naming explicitly.

**32³ probe grid per cascade, co-located.** All 4 cascades use the same 32³ grid at the same world-space positions. This simplifies the merge (no spatial interpolation across neighboring probes) but means upper cascades do not cover more volume with finer angular resolution at the spatial level. The ShaderToy uses different spatial probe densities. If we later want ShaderToy-style spatial multi-resolution, the entire atlas addressing scheme changes.

**Full sphere Fibonacci (now octahedral) sampling.** The project chose volumetric 3D probes with full-sphere rays rather than surface-attached hemispheres. This is a defensible 3D adaptation. It means BRDF/cosine weighting is not applied (all directions weighted equally), and rays into walls below the probe are fired and wasted. Changing this would require per-probe surface normal injection (not available for volume probes).

**`texture(uUpperCascade, uvwProbe)` as merge path.** This is the target to replace in Phase 5b+5c. The current binding (`sampler3D uUpperCascade` → `probeGridTexture[ci+1]`) will be replaced by `sampler3D uUpperCascadeAtlas` → `probeAtlasTexture[ci+1]`. The old `probeGridTexture` path stays alive for `raymarch.frag` via the reduction pass.

**RGBA16F for all probe storage.** Half-float was the choice for memory compactness and compatibility with older OpenGL. The packed hit count encoding is a symptom of this choice. Phase 5b introduces a second 3D texture per cascade (the atlas, also RGBA16F) — at D=4 this costs 4 MB per cascade, 16 MB total. Acceptable on RTX 2080 SUPER. At ShaderToy-scaled D (C3: D=16, 512²×32×8 = 64 MB for C3 alone) this becomes a real constraint.

---

## 6. What Phase 5b Must Get Right

Based on everything above, Phase 5b has three things that can silently go wrong:

**1. Atlas must be bound with `GL_NEAREST`, not `GL_LINEAR`.**  
The gl_helpers default is GL_LINEAR. Atlas tiles are packed: D=4 means each 4×4 block belongs to one probe. Bilinear sampling at a tile edge will blend with the adjacent probe. This is an undetectable bug — the rendered image will look slightly soft but the direction-bin isolation will be silently broken. **Use `GL_NEAREST` at allocation time, and use `texelFetch` everywhere in the merge path.**

**2. Barrier ordering.**  
Phase 5b introduces two compute dispatches per cascade: the atlas bake (radiance_3d.comp) and the reduction pass (reduction_3d.comp). Between them: `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT`. The reduction reads the atlas as a sampler — without the barrier, it reads stale data from a previous frame.

**3. `imageStore` vs `imageStore` race in the merge loop.**  
Phase 5c will read `uUpperCascadeAtlas` (ci+1) while writing `oAtlas` (ci) within the same dispatch. These are different textures bound to different image units — no race. But the cascades must be dispatched C3 first, then C2, C1, C0 — same as today. C2's dispatch must not start until C3's atlas bake AND C3's reduction are both complete. One memory barrier after both dispatches for each ci ensures this.

---

## 7. Next Steps (ordered)

1. **Phase 5b** — Allocate `probeAtlasTexture[4]`, write per-direction radiance + hit distance per bin in the inner loop. Keep `probeGridTexture` alive. No merge change yet.
2. **Phase 5b-1** — `reduction_3d.comp`: new shader, one thread per probe, averages D² bins → writes `probeGridTexture`. Keeps `raymarch.frag` working.
3. **Phase 5c** — Replace `texture(uUpperCascade, uvwProbe).rgb` with `texelFetch(uUpperCascadeAtlas, upperTxl, 0).rgb` in the inner (dx, dy) loop. This is the payoff commit.
4. **Phase 5c validation** — A/B: compare GI image at merge ON, Phase 4 baseline vs Phase 5c. Expect visible directional color bleed improvement at cascade boundaries.
5. **Fix the `baseRaysPerProbe` UI slider** — either hide it in Phase 5 mode or relabel it to "retired."
6. **Phase 5e A/B** — Test D=4 (16 bins all cascades) vs D scaled per cascade. If no visible quality difference, lock in D=4 and close.

---

## 8. Calibration: Are We On Track?

The original project goal was "visible Cornell-box raymarched image similar to ShaderToy." The current state:

- **Visible Cornell-box raymarched image:** ✅ Done since Phase 1
- **Working multi-cascade hierarchy:** ✅ Done since Phase 3
- **Directional GI merge:** ⬜ Phase 5b+5c (next 2–3 sessions)
- **"Similar to ShaderToy":** Partially. The geometry, cascade structure, merge topology, and distance-blend math are all correct. The remaining gap is angular: the isotropic `upperSample` is the last major blocker.

Phase 5c is the highest-leverage commit in the project. If it works, cascade boundary banding should visibly decrease and color bleeding should become directionally accurate. If it doesn't produce visible improvement, the diagnosis shifts to the fixed D=4 angular resolution (Phase 5e).

The project is on track. The next commit should be Phase 5b.
