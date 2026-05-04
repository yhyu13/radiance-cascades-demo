# Review: `phase6b_renderdoc_ai_plan.md` (Revisit)

## Verdict

The plan is still ambitious and directionally useful, but it remains substantially ahead of the current codebase. The later Phase 12/14 screenshot, burst, and sequence tooling improved image-based analysis, but they did **not** land the RenderDoc integration this plan depends on. So the original concerns mostly still hold.

## Main findings

### 1. This is still a plan for infrastructure that does not exist in the live branch

The current branch has:

- screenshot analysis
- burst analysis
- sequence analysis

But it still does **not** have:

- `renderdoc_app.h` integration
- `initRenderDoc()`
- `beginRdocFrameIfPending()` / `endRdocFrameIfPending()`
- `launchRdocAnalysis()`
- `G` hotkey capture path
- `tools/analyze_renderdoc.py`

So this document still reads more like a forward design spec than something close to implementation. That is fine, but the distinction should stay explicit.

### 2. The plan is more careful than the earliest version, but it still overstates how much “resource snapshot + labels” can tell you

The revised note correctly adds a limitation section saying this is resource snapshot analysis, not event walking. That is a real improvement.

But it still leans too hard on the idea that labeled resources at end-of-frame will be enough for automated diagnosis of most pipeline faults.

Why that is still weak:

- many bugs are about **when** a resource was wrong, not just what it looks like at frame end
- transient intermediate corruption can be hidden by later passes
- end-of-frame atlas/grid state still does not tell you which dispatch introduced the problem

So the current note is better than the original, but the practical diagnostic reach is still somewhat oversold.

### 3. The plan’s GPU-timing section is useful, but it quietly expands scope beyond the original capture idea

The newer performance-analysis block is a meaningful scope increase:

- resource extraction
- final-frame image diagnosis
- recursive action walk
- GPU timing table
- program-label dependency for pass naming

That is not necessarily bad, but it makes the tool much less like a simple “press G, snapshot a few textures” helper and much more like a mini RenderDoc analysis framework. The document should probably acknowledge that this is now a broader Phase 6b+14c hybrid tool design.

### 4. The GL object labeling requirements are still underplayed

The plan now correctly distinguishes texture labels from program labels, which is good.

But the success of the Python analysis still depends heavily on consistent labeling:

- texture labels for resource lookup
- program labels for meaningful timing rows
- stable names across refactors

Without that discipline, the automation degrades quickly into heuristics and missing-resource reports. The plan treats labeling as a prerequisite, but in practice it is closer to a maintenance burden that must stay synchronized with the renderer.

### 5. The branch now has stronger screenshot/sequence tools, which raises the bar for Phase 6b justification

Since the original review, the repo gained:

- single-image capture
- stats-backed capture
- 3-mode burst capture
- multi-frame sequence capture

That means Phase 6b now needs to justify itself against a stronger existing baseline. The remaining value is mainly:

- pass-level GPU timing
- pipeline-internal texture snapshots
- validation of hypotheses that image-space capture cannot settle

The plan still implies a broader “automated diagnosis” payoff than the current gap really supports.

## Bottom line

This revised plan is better than the early Phase 6b version because it now clearly states the resource-snapshot limitation and adds a thoughtful GPU-timing section.

But the key critique remains:

- it is still a significant unimplemented subsystem
- it still relies heavily on labeling discipline
- and it still risks overpromising what end-of-frame resource snapshots can diagnose automatically
