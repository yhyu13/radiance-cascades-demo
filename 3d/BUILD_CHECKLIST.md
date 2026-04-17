# Build & Test Checklist - Quick Start Implementation

**Date:** 2026-04-17  
**Purpose:** Verify the quick start implementation works correctly  

---

## Pre-Build Checks

### System Requirements
- [ ] Windows 10/11 installed
- [ ] Visual Studio 2019+ or MinGW-w64 installed
- [ ] CMake 3.25+ installed (`cmake --version`)
- [ ] Git installed (for submodules)
- [ ] GPU supports OpenGL 4.3+

### Dependencies Check
Run these commands in PowerShell to verify dependencies:

```powershell
# Check CMake version
cmake --version

# Check if GLEW is available (optional - will be downloaded if missing)
where glew32.dll

# Check if GLM headers are available (optional - header-only)
# GLM should be found by CMake or bundled
```

### Repository Setup
```powershell
# Navigate to project root
cd c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo

# Initialize submodules (if not already done)
git submodule update --init --recursive

# Navigate to 3D directory
cd 3d
```

---

## Build Process

### Option 1: Automated Build (Recommended)

```powershell
# From 3d/ directory
.\build.ps1
```

**Expected Output:**
```
========================================
Building 3D Radiance Cascades
========================================

Creating build directory...
Running CMake...
-- Building for: Visual Studio 17 2022
-- Selecting Windows SDK version 10.0.xxxxx.0
-- The CXX compiler identification is MSVC 19.xx.xxxxx
-- Detecting CXX compiler ABI info
...
-- Configuring done
-- Generating done
-- Build files have been written to: .../3d/build

Building project...
Microsoft (R) Build Engine version 16.xx.x+...
Copyright (C) Microsoft Corporation. All rights reserved.

  Checking Build System
  Building Custom Rule .../3d/CMakeLists.txt
  demo3d.cpp
  gl_helpers.cpp
  main3d.cpp
  RadianceCascades3D.vcxproj -> ...\3d\build\Release\RadianceCascades3D.exe

========================================
Build successful!
========================================

Executable location: build/RadianceCascades3D.exe
```

### Option 2: Manual Build

```powershell
# From 3d/ directory
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## Post-Build Verification

### Check Executable Exists
```powershell
Test-Path build\Release\RadianceCascades3D.exe
# Should return: True
```

### Check Shaders Copied
```powershell
Test-Path build\res\shaders\voxelize.comp
# Should return: True

# List all shaders
Get-ChildItem build\res\shaders\*.comp
# Should show: voxelize.comp, sdf_3d.comp, radiance_3d.comp, inject_radiance.comp
```

### Check Resource Directory
```powershell
# Verify res folder exists relative to where you'll run
Test-Path res\shaders
# If False, copy it:
Copy-Item -Recurse res build\
```

---

## Runtime Testing

### Launch Application

```powershell
# IMPORTANT: Run from 3d/ directory (not build/)
cd c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d
.\build\Release\RadianceCascades3D.exe
```

**OR** copy executable to root and run from there:
```powershell
Copy-Item build\Release\RadianceCascades3D.exe .
.\RadianceCascades3D.exe
```

### Expected Console Output

```
========================================
  Radiance Cascades 3D Demo
  Version: 0.1.0
