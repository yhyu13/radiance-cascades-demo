# 3D Radiance Cascades - Quick Start Implementation

**Status:** Minimal Working Example (MWE)  
**Last Updated:** 2026-04-17  
**Version:** 0.1.0-alpha  

---

## Overview

This is a **quick-start implementation** of the 3D Radiance Cascades system. It provides a minimal working foundation with the following features:

### ✅ Implemented Features

- **Volume Textures**: 3D texture creation and management (RGBA8, R32F, RGBA16F)
- **Basic Voxelization**: Procedural box placement via `addVoxelBox()`
- **Shader Loading**: Compute shader compilation and loading
- **Scene Presets**: Empty room, Cornell box, simple test scenes
- **Camera System**: 3D perspective camera with Raylib integration
- **UI Framework**: ImGui panels for settings and debugging
- **Build System**: Clean CMakeLists.txt with proper dependencies

### ❌ Not Yet Implemented (Placeholders)

- **SDF Generation**: Jump Flooding Algorithm stubbed out
- **Raymarching**: Basic background clear only
- **Cascade Updates**: Structure in place, no actual computation
- **Direct Lighting**: Placeholder function
- **Temporal Reprojection**: Not yet coded

---

## Quick Start Guide

### Prerequisites

- **Windows 10/11** (PowerShell script provided)
- **CMake 3.25+**
- **Visual Studio 2019+** or **MinGW-w64**
- **OpenGL 4.3+** compatible GPU
- **Git** (for submodules)

### Building

#### Option 1: PowerShell Script (Recommended)

```powershell
cd 3d
.\build.ps1
```

#### Option 2: Manual Build

```powershell
cd 3d
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Running

```powershell
cd 3d/build
./RadianceCascades3D.exe
```

**Important:** The executable must be run from the directory containing the `res/` folder, or shaders won't load!

---

## Project Structure

```
3d/
├── src/
│   ├── main3d.cpp          # Application entry point
│   ├── demo3d.h            # Demo3D class declaration
│   ├── demo3d.cpp          # Demo3D implementation (partially complete)
│   ├── gl_helpers.h        # OpenGL helper declarations
│   ├── gl_helpers.cpp      # OpenGL helper implementations ✅ COMPLETE
│   └── config.h.in         # CMake configuration template ✅ NEW
├── include/
│   └── gl_helpers.h        # Public OpenGL API
├── res/
│   └── shaders/            # GLSL compute shaders
│       ├── voxelize.comp
│       ├── sdf_3d.comp
│       ├── radiance_3d.comp
│       ├── inject_radiance.comp
│       └── raymarch.frag
├── shader_toy/             # Reference implementations
│   ├── Common.glsl
│   ├── CubeA.glsl
│   └── Image.glsl
├── CMakeLists.txt          # Build configuration ✅ FIXED
├── build.ps1               # Windows build script ✅ NEW
├── refactor_plan.md        # Comprehensive implementation roadmap ✅ NEW
└── QUICKSTART.md           # This file ✅ NEW
```

---

## Current Architecture

### Rendering Pipeline (Simplified)

```
┌─────────────┐
│ Scene Setup │ ← addVoxelBox() creates voxel data
└──────┬──────┘
       │
       ▼
