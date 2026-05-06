# Reply to Review 02 — Phase 6b RenderDoc AI Plan

**Date:** 2026-04-30
**Reviewer document:** `02_phase6b_renderdoc_ai_plan_review.md`
**Status:** All five findings accepted. Plan doc updated accordingly.

---

## Finding 1 — High: analysis is resource snapshot, not per-stage pipeline traversal

**Accepted.**

The reviewer is correct. The plan headline says "per-stage pipeline analysis" but the
script only does:

1. `controller.GetResources()` — full resource list at the end of the capture
2. name-fragment match
3. `controller.GetTexture(res.resourceId)` — state at the last event that referenced it
4. save one middle Z slice

This is "resource snapshot analysis." It does not walk draw/dispatch events, does not
read resource state immediately after the dispatch that wrote it, and cannot attribute
a texture's contents to a specific pipeline pass.

For the artifacts we care about — "SDF volume looks wrong", "probe atlas has flat tiles"
— snapshot analysis is still useful diagnostic information. The issue is the framing, not
the capability.

**Fix applied in `phase6b_renderdoc_ai_plan.md`:**

- Title changed from "per-stage pipeline analysis" to "resource snapshot analysis"
- Opening goal rewritten: "inspect each key texture resource as it exists at the end of
  the captured frame" (not "extract each pipeline stage texture")
- Added a "Limitations" section explaining that snapshot analysis cannot determine
  *which dispatch wrote* a resource or what the resource looked like mid-frame. A future
  event-walking variant would require traversing `controller.GetDrawcalls()` and calling
  `controller.SetFrameEvent(eventId, True)` before each texture save — that is a
  meaningful engineering step beyond the current scope.

---

## Finding 2 — High: resource name matching will fail without glObjectLabel() calls

**Accepted. This is the most important technical gap.**

The script's `find_last_use(controller, "sdfTexture")` works only if OpenGL resources
carry those names in RenderDoc's resource list. RenderDoc picks up names via:

1. `glObjectLabel(GL_TEXTURE, id, -1, "sdfTexture")` — explicit GL labeling (not present)
2. Debug group / push-pop annotation — not present
3. Driver-assigned names like "Texture 3" — always present, not useful for matching

The current codebase creates textures with `glGenTextures` / `glBindTexture` and no
`glObjectLabel` calls anywhere. So in a real capture, RenderDoc will show "Texture 1",
"Texture 2", etc. The name-fragment matching will return zero matches for every stage.

**Fix applied in `phase6b_renderdoc_ai_plan.md`:**

Added a new `glObjectLabel()` section as a prerequisite before the Python analysis.
For each key texture in `demo3d.cpp`, immediately after the `glBindTexture` call:

```cpp
// Phase 6b: label textures so RenderDoc can identify them by name
glObjectLabel(GL_TEXTURE, sdfTexture,         -1, "sdfTexture");
glObjectLabel(GL_TEXTURE, albedoTexture,      -1, "albedoTexture");
glObjectLabel(GL_TEXTURE, probeAtlasTexture,  -1, "probeAtlasTexture");
glObjectLabel(GL_TEXTURE, probeGridTexture,   -1, "probeGridTexture");
```

`glObjectLabel` is core in GL 4.3 (the project already requires 4.3 for compute). It is
a no-op in release builds if the GL debug context flag is not set — which is fine, since
Phase 6b is an opt-in local tool. The label calls belong in `demo3d.cpp` where each
texture is first allocated (not in the analysis script).

The Python script's `find_last_use()` function is unchanged — but now it will actually
find matches because the resources carry the expected names.

---

## Finding 3 — Medium: `GetTextures()[0]` is not a reliable backbuffer reference

**Accepted.**

`controller.GetTextures()[0].resourceId` is a positional shortcut that relies on
RenderDoc happening to list the swapchain backbuffer first. This is not guaranteed by the
API and will silently pick the wrong texture on captures where the resource ordering
differs.

The correct approach is to find the texture that was bound as the final render target at
the last draw event, or to use the RenderDoc pipeline state API to read the current
output merge descriptor after seeking to the last event.

For the current scope (opt-in local tool, not production automation), a pragmatic fix is
preferable over full pipeline-state walking.

**Fix applied in `phase6b_renderdoc_ai_plan.md`:**

Replaced the positional shortcut with a heuristic that is at least self-documenting
about its assumptions:

```python
# Seek to last event and find the largest RGBA texture that matches screen dimensions
controller.SetFrameEvent(controller.GetDrawcalls()[-1].eventId, True)
textures = controller.GetTextures()
w, h = GetScreenWidth(), GetScreenHeight()  # or read from capture metadata
candidates = [t for t in textures
              if t.width == w and t.height == h
              and t.format.compCount == 4]
backbuffer_id = candidates[0].resourceId if candidates else None
```

