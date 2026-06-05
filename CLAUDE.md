# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

**From the `3d/` directory (primary 3D implementation):**

```bash
# PowerShell (Windows)
./build.ps1

# Or manually:
mkdir -p build && cd build
cmake ..
cmake --build . --config Release
```

**Important:** The executable must be run from the project root (not the build directory) so shaders and resources load correctly from `res/shaders/` or `3d/res/shaders/`.

**From the parent directory (legacy 2D version):**
```bash
git submodule update --init
./build.sh        # build only
./build.sh -r    # build and run
```

## Project Structure

This repository contains two implementations:
- **`3d/`** — Current 3D Radiance Cascades (CMake project, primary focus)
- Root `src/` — Original 2D Radiance Cascades for soft shadows (separate build system)

## Architecture Overview

This is a **3D Radiance Cascades implementation** — a real-time global illumination algorithm using hierarchical probe grids. The 3D version runs in `3d/` as a separate CMake project.

### Rendering Pipeline (per frame)

```
Pass 1: Voxelize        — upload primitive list to GPU buffer
Pass 2: SDF bake        — sdf_analytic.comp writes sdfTexture + albedoTexture (64³ volume)
Pass 3: Cascade bake    — radiance_3d.comp × 4 levels (C3→C2→C1→C0)
                          + reduction_3d.comp × 4 levels
Pass 4: Raymarch        — raymarch.frag shades every screen pixel
Pass 5: SDF debug       — top-left 400×400 overlay
Pass 6: Radiance debug  — top-right 400×400 overlay
```

Passes 1–2 only run when `sceneDirty`. Pass 3 only when `cascadeReady == false`. Passes 4–6 run every frame.

### Scene Representation

The scene uses **analytic SDF primitives** (boxes, spheres) instead of triangle meshes. The CPU-side `AnalyticSDF` class (in `3d/src/analytic_sdf.h`) builds a Cornell Box scene. A `GPUPrimitive` SSBO (64 bytes per entry) is uploaded to GPU, where `sdf_analytic.comp` evaluates SDF formulas per-voxel.

### Cascade Hierarchy

4 cascade levels (C0–C3) share the same 32³ probe grid at identical world positions. Each level covers a different **ray distance shell**:

| Level | Interval | Purpose |
|-------|----------|---------|
| C0 | [0.02, 0.125] m | near-field |
| C1 | [0.125, 0.5] m | mid-field |
| C2 | [0.5, 2.0] m | walls reachable |
| C3 | [2.0, 8.0] m | far-field |

All 4 levels use identical 32³ probe grid — cascade INDEX determines ray interval, not probe density.

### Render Modes (raymarch.frag)

Modes 0–19 controlled by `uRenderMode` uniform:
- Mode 0 = final image (direct + indirect GI)
- Modes 1–6 = diagnostic views (normals, depth, indirect, direct, steps, GI-only)
- Mode 14 = LeakSuspect heatmap
- Mode 15 = TemporalOscillation heatmap
- Mode 16 = PT reference
- Mode 19 = PT direct-only

### Key Data Structures

- `RadianceCascade3D` struct: holds probe grid texture, atlas texture, resolution, cell size, interval bounds
- `GPUPrimitive` SSBO: 64-byte std430 layout (type, padding, position vec4, scale vec4, color vec4)
- `sceneDirty` / `sdfReady` / `cascadeReady` invalidation chain controls pass execution

## Implementation Phases

The 3D implementation evolved through numbered phases (documented in `3d/doc/`):
- **Phase 0-1**: Framework, analytic SDF, basic cascades
- **Phase 2-3**: Directional atlas, visibility modes, weighted sampling
- **Phase 4-5**: Soft shadows, octahedral direction mapping, co-located cascades
- **Phase 6-7**: Screenshot/RenderDoc integration, hybrid RC + per-pixel correction
- **Phase 8-9**: Temporal accumulation, probe jitter, multi-bounce
- **Phase 10+**: Performance optimizations, quality improvements

## Directory Structure

```
3d/
├── src/
│   ├── demo3d.h/cpp       — main demo class, render loop, all passes
│   ├── main3d.cpp         — entry point, window/OpenGL init
│   ├── analytic_sdf.h/cpp — CPU-side primitive list, Cornell Box builder
│   ├── gl_helpers.cpp/h   — OpenGL helper functions
│   └── obj_loader.h       — OBJ mesh loader
├── res/shaders/
│   ├── sdf_analytic.comp    — SDF + albedo bake
│   ├── radiance_3d.comp     — cascade injection
│   ├── reduction_3d.comp    — isotropic reduction
│   ├── inject_radiance.comp  — direct lighting
│   ├── voxelize.comp         — geometry voxelization
│   ├── raymarch.frag         — final rendering
│   └── *sdf_debug*.frag     — debug overlays
├── doc/                     — phase-by-phase architecture docs
├── CMakeLists.txt           — builds RadianceCascades3D.exe
└── build.ps1                — Windows build script
```

## Dependencies

- **raylib** — window/context (in `../lib/raylib`)
- **rlImGui** — Raylib-ImGui bridge (in `../lib/rlImGui`)
- **ImGui** — UI (in `../lib/imgui`)
- **GLEW** — OpenGL extension loading
- **GLM** — math library

Requires **OpenGL 4.3+** (compute shaders required).

## Debugging

- Render modes 0–19 in raymarch.frag provide various diagnostic views
- Top-left panel: SDF slice visualization
- Top-right panel: radiance probe grid slice
- `bRenderDoc` flag in demo3d.h enables RenderDoc GPU capture (press G to trigger)
- Disable Merge toggle forces cascade recompute without upper cascade contribution

## Development Guidelines

See **AGENTS.md** for detailed development conventions including:
- Container-first policy (never install system packages on host)
- Coding style (camelCase variables, C++23 features)
- Resource loading constraint (must run from project root)
- CONTINUITY.md protocol for session state
- Definition of done checklist

Key conventions from AGENTS.md:
```cpp
// Variable naming: camelCase
int screenWidth;
float brushSize;

// Structs: lowercase with braces on same line
struct WindowData {
  ImGuiWindowFlags flags = 0;
  bool open              = true;
};
```

## Texture Inventory

- `sdfTexture` (GL_R32F, 64³) — signed distance field
- `albedoTexture` (GL_RGBA8, 64³) — nearest primitive color
- `cascades[N].probeGridTexture` (GL_RGBA16F, 32³) — merged radiance per cascade
- `cascades[N].probeAtlasTexture` (RGBA16F) — Phase 5 directional atlas
