# 3D Radiance Cascades Implementation

## Overview

This directory contains the 3D extension of the Radiance Cascades algorithm, extending the original 2D screen-space implementation to full volumetric global illumination in 3D space. It renders true 3D scenes with light bouncing through volume space using a hierarchical probe grid.

## Current Status

**Implementation Phase**: Active Development (Stubs & Framework)

### Completed Components ✅

#### Shaders (GLSL)
- `res/shaders/voxelize.comp` - Geometry voxelization compute shader
- `res/shaders/sdf_3d.comp` - 3D Signed Distance Field generation using Jump Flooding
- `res/shaders/radiance_3d.comp` - 3D radiance cascade injection with hierarchical probing
- `res/shaders/inject_radiance.comp` - Direct lighting injection (point, directional, area lights)
- `res/shaders/raymarch.frag` - Volume raymarching for final visualization

#### Core Code
- `demo3d.h` - Complete class declaration with full API documentation
- `demo3d.cpp` - Implementation stubs with detailed TODO comments
- `gl_helpers.cpp` - OpenGL helper functions for 3D texture/FBO management
- `gl_helpers.h` - Helper function declarations

#### Scene Presets
- Empty Room
- Cornell Box (classic rendering test)
- Simplified Sponza Atrium
- Maze
- Pillars Hall
- Procedural City

#### UI Panels
- Settings panel (scene selection, algorithm choice, quality settings)
- Cascade control panel (hierarchy visualization, per-cascade parameters)
- Tutorial panel (algorithm explanation, controls, performance tips)

### Pending Implementation 🚧

#### Critical Path
1. **Constructor/Destructor** - Full resource initialization
   - Load all shaders from files
   - Create volume textures and framebuffers
   - Initialize cascade hierarchy
   - Set up ImGui for 3D navigation

2. **Input Processing** - Camera and interaction
   - WASD + QE camera movement
   - Mouse rotation handling
   - Voxel/light placement

3. **Voxelization Pass** - Convert geometry to voxels
   - Bind voxelization framebuffer
   - Render scene geometry to volume
   - Handle dynamic objects

4. **Cascade Update Loop** - Hierarchical radiance computation
   - Implement temporal reprojection
   - Cascade merging logic
   - LOD selection

5. **Build System Integration**
   - Update CMakeLists.txt for 3D target
   - Link GLEW and GLM libraries
   - Configure shader copying

## Architecture

### Rendering Pipeline

```
Scene Input → Voxelization → 3D SDF (JFA) → 
Direct Lighting Injection → Radiance Cascades (Hierarchical) → 
Raymarching → Final Composite
```

### Key Data Structures

#### Volume Textures
- **Voxel Grid** (RGBA8): Surface occupancy and color
- **SDF** (R32F): Signed distance field
- **Radiance Cascades** (RGBA16F): Hierarchical probe grid
- **Direct Lighting** (RGBA16F): Emissive contribution

#### Cascade Hierarchy
```cpp
Cascade 0: 32³ probes, 0.1 unit cells (finest, near-field)
Cascade 1: 64³ probes, 0.5 unit cells
Cascade 2: 128³ probes, 2.0 unit cells
Cascade 3: 64³ probes, 8.0 unit cells
Cascade 4: 32³ probes, 32.0 unit cells (coarsest, far-field)
```

### Memory Requirements

| Resolution | Voxel Count | Memory (Est.) | Target FPS |
|------------|-------------|---------------|------------|
| 64³ | 262K | 100 MB | 60+ |
| 128³ | 2.1M | 800 MB | 30-60 |
| 256³ | 16.8M | 6.4 GB | 15-30 |

**Recommendation**: Start with 128³ for prototyping.

## Technical Requirements

### System Requirements
- **OpenGL**: 4.3+ (compute shaders required)
- **GPU Memory**: Minimum 2GB (for 128³ volume)
- **Compute Shader**: GL_ARB_compute_shader
- **Image Load/Store**: GL_ARB_shader_image_load_store