┌──────────────┐
│ Voxelization │ ← Currently just marks scene as dirty
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ SDF Gen      │ ← PLACEHOLDER (not implemented)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Cascades     │ ← PLACEHOLDER (structure ready)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Raymarch     │ ← PLACEHOLDER (clears to background)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ UI Overlay   │ ← ImGui panels working
└──────────────┘
```

### Memory Layout

For default 128³ volume resolution:

| Texture | Format | Size | Purpose |
|---------|--------|------|---------|
| Voxel Grid | RGBA8 | ~8 MB | Geometry storage |
| SDF | R32F | ~8 MB | Distance field |
| Direct Lighting | RGBA16F | ~16 MB | Light injection |
| Prev Frame | RGBA16F | ~16 MB | Temporal reprojection |
| Current Radiance | RGBA16F | ~16 MB | Cascade output |
| **Total** | | **~64 MB** | |

*Note: Actual usage may vary based on driver alignment*

---

## Testing the Build

After building, you should see:

1. **Window opens** with dark gray background
2. **ImGui panels** visible:
   - "3D RC Settings" (top-left)
   - "Cascades" (shows cascade info)
   - "3D Radiance Cascades - Quick Start" (tutorial)
3. **Console output** showing:
   ```
   ========================================
   [Demo3D] Initializing 3D Radiance Cascades
   ========================================
   [Demo3D] Creating volume buffers at resolution 128^3
   [Demo3D] Memory usage: ~64.0 MB
   [Demo3D] Loading shaders...
   [Demo3D] Shader loaded successfully: voxelize.comp
   ...
   ========================================
   [Demo3D] Initialization complete!
   ========================================
   ```

If you see errors about missing shaders:
- Make sure you're running from the correct directory
- Check that `res/shaders/` exists relative to the executable
- Verify shader files are copied to build directory

---

## Next Steps (Following refactor_plan.md)

To progress from this quick start to a full implementation:

### Phase 1: Foundation (Week 1-2) ✅ PARTIALLY DONE
- [x] OpenGL helpers implemented
- [x] Volume textures created
- [x] Basic scene geometry
- [ ] **TODO**: Implement full voxelization pass
- [ ] **TODO**: Implement 3D JFA for SDF generation

### Phase 2: Core Algorithm (Week 3-4)
- [ ] Implement single cascade level
- [ ] Add direct lighting injection
- [ ] Test raymarching visualization
- [ ] Debug probe sampling

### Phase 3: Cascade Hierarchy (Week 5-6)
- [ ] Multi-level cascade system
- [ ] Weighted bilinear merging
- [ ] Temporal reprojection

### Phase 4: Optimization (Week 7-8)
- [ ] Sparse voxel octree
- [ ] Performance profiling
- [ ] UI polish

See [`refactor_plan.md`](refactor_plan.md) for detailed implementation steps.

---

## Troubleshooting

### Build Errors

**Error: "Cannot find GLEW"**
```powershell
# Install GLEW via vcpkg or download from source
vcpkg install glew:x64-windows
```

**Error: "Cannot find GLM"**
```powershell
# GLM is header-only, ensure it's in include path
# Or install via package manager
vcpkg install glm:x64-windows
```

**Error: "Shaders not found"**
- Run executable from directory containing `res/` folder
- Or copy shaders to same directory as executable:
  ```powershell
  Copy-Item -Recurse res/shaders build/res/
  ```

### Runtime Issues

**Black screen**
- Expected for quick start (raymarching is placeholder)
- Check console for error messages
- Verify OpenGL 4.3+ context created

**Low FPS**
- Reduce `volumeResolution` in constructor (try 64 instead of 128)
- Disable debug visualizations

**Crash on startup**
- Check console output for specific error
- Verify all shader files exist in `res/shaders/`
- Ensure GPU supports OpenGL 4.3+

---

## Code Quality Notes

### What's Production-Ready

✅ **Complete and Tested:**
- `gl_helpers.cpp` - All OpenGL wrapper functions
- Volume texture creation/destruction
- Shader loading and compilation
- Basic UI framework

⚠️ **Stubbed/Placeholder:**
- Most rendering pipeline methods
- SDF generation
- Cascade updates
- Raymarching

### Known Limitations

1. **No actual GI calculation** - cascades don't compute lighting yet
2. **No raymarching** - just clears to background color
3. **Memory inefficient** - uses dense volumes, not sparse octrees
4. **No error recovery** - shader failures may crash

---

## Contributing

If you want to help implement the missing features:

1. Read [`refactor_plan.md`](refactor_plan.md) for detailed roadmap
2. Pick a milestone from Phase 2 or later
3. Implement one function at a time
4. Test thoroughly before committing
5. Update this document with progress

### Good First Tasks

- Implement `sdfGenerationPass()` using 3D JFA algorithm
- Add basic raymarching through voxel grid
- Create more test scenes in `setScene()`
- Add camera controls (WASD + mouse look)

---

## References

- **ShaderToy Reference**: `shader_toy/` directory
- **2D Implementation**: `../src/demo.cpp` for patterns
- **Migration Guide**: `MIGRATION_TO_3D.md`
- **Full Plan**: `refactor_plan.md`

---

## License

Same as parent project - check root LICENSE file.

---

**Remember:** This is a **quick start** to get something compiling and running. The real work begins when implementing the actual radiance cascade algorithm! 🚀
