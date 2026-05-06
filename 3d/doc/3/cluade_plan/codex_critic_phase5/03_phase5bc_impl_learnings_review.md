# Phase 5b/5b-1/5c Implementation Learnings Review

## Verdict

This learnings note is mostly solid. It describes the atlas/reduction/merge split correctly, and it is more honest than the earlier reflection about runtime validation still being pending.

The remaining issues are precision and completeness, not fabrication. A few active UI/debug inconsistencies are still missing from the document, and one validation claim is still too eager.

## Findings

### 1. Medium: the document omits that the new `Bin` debug mode is still not reachable through the advertised `[F] to cycle` path

The validation table treats Mode 5 as a pending runtime check:
- `doc/cluade_plan/phase5bc_impl_learnings.md:175`

But the keyboard cycle path still does:
- `radianceVisualizeMode = (radianceVisualizeMode + 1) % 5`

Evidence:
- `src/demo3d.cpp:313`
- `src/demo3d.cpp:2130-2154`

So the new `Bin` mode exists in the radio-button UI, but not in the shortcut flow the panel advertises. That is not a renderer correctness bug, but it is a real Phase 5 debug-path regression and should be called out in the learnings doc.

### 2. Medium: "Phase 5e can be skipped or locked in" is still stronger than the current UI/debug state supports

The document says:
- "If quality is acceptable at D=4 for all cascades, Phase 5e can be skipped or locked in"

Evidence:
- `doc/cluade_plan/phase5bc_impl_learnings.md:185-186`

That is still too optimistic as a project-state statement. The live code continues to carry stale Phase 4 ray-count semantics in multiple places:
- retired `baseRaysPerProbe` slider still exists
- `RadianceCascade3D::raysPerProbe` is still initialized/scaled as if per-cascade ray counts matter
- some UI labels and plots still report `raysPerProbe`

Evidence:
- `src/demo3d.h:111,662-663`
- `src/demo3d.cpp:371-381`
- `src/demo3d.cpp:1391`
- `src/demo3d.cpp:2084-2087`
- `src/demo3d.cpp:2264`

The rendering path may be good enough to defer 5e, but "locked in" should wait until the instrumentation story matches the actual fixed-`D^2` dispatch model.

### 3. Low: the document is slightly imprecise about where `useDirectionalMerge` invalidation lives

The note says the toggle is tracked "in the update loop."

Evidence:
- `doc/cluade_plan/phase5bc_impl_learnings.md:103-109`

In the current code, that invalidation is part of the `render()`-side `cascadeReady` gating, not `update()`.

Evidence:
- `src/demo3d.cpp:384-388`

This does not change the technical conclusion, but the review standard in this folder has been to keep implementation notes line-accurate.

### 4. Low: the file has visible encoding corruption, which makes technical review harder than it should be

There is repeated mojibake in headings and prose, for example in the title line and several phase separator labels.

Evidence:
- `doc/cluade_plan/phase5bc_impl_learnings.md:1`
- `doc/cluade_plan/phase5bc_impl_learnings.md:12`
- `doc/cluade_plan/phase5bc_impl_learnings.md:44`

The underlying technical content is still understandable, but this should be cleaned up if the file is meant to be a durable project record.

## Where the document is strong

- The atlas/reduction ownership model is explained clearly and matches the code.
- The `GL_NEAREST` requirement is correctly elevated as a correctness issue, not a polish detail.
- The alpha-zeroing section names the real downstream breakage and the actual fixes.
- Runtime validation is explicitly marked pending instead of being blurred into "done."

## Bottom line

This is a useful implementation-learnings document, but it still misses a couple of active Phase 5 cleanup debts around debug access and stale ray-count semantics. I would keep the core content, then revise it to:

1. mention that Mode 5 is not reachable via the current `[F]` cycle path,
2. avoid "locked in" wording until the retired ray-count/UI story is cleaned up,
3. fix the small "update loop" wording drift, and
4. repair the file encoding.