### Dependencies
- **GLEW**: OpenGL extension loading
- **GLM**: Mathematics library (vector/matrix operations)
- **Raylib**: Window/context management
- **ImGui**: User interface
- **rlImGui**: Raylib-ImGui bridge

## Building

### Prerequisites

1. Install dependencies:
   ```bash
   # Ensure submodules are initialized
   git submodule update --init
   ```

2. Verify OpenGL 4.3+ support:
   ```bash
   glxinfo | grep "OpenGL version"
   ```

### Build Commands

```powershell
.\build.ps1
.\build\RadianceCascades3D.exe
```

Running the executable with no arguments starts the **reference surface-RC interactive view** (the Phase 9 default). The legacy volumetric app is reachable through the explicit deprecation window:

```powershell
.\build\RadianceCascades3D.exe --runtime-shell=legacy
```

Key runtime commands:

| Command | Action |
|---------|--------|
| *(no args)* | Reference surface-RC interactive view (App3D default) |
| `--runtime-shell=legacy` | Legacy Demo3D app (deprecation window) |
| `--reference-render=N [--reference-render-shot=FILE]` | Reference render, N frames, optional shot |
| `--reference-pt-shot=FILE [--reference-pt-spp=N] [--reference-pt-bounces=N]` | CPU path-trace reference shot |
| `--legacy-render=N [--legacy-render-shot=FILE]` | Legacy Cornell new-RC render |
| `--legacy-pt-shot=FILE` | Legacy Cornell path-trace shot |
| `--validate-reference-<gate> --reference-<gate>-report=FILE` | Run a G1-G10 validation gate |
| `--validate-chart-provider --chart-provider-report=FILE` | Phase 11 M1 CPU UV2 packer (R4/R5/R6). No GPU. |
| `--atlas-filter=linear\|nearest` | Atlas sampling (default LINEAR; Phase 12-A) |
| `--rc-quality=parity\|high-c0` | Quality profile. Default `parity` is the only G0–G10 gate. `high-c0` is refused by validation. |
| `--occupancy-json=PATH` | Write per-cascade active/inactive counts after a reference render |
| `--auto-rdoc` | **Rejected (exit 2).** Obsolete 2026-08-22. Use `rdc capture` below. |

Runtime shaders are loaded from the canonical source directory configured by CMake. The build also synchronizes a packaging copy under `build/res/shaders`.

### GPU capture (RenderDoc) — use `rdc-cli`, not `--auto-rdoc`

`--auto-rdoc` (8s in-process `TriggerCapture` + `qrenderdoc --py rdoc_extract.py` + `analyze_renderdoc.py`) is **fail-closed**. The replacement is the `renderdoc-gpu-debug` skill (`rdc-cli`). Comparison and live evidence: `doc/journey.md` Era 12. Historical internals: `doc/13_renderdoc_auto_rdoc.md`.

This machine (not on PATH):

```powershell
$env:RENDERDOC_PYTHON_PATH = "D:\GitRepo-My\rdc-cli\.local\renderdoc"
$rdc = "D:\GitRepo-My\rdc-cli\.venv\Scripts\rdc.exe"
```

Capture a **New RC** (default App3D surface kernel) frame (from `3d/`; **do not** pass `--wait-for-exit`, `--auto-rdoc`, or `--runtime-shell=legacy`):

```powershell
& $rdc doctor
& $rdc capture --frame 24 --timeout 180 --json -- `
  .\build\RadianceCascades3D.exe --reference-render=40
```

Volumetric RC remains `--runtime-shell=legacy` only. Its Duration numbers are a different algorithm.

Inspect (always `open` → work → `close`):

```powershell
& $rdc open .\tools\captures\rdoc_frame_frame480.rdc
& $rdc info --json
& $rdc passes --json
& $rdc events --limit 50
& $rdc counters --name "GPU Duration" --json
& $rdc resources --json                    # sdfTexture / albedoTexture / cascade*_probeAtlas
& $rdc texture 59 --slice 64 -o sdf.png    # 3D mid-Z; IDs from resources
& $rdc thumbnail -o thumb.png
& $rdc close
```

| Need | Use |
|------|-----|
| Headless capture + GPU Duration + stage PNGs | `rdc capture` then the inspect block above |
| All 4 cascades in one frame **and** no ImGui overlay | Run `--runtime-shell=legacy`, press **G** (app-side `forceCascadeRebuild` + `skipUIRendering`). Then `rdc open` the `.rdc`. `rdc capture --frame N` does not hide UI and does not force stagger. |
| Forensic replay of the old extract scripts | `RDOC_LEGACY=1` only. Default is refuse (exit 2). |

### Phase 0 Baseline

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase0_baseline\run_phase0_baseline.ps1
```