If `glObjectLabel(GL_TEXTURE, backbufferTex, -1, "backbuffer")` is added in C++, this
can be simplified to `find_last_use(controller, "backbuffer")` — consistent with the
other stage lookups. Added that label call to the C++ section.

Also added a note: if the heuristic still fails, the user can open the `.rdc` in
RenderDoc GUI, identify the backbuffer resource ID manually, and hard-code it for local
use. This is appropriate for an opt-in local tool.

---

## Finding 4 — Medium: `demo->pendingRdocCapture` and `demo->rdoc` accessed directly in main3d.cpp

**Accepted.**

Having `main3d.cpp` directly read/write `demo->pendingRdocCapture` and call
`demo->rdoc->StartFrameCapture()` creates two problems:

1. It forces both fields to be `public` (or forces a friend declaration).
2. It scatters the RenderDoc protocol (start / end / GetCapture / launchAnalysis) across
   two files with no clear encapsulation boundary.

**Fix applied in `phase6b_renderdoc_ai_plan.md`:**

Added two methods to `Demo3D`:

```cpp
// demo3d.h
void beginRdocFrameIfPending();   // call before BeginDrawing()
void endRdocFrameIfPending();     // call after EndDrawing()
```

```cpp
// demo3d.cpp
void Demo3D::beginRdocFrameIfPending() {
#ifdef _WIN32
    if (pendingRdocCapture && rdoc)
        rdoc->StartFrameCapture(nullptr, nullptr);
#endif
}

void Demo3D::endRdocFrameIfPending() {
#ifdef _WIN32
    if (!pendingRdocCapture || !rdoc) return;
    rdoc->EndFrameCapture(nullptr, nullptr);
    pendingRdocCapture = false;

    uint32_t n = rdoc->GetNumCaptures();
    char capPath[512]; uint32_t pathLen = sizeof(capPath); uint64_t ts;
    if (rdoc->GetCapture(n - 1, capPath, &pathLen, &ts)) {
        std::string path(capPath, pathLen > 0 ? pathLen - 1 : 0);
        std::cout << "[6b] Capture saved: " << path << std::endl;
        launchRdocAnalysis(path);
    }
#endif
}
```

`main3d.cpp` then reduces to two clean method calls with no RenderDoc API exposure:

```cpp
demo->beginRdocFrameIfPending();
BeginDrawing();
// ... render ...
EndDrawing();
demo->endRdocFrameIfPending();
```

`rdoc` and `pendingRdocCapture` can remain `private`. This is the same encapsulation
shape used by `takeScreenshot()` in Phase 6a.

---

## Finding 5 — Medium: same host-tools / API-key concern as Phase 6a

**Accepted.**

Phase 6b has the same environment assumptions as Phase 6a:

- RenderDoc installed at `C:\Program Files\RenderDoc\` (hard-coded path)
- `ANTHROPIC_API_KEY` in the shell environment
- Live internet connection during analysis

These are valid for a local developer workflow but not for CI or reproducible branch
validation.

**Fix applied in `phase6b_renderdoc_ai_plan.md`:**

Added the same "Scope: opt-in local developer tool" section used in the Phase 6a plan.
The section explicitly states:

- This tool requires a specific host installation (RenderDoc) and live API credentials
- It is not part of CI, not part of branch validation, and not run automatically
- If RenderDoc is not installed, the app silently disables the `G` hotkey — no build
  failure, no runtime error
- The analysis Python script is only invoked manually or via the hotkey — never by CMake
  or test runners

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Script is resource snapshot, not per-stage event walking — headline overclaims | High | **Accepted — plan title and goal rewritten as "resource snapshot analysis"; limitations section added** |
| `glObjectLabel()` absent — name matching will return zero results | High | **Accepted — added `glObjectLabel()` calls for 4 key textures + backbuffer as C++ prerequisite** |
| `GetTextures()[0]` is not a reliable backbuffer reference | Medium | **Accepted — replaced with dimension-match heuristic; `glObjectLabel("backbuffer")` added as preferred path** |
| `demo->pendingRdocCapture` / `demo->rdoc` leak RenderDoc state into main3d.cpp | Medium | **Accepted — encapsulated in `beginRdocFrameIfPending()` / `endRdocFrameIfPending()`** |
| Same host-tools/API-key concern as Phase 6a | Medium | **Accepted — added opt-in local developer tool scope section identical to Phase 6a** |