========================================
[INIT] Setting up ImGui...
[INFO] OpenGL Version: 4.6.0 NVIDIA xxx.xx
[INFO] GLSL Version: 4.60 NVIDIA
[INFO] Renderer: NVIDIA GeForce RTX xxxx
[INFO] Vendor: NVIDIA Corporation
[CHECK] OpenGL Version: 4.6
[CHECK] Compute shaders: Supported
[CHECK] Image load/store: Supported
[CHECK] Max 3D Texture Size: 16384
[CHECK] Max Compute Work Groups: 2147483647 x 65535 x 65535
[CHECK] All requirements satisfied.
[MAIN] Creating 3D demo instance...
========================================
[Demo3D] Initializing 3D Radiance Cascades
========================================
[Demo3D] Camera reset to position: 0, 2, 5
[Demo3D] Creating volume buffers at resolution 128^3
[Demo3D] Memory usage: ~64.0 MB
[Demo3D] Volume buffers created successfully
[Demo3D] Loading shaders...
[Demo3D] Loading shader: res/shaders/voxelize.comp
[Demo3D] Shader loaded successfully: voxelize.comp
[Demo3D] Loading shader: res/shaders/sdf_3d.comp
[Demo3D] Shader loaded successfully: sdf_3d.comp
[Demo3D] Loading shader: res/shaders/radiance_3d.comp
[Demo3D] Shader loaded successfully: radiance_3d.comp
[Demo3D] Loading shader: res/shaders/inject_radiance.comp
[Demo3D] Shader loaded successfully: inject_radiance.comp
[Demo3D] Initialized 6 cascade levels
[Demo3D] Setting up initial scene...
[Demo3D] Loading: Empty Room
========================================
[Demo3D] Initialization complete!
========================================
[Demo3D] Volume resolution: 128³
[Demo3D] Memory usage: ~64.0 MB
[Demo3D] Shaders loaded: 4
========================================

[MAIN] Entering main loop.
```

### Expected Visual Output

1. **Window Opens:** 1280x720 pixels
2. **Background:** Dark gray/black
3. **UI Panels Visible:**
   - Top-left: "3D RC Settings"
     - Shows volume resolution
     - Shows memory usage
     - "Reload Shaders" button
     - "Reset Camera" button
   - Another panel: "Cascades"
     - Lists 6 cascade levels
   - Tutorial panel: "3D Radiance Cascades - Quick Start"
     - Status indicator
     - Controls list
     - Feature checklist
4. **FPS Counter:** Top-left corner (Raylib default)

### Interactive Testing

Try these actions:

- [ ] **Click "Reload Shaders"** - Console should show reloading messages
- [ ] **Click "Reset Camera"** - Camera resets to default position
- [ ] **Toggle checkboxes** in settings panel
- [ ] **Resize window** - Should handle gracefully
- [ ] **Close window** - Clean shutdown, no crashes

---

## Troubleshooting

### Issue: "Cannot find GLEW"

**Solution:**
```powershell
# Install via vcpkg
vcpkg install glew:x64-windows