This command builds Release, captures a deterministic legacy-volumetric smoke frame, verifies shader compile/hash integrity, and writes a machine-readable G0 report under `tools/10_refactor/phase0_baseline/runs/`. This is baseline evidence only and does not claim surface radiance-cascade parity.

### Phase 1 Shell Cut-Over

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase1_shell\run_phase1_shell_parity.ps1
```

Since Phase 9, `App3D` is the default runtime shell and runs the reference surface-RC renderer. `Demo3D` (the legacy volumetric app) is no longer the default and is reachable only through the explicit `--runtime-shell=legacy` deprecation window. This gate verifies the legacy baseline is stable, the app3d shell runs the reference renderer, and legacy-only flags without `--runtime-shell=legacy` fail with a usage error instead of silently falling back to old global state.

### Phase 2 Reference Scene

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\10_refactor\phase2_reference_scene\run_phase2_reference_scene.ps1
```

This CPU-only validation freezes the independent ShaderToy Cornell scene and chart contract. It does not alter legacy rendering or claim transport parity.

## Usage

### Keyboard Controls

| Key | Action |
|-----|--------|
| W/S/A/D | Camera movement (XZ plane) |
| Q/E | Camera height adjustment (Y axis) |
| Mouse Drag | Camera rotation |
| Scroll | Brush size adjustment |
| 1 | Switch to voxelization mode |
| 2 | Switch to light placement mode |
| Space | Toggle between modes |
| F1 | Toggle UI visibility |
| F2 | Take screenshot |
| G | Save a RenderDoc `.rdc` (legacy shell; no auto-extract — inspect with `rdc open`) |
| R | Reload shaders (hot-swap) |
| C | Clear scene |
| Escape | Exit application |

### UI Panels

1. **Settings Panel**
   - Scene selection (6 preset scenes)
   - Algorithm choice (RC vs traditional GI)
   - Quality settings (raymarch steps, termination threshold)
   - Performance options (sparse voxels, temporal reprojection)
   - Debug visualization toggles

2. **Radiance Cascades Panel**
   - Cascade hierarchy tree view
   - Per-cascade parameters display
   - Memory usage breakdown
   - Merge control

3. **Tutorial Panel**
   - Algorithm explanation
   - Control reference
   - Performance tips
   - About information

## Performance Optimization

### Implemented Techniques

1. **Sparse Voxel Representation**
   - Only allocate voxels near surfaces
   - Reduces memory from O(n³) to O(surface area)

2. **Temporal Reprojection**
   - Reuse previous frame's radiance
   - Blend 10% new + 90% old for stability
   - Reduces per-frame computation

3. **Adaptive Step Size**
   - SDF-guided raymarching
   - Large steps in empty space
   - Small steps near surfaces

4. **Early Ray Termination**
   - Stop when alpha > threshold (0.95)
   - Avoid unnecessary sampling

### Future Optimizations

- [ ] Frustum culling for visible region only
- [ ] GPU-driven LOD selection
- [ ] Async compute for cascade updates
- [ ] Variable rate shading for cascades
- [ ] Compression for volume textures

## Debugging

### Debug Visualizations

Enable in Settings → Debug:
- **Show Cascade Slices**: View individual Z slices of cascades
- **Show Voxel Grid**: Wireframe overlay of voxel structure
- **Show Performance Metrics**: Real-time timing graph
- **Show ImGui Demo**: Dear ImGui demo window

### Performance Metrics

