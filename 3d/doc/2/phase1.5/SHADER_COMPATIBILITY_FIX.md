# Shader Compatibility Fix - OpenGL 3.3/4.3 Migration

## Overview

Fixed all compute shaders to be compatible with the available OpenGL context (3.3 Core Profile with GLSL 4.60 support) by downgrading from GLSL 450 to 430 and removing `layout(binding = X)` qualifiers.

## Problem

The original shaders used:
- `#version 450 core` - Requires OpenGL 4.5
- `layout(binding = X)` - Automatic binding point assignment (GL 4.2+)
- Invalid syntax from debug code (`if (binding == X)`)
- Reserved keyword conflicts (`sample` as variable name)
- Invalid format specifiers (`rgb32f` instead of `rgba32f`)

Raylib on this system creates an OpenGL 3.3 context, causing compilation failures.

## Solution Applied

### 1. Version Downgrade
Changed all compute shaders from `#version 450 core` to `#version 430 core`:
- ✅ inject_radiance.comp
- ✅ radiance_3d.comp
- ✅ sdf_3d.comp
- ✅ voxelize.comp
- ✅ sdf_analytic.comp (already at 430)

**Rationale**: GLSL 430 is the minimum version that supports compute shaders (introduced in OpenGL 4.3).

### 2. Removed Binding Qualifiers
Removed `binding = X` from all image/sampler declarations:

**Before:**
```glsl
layout(rgba16f, binding = 1) uniform image3D oRadiance;
layout(r32f, binding = 0) uniform sampler3D uSDF;
```

**After:**
```glsl
layout(rgba16f) uniform image3D oRadiance;
uniform sampler3D uSDF;
```

**Note**: Bindings are now set manually in CPU code using `glBindImageTexture()` and `glUniform1i()`.

### 3. Fixed Format Specifiers
Changed invalid `rgb32f` to valid `rgba32f`:
```glsl
// Before
layout(rgb32f) uniform image3D oVoronoi;

// After
layout(rgba32f) uniform image3D oVoronoi;
```

### 4. Removed Sampler Format Qualifiers
Format qualifiers only apply to images, not samplers:
```glsl
// Before (INVALID)
layout(r32f) uniform sampler3D uSDF;

// After
uniform sampler3D uSDF;
```

### 5. Fixed Reserved Keyword Conflicts
Renamed variables using GLSL reserved keywords:
```glsl
// Before
vec4 sample = texture(uRadianceTexture, texCoord);

// After
vec4 radianceSample = texture(uRadianceTexture, texCoord);
```

### 6. Inlined Image Function Calls
GLSL 430 doesn't allow passing images as function parameters without proper qualifiers. Inlined `safeLoad()` calls:

**Before:**
```glsl
vec4 safeLoad(image3D img, ivec3 pos) {
    if (any(lessThan(pos, ivec3(0))) || ...) 
        return vec4(INF, 0.0, 0.0, 0.0);
    return imageLoad(img, pos);
}

float getDensity(ivec3 pos) {
    return safeLoad(uVoxelGrid, pos).a;
}
```

**After:**
```glsl
float getDensity(ivec3 pos) {
    if (any(lessThan(pos, ivec3(0))) || any(greaterThanEqual(pos, uVolumeSize))) {
        return 0.0;
    }
    return imageLoad(uVoxelGrid, pos).a;
}
```

### 7. Fixed Debug Code Syntax
Commented out debug blocks that incorrectly used `binding` as a variable:

**Before:**
```glsl
if (binding == 2) {
    vec4 voronoiData = safeLoad(oVoronoi, samplePos);
    ...
}
```

**After:**
```glsl
// TODO: Optional Voronoi diagram output
// if (enableVoronoi) {
//     vec4 voronoiData = imageLoad(oVoronoi, samplePos);
//     ...
// }
```

### 8. Fixed Invalid Sampler Comparison
Replaced invalid sampler comparison with integer flag:

**Before:**
```glsl
if (uCascadeIndex > 0 && uPrevRadiance != sampler3D(0)) {
    // Can't compare samplers!
}
```

**After:**
```glsl
// Added uniform: uniform int uHasPreviousFrame;
if (uCascadeIndex > 0 && uHasPreviousFrame > 0) {
    // Valid check
}
```

## Files Modified

| File | Changes |
|------|---------|
| `res/shaders/inject_radiance.comp` | Version downgrade, removed bindings, fixed debug code |
| `res/shaders/radiance_3d.comp` | Version downgrade, removed bindings, fixed sampler comparison, added `uHasPreviousFrame` uniform |
| `res/shaders/sdf_3d.comp` | Version downgrade, removed bindings, fixed formats, inlined safeLoad, commented debug code |
| `res/shaders/voxelize.comp` | Version downgrade, removed bindings, fixed debug code syntax |
| `res/shaders/radiance_debug.frag` | Renamed `sample` variable to `radianceSample` |
| `src/main3d.cpp` | Removed invalid `FLAG_GRAPHICS_API_OPENGL_43`, added working directory fix |

## Test Results

All compute shaders now compile and link successfully:

```
[Demo3D] Shader loaded successfully: voxelize.comp
[Demo3D] Shader loaded successfully: sdf_3d.comp
[Demo3D] Shader loaded successfully: sdf_analytic.comp
[Demo3D] Shader loaded successfully: radiance_3d.comp
[Demo3D] Shader loaded successfully: inject_radiance.comp
```

Cascade initialization verified:
```
[OK] Cascade initialized: 32^3, cell=0.1, texID=9
[OK] Cascade initialized: 64^3, cell=0.4, texID=10
[OK] Cascade initialized: 128^3, cell=1.6, texID=11
[OK] Cascade initialized: 32^3, cell=6.4, texID=12
[OK] Cascade initialized: 64^3, cell=25.6, texID=13
[OK] Cascade initialized: 128^3, cell=102.4, texID=14
```

## Lessons Learned

1. **OpenGL Version Mismatch**: Always verify the actual OpenGL context version provided by your windowing library (Raylib/GLFW) before assuming shader compatibility.

2. **Binding Qualifiers**: While convenient, `layout(binding = X)` requires GL 4.2+. For broader compatibility, set bindings manually in CPU code.

3. **Reserved Keywords**: GLSL has many reserved keywords (`sample`, `binding`, etc.). Avoid using them as variable names.

4. **Image vs Sampler**: Format qualifiers (`r32f`, `rgba16f`, etc.) only apply to `image*` types, not `sampler*` types.

5. **Function Parameters**: Passing images as function parameters in GLSL requires careful qualifier matching. Inlining is often safer for portability.

6. **Debug Code Hygiene**: Never leave incomplete debug blocks with syntax errors. Comment them properly or remove them.

## Next Steps

With all shaders compiling successfully, the next tasks are:

1. **Option B**: Implement cascade update algorithm (propagate radiance between levels)
2. **Option C**: Connect debug visualizations to actual cascade data
3. Create missing vertex shaders for debug visualization ([sdf_debug.vert](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\res\shaders\sdf_debug.vert), [radiance_debug.vert](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\res\shaders\radiance_debug.vert), [lighting_debug.vert](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\res\shaders\lighting_debug.vert))

## References

- [OpenGL 4.3 Specification](https://www.khronos.org/registry/OpenGL/specs/gl/glspec43.core.pdf)
- [GLSL 4.30 Specification](https://www.khronos.org/registry/OpenGL/specs/gl/GLSLangSpec.4.30.pdf)
- [Raylib Documentation](https://www.raylib.com/)