# Or download from https://glew.sourceforge.net/
# Extract and add to PATH
```

### Issue: "Cannot open include file: 'GL/glew.h'"

**Solution:**
```powershell
# Ensure GLEW include directory is in CMake path
# Or set environment variable:
$env:GLEW_ROOT = "C:\path\to\glew"
```

### Issue: "Shaders failed to compile"

**Check:**
1. Shader files exist: `Test-Path res\shaders\voxelize.comp`
2. Read permissions on shader files
3. No syntax errors in shaders (check console output)

**Fix:**
```powershell
# Copy shaders manually
Copy-Item -Recurse res\shaders build\res\
```

### Issue: "Black screen, nothing visible"

**This is EXPECTED for quick start!**
- Raymarching is a placeholder
- Only UI panels should be visible
- Check console for errors

If UI is also missing:
1. Check ImGui initialized correctly
2. Verify OpenGL context created
3. Look for error messages in console

### Issue: "Application crashes on startup"

**Debug Steps:**
1. Run in debugger (Visual Studio)
2. Check console output before crash
3. Verify all resource paths correct
4. Check OpenGL version meets minimum (4.3+)

**Common Causes:**
- Missing shader files
- Insufficient OpenGL support
- Out of memory (reduce volumeResolution)

### Issue: "Low FPS (< 30)"

**Solutions:**
1. Reduce volume resolution in constructor (change 128 to 64)
2. Disable debug visualizations
3. Update GPU drivers
4. Close other GPU-intensive applications

---

## Performance Benchmarks

### Expected Performance (RTX 3060 or equivalent)

| Metric | Expected Value |
|--------|----------------|
| Initialization Time | < 1 second |
| Frame Time (empty) | < 1 ms |
| FPS (vsync off) | 200+ FPS |
| FPS (vsync on) | 60 FPS (monitor refresh) |
| VRAM Usage | ~100-150 MB |
| CPU Usage | < 5% |

### Memory Breakdown

| Component | Size |
|-----------|------|
| Voxel Grid (128³ RGBA8) | ~8 MB |
| SDF Volume (128³ R32F) | ~8 MB |
| Lighting Textures (3x RGBA16F) | ~48 MB |
| Cascade Probes (6 levels) | ~20 MB |
| Overhead | ~16 MB |
| **Total** | **~100 MB** |

---

## Validation Checklist

After building and running, verify:

### Build Success
- [ ] No CMake configuration errors
- [ ] No compilation errors
- [ ] No linker errors
- [ ] Executable created in `build/Release/`
- [ ] Shaders copied to build directory

### Runtime Success
- [ ] Window opens without crash
- [ ] Console shows initialization messages
- [ ] No OpenGL errors in console
- [ ] All 4 shaders load successfully
- [ ] Scene loads (Empty Room)
- [ ] Memory allocation succeeds (~64 MB)

### UI Verification
- [ ] ImGui panels render
- [ ] Settings panel visible
- [ ] Cascades panel shows 6 levels
- [ ] Tutorial panel displays
- [ ] Buttons respond to clicks
- [ ] Checkboxes toggle

### Functional Tests
- [ ] "Reload Shaders" button works
- [ ] "Reset Camera" button works
- [ ] Window resize handled
- [ ] Clean shutdown on close
- [ ] No memory leaks (basic check)

---

## Next Steps After Successful Build

Once everything is working:

1. **Read the Documentation:**
   - [ ] Review `QUICKSTART.md` for user guide
   - [ ] Read `refactor_plan.md` for implementation roadmap
   - [ ] Check `IMPLEMENTATION_SUMMARY_QUICKSTART.md` for what's done

2. **Explore the Code:**
   - [ ] Open `demo3d.cpp` and find implemented methods
   - [ ] Look for `// TODO:` comments (these are stubs)
   - [ ] Check `gl_helpers.cpp` (fully implemented)

3. **Choose Your Path:**
   - **Option A:** Implement SDF generation (high priority)
   - **Option B:** Add basic raymarching (visual feedback)
   - **Option C:** Improve camera controls (usability)
   - **Option D:** Follow full refactor plan (complete implementation)

4. **Join Development:**
   - Pick a task from `refactor_plan.md`
   - Implement one function at a time
   - Test thoroughly
   - Document your changes

---

## Reporting Issues

If you encounter problems not covered above:

1. **Check Existing Docs:**
   - Search `QUICKSTART.md` troubleshooting section
   - Review console error messages
   - Check OpenGL version compatibility

2. **Gather Information:**
   - OS version
   - GPU model and driver version
   - CMake version
   - Compiler version
   - Full error message from console

3. **Create Minimal Reproduction:**
   - Exact steps to reproduce
   - Expected vs actual behavior
   - Screenshots if applicable

---

## Success Criteria

You've successfully completed the quick start when:

✅ Project builds without errors  
✅ Application runs without crashing  
✅ UI panels are visible and interactive  
✅ Console shows successful initialization  
✅ All shaders load correctly  
✅ You understand what's implemented vs stubbed  
✅ You know how to proceed with next steps  

---

**Congratulations!** 🎉

You now have a working foundation for 3D Radiance Cascades. The hard part (getting it to compile and run) is done. Now comes the fun part: implementing the actual algorithm!

Refer to `refactor_plan.md` for detailed guidance on implementing the missing features.

---

*Last Updated: 2026-04-17*