Display in real-time:
- Frame time (ms) and FPS
- Voxelization pass time
- SDF generation time
- Cascade update time
- Raymarching time
- Active voxel count
- Total memory usage

### Common Issues

#### Issue: Low FPS (< 15)
**Solutions**:
- Reduce volume resolution (128³ → 64³)
- Decrease cascade count
- Reduce raymarch steps
- Enable sparse voxels

#### Issue: High Memory Usage (> 2GB)
**Solutions**:
- Use lower resolution volume
- Reduce cascade count
- Use RGBA8 instead of RGBA16F where possible

#### Issue: Visual Artifacts
**Solutions**:
- Check shader compilation logs
- Verify texture bindings
- Increase termination threshold
- Disable temporal reprojection temporarily

## File Structure

```
3d/
├── include/
│   └── gl_helpers.h          # OpenGL helper declarations
├── src/
│   ├── demo3d.h              # Main demo class header
│   ├── demo3d.cpp            # Implementation (stubs + partial impl)
│   ├── main3d.cpp            # Application entry point
│   └── gl_helpers.cpp        # OpenGL helpers implementation
├── res/
│   └── shaders/
│       ├── voxelize.comp         # Voxelization compute shader
│       ├── sdf_3d.comp           # 3D SDF generation
│       ├── radiance_3d.comp      # Radiance cascade injection
│       ├── inject_radiance.comp  # Direct lighting injection
│       └── raymarch.frag         # Volume raymarching
├── MIGRATION_TO_3D.md        # Comprehensive migration guide
├── IMPLEMENTATION_SUMMARY.md # Implementation notes
└── README.md                 # This file
```

## Testing Strategy

### Incremental Validation

1. ✅ **Shader Compilation**: All shaders compile without errors
2. 🔄 **Volume Creation**: Successfully create 3D textures
3. ⏳ **Voxelization**: Test with simple geometry (cube)
4. ⏳ **SDF Generation**: Validate distance field correctness
5. ⏳ **Single Cascade**: Test finest cascade only
6. ⏳ **Multiple Cascades**: Add levels incrementally
7. ⏳ **Full Integration**: Complete pipeline

### Unit Tests (Planned)

- Voxel box creation
- SDF accuracy verification
- Cascade parameter validation
- Memory allocation checks

## Known Limitations

1. **Memory**: 128³ volume requires ~800MB VRAM
2. **Performance**: Real-time only at lower resolutions
3. **Geometry Complexity**: Limited by SSBO size
4. **Dynamic Scenes**: Requires re-voxelization every frame

## Future Enhancements

### Short-term (v0.1)
- [ ] Complete constructor/destructor
- [ ] Implement input handling
- [ ] Basic scene interaction
- [ ] Working voxelization

### Medium-term (v0.2)
- [ ] Temporal reprojection
- [ ] Sparse voxel octree
- [ ] Multiple light types
- [ ] Material system

### Long-term (v1.0)
- [ ] Dynamic geometry support
- [ ] Animated characters
- [ ] Reflections
- [ ] Refractions
- [ ] Participating media

## References

### Academic Papers
1. Sannikov, A. "Real-Time Global Illumination using Radiance Cascades"
2. McGuire, M. et al. "Sparse Voxel Octree Global Illumination"
3. Crassin, C. et al. "Voxel Cone Tracing"

### Online Resources
- [LearnOpenGL - Compute Shaders](https://learnopengl.com/Advanced-OpenGL/Compute-Shaders)
- [Khronos OpenGL Registry](https://www.khronos.org/opengl/)
- [Raylib Documentation](https://www.raylib.com/)

## Contributing

When contributing to this codebase:
1. Follow existing code style (C++23, camelCase)
2. Document all public APIs with Doxygen comments
3. Test shaders on multiple GPUs
4. Profile performance impact of changes
5. Update this README with new features

## License

Same as the main project (check root LICENSE file).

## Contact

For questions or issues:
- GitHub Issues: [Create an issue]
- Email: [Check repository]

---

**Last Updated**: 2026-08-22  
**Version**: 0.1 (Work in Progress)
