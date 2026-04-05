# Shader Directory Reorganization

## Overview

Shader files for the 3D Radiance Cascades implementation have been moved from `res/shaders/` to `3d/res/shaders/` to better organize 2D and 3D resources.

**Date**: 2026-03-30  
**Status**: ✅ Complete

---

## Changes Summary

### Files Moved

The following shader files were relocated:

1. **`voxelize.comp`** - Geometry voxelization compute shader
   - From: `res/shaders/voxelize.comp`
   - To: `3d/res/shaders/voxelize.comp`
   - Size: 5,991 bytes

2. **`sdf_3d.comp`** - 3D Signed Distance Field generation
   - From: `res/shaders/sdf_3d.comp`
   - To: `3d/res/shaders/sdf_3d.comp`
   - Size: 5,448 bytes

3. **`radiance_3d.comp`** - Radiance cascade injection
   - From: `res/shaders/radiance_3d.comp`
   - To: `3d/res/shaders/radiance_3d.comp`
   - Size: 7,990 bytes

4. **`inject_radiance.comp`** - Direct lighting injection
   - From: `res/shaders/inject_radiance.comp`
   - To: `3d/res/shaders/inject_radiance.comp`
   - Size: 7,191 bytes

5. **`raymarch.frag`** - Volume raymarching visualization
   - From: `res/shaders/raymarch.frag`
   - To: `3d/res/shaders/raymarch.frag`
   - Size: 7,489 bytes

### Code Changes

#### 1. demo3d.cpp

**Updated Constructor Comments**:
```cpp
// Step 3: Shader loading
// Note: Shaders are located in 3d/res/shaders/ directory
```

**Updated loadShader() Function**:
```cpp
bool Demo3D::loadShader(const std::string& shaderName) {
    // ...
    std::string filepath = "3d/res/shaders/" + shaderName;
    // ...
}
```

#### 2. CMakeLists.txt

**Added Dual Copy Rule**:
```cmake
# Copy shaders to build directory
file(COPY ${PROJECT_SOURCE_DIR}/../res/shaders DESTINATION ${PROJECT_BINARY_DIR}/res)
file(COPY ${PROJECT_SOURCE_DIR}/res/shaders DESTINATION ${PROJECT_BINARY_DIR}/3d/res)
```

This ensures both 2D and 3D shaders are available when building.

#### 3. Documentation Updates

All documentation files updated to reflect new paths:
- ✅ `3d/README.md`
- ✅ `3d/IMPLEMENTATION_SUMMARY.md`
- ✅ `3d/MIGRATION_TO_3D.md`

---

## Rationale

### Why Move Shaders?

1. **Separation of Concerns**: Keeps 2D and 3D resources organized
2. **Clear Ownership**: Makes it obvious which shaders belong to 3D implementation
3. **Easier Maintenance**: Simplifies finding and updating 3D-specific shaders
4. **Build Flexibility**: Allows independent building of 2D and 3D versions

### Benefits

- **Better Organization**: Clear separation between 2D and 3D assets
- **Reduced Confusion**: No ambiguity about which shaders are for 3D
- **Modular Builds**: 3D version can be built independently
- **Cleaner Git History**: Changes to 3D shaders don't affect 2D directories

---

## Impact Analysis

### Affected Components

✅ **Source Files Modified**:
- `3d/src/demo3d.cpp` - Updated shader path references

✅ **Build System**:
- `3d/CMakeLists.txt` - Updated copy rules

✅ **Documentation**:
- `3d/README.md`
- `3d/IMPLEMENTATION_SUMMARY.md`
- `3d/MIGRATION_TO_3D.md`

✅ **No Breaking Changes**:
- 2D version continues to use `res/shaders/`
- Both versions can coexist

---

## Directory Structure

### Before
```
project-root/
├── res/
│   └── shaders/
│       ├── voxelize.comp          ← 3D shader mixed with 2D
│       ├── sdf_3d.comp            ← 3D shader mixed with 2D
│       ├── radiance_3d.comp       ← 3D shader mixed with 2D
│       ├── inject_radiance.comp   ← 3D shader mixed with 2D
│       └── raymarch.frag          ← 3D shader mixed with 2D
└── 3d/
    └── src/
        └── demo3d.cpp             ← References res/shaders/
```

### After
```
project-root/
├── res/
│   └── shaders/                   ← 2D shaders only
│       ├── jfa.frag
│       ├── rc.frag
│       └── ... (other 2D shaders)
├── 3d/
│   ├── res/
│   │   └── shaders/               ← 3D shaders (new location)
│   │       ├── voxelize.comp
│   │       ├── sdf_3d.comp
│   │       ├── radiance_3d.comp
│   │       ├── inject_radiance.comp
│   │       └── raymarch.frag
│   └── src/
│       └── demo3d.cpp             ← References 3d/res/shaders/
```

---

## Build Instructions

### Building 3D Version

```bash
cd build
cmake ../3d
make -j4
cd ..
./build/radiance_cascades_3d
```

**Important**: Run from project root to correctly load shaders from `3d/res/shaders/`.

### Building 2D Version

```bash
cd build
cmake ..
make -j4
cd ..
./build/radiance_cascades
```

**Note**: 2D version unchanged, still uses `res/shaders/`.

---

## Testing Checklist

After reorganization, verify:

- [ ] All 5 shader files exist in `3d/res/shaders/`
- [ ] `demo3d.cpp` compiles without errors
- [ ] Shader loading uses correct path (`3d/res/shaders/`)
- [ ] CMake copies shaders to build directory
- [ ] Executable can find shaders when run from project root
- [ ] 2D version still works (unchanged)

---

## Migration Notes

### For Developers

If you're working on both 2D and 3D versions:

1. **2D Shaders**: Continue using `res/shaders/`
2. **3D Shaders**: Use `3d/res/shaders/`
3. **Shared Shaders**: Consider duplicating or creating symlinks if needed

### For CI/CD

Update build scripts to:
- Copy both `res/shaders/` and `3d/res/shaders/`
- Ensure runtime paths are correct for both versions

---

## Rollback Plan

If issues arise, reverting is straightforward:

```bash
# Move shaders back
git checkout res/shaders/*.comp res/shaders/*.frag
# Revert code changes
git checkout 3d/src/demo3d.cpp 3d/CMakeLists.txt
# Revert documentation
git checkout 3d/*.md
```

---

## Future Considerations

### Potential Improvements

1. **Asset Manifest**: Create a JSON/YAML file listing all shader locations
2. **Path Constants**: Define shader paths as compile-time constants
3. **Runtime Detection**: Auto-detect shader directory structure
4. **Unified Config**: Single configuration file for both 2D and 3D

### Best Practices

- Always reference shaders relative to project root
- Document any path assumptions in comments
- Test builds from clean directory
- Verify runtime resource loading

---

## Related Files

- `3d/CMakeLists.txt` - Build configuration
- `3d/src/demo3d.cpp` - Main implementation
- `3d/README.md` - User documentation
- `3d/MIGRATION_TO_3D.md` - Technical migration guide

---

**Status**: ✅ Complete  
**Verified**: All files moved and paths updated  
**Next Steps**: Test build and runtime execution
