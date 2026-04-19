# Cornell Box Scene - Quick Guide

## Overview

The **Cornell Box** is a classic computer graphics test scene used to validate global illumination algorithms. It's now available in our Radiance Cascades demo!

---

## What is the Cornell Box?

The Cornell Box is a simple room with:
- **Red wall** (left side) - Tests red color bleeding
- **Green wall** (right side) - Tests green color bleeding  
- **White walls** (back, floor, ceiling) - Neutral surfaces
- **Two boxes** in the center - Test occlusion and shadows
- **Light source** on ceiling - Provides illumination

### Why It's Important

The Cornell Box demonstrates:
1. ✅ **Color bleeding** - Red/green light bouncing onto white surfaces
2. ✅ **Soft shadows** - Area light effects
3. ✅ **Indirect lighting** - Light bouncing multiple times
4. ✅ **Material interaction** - How different colors affect each other

This makes it perfect for validating our radiance cascade pipeline!

---

## Available Scenes

Our demo currently supports **3 scenes**:

| Scene ID | Name | Description | Best For |
|----------|------|-------------|----------|
| **0** | Empty Room | Simple box with 3 walls | Testing basic SDF generation |
| **1** | **Cornell Box** | Classic GI test scene | **Validating color bleeding** ⭐ |
| **2** | Simplified Sponza | Large hall with pillars | Testing large-scale cascades |

---

## How to Switch Scenes

### Method 1: Quick Start GUI (Recommended)

1. Launch the application
2. Look at the **"Quick Start"** panel
3. Find the **"Scene Selection"** section
4. Click one of:
   - `Empty Room` - Basic test scene
   - `Cornell Box` - Classic GI validation ⭐
   - `Simplified Sponza` - Large environment

The active scene will show `[ACTIVE]` next to its name.

### Method 2: Code Modification

In [`src/demo3d.cpp`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp) constructor (around line 185):

```cpp
// Change this number to switch default scene:
setScene(0); // Empty room (default)
setScene(1); // Cornell Box
setScene(2); // Simplified Sponza
```

Then rebuild and run.

---

## Cornell Box Details

### Scene Configuration

When you select **Scene 1 (Cornell Box)**, the following primitives are created:

```cpp
// From analytic_sdf.cpp::createCornellBox()

// Back wall (white)
addBox(center=(0.5, 0.5, 0), size=(1.0, 1.0, 0.05), color=white)

// Left wall (RED) ← Key for color bleeding test!
addBox(center=(0, 0.5, 0.5), size=(0.05, 1.0, 1.0), color=red)

// Right wall (GREEN) ← Key for color bleeding test!
addBox(center=(1.0, 0.5, 0.5), size=(0.05, 1.0, 1.0), color=green)

// Floor (white)
addBox(center=(0.5, 0, 0.5), size=(1.0, 0.05, 1.0), color=white)

// Ceiling (white)
addBox(center=(0.5, 1.0, 0.5), size=(1.0, 0.05, 1.0), color=white)

// Tall box (center-left, white)
addBox(center=(0.35, 0.2, 0.5), size=(0.3, 0.4, 0.3), color=white)

// Short box (center-right, white)
addBox(center=(0.65, 0.1, 0.4), size=(0.3, 0.2, 0.3), color=white)
```

**Total:** 7 primitives uploaded to GPU via SSBO

### Lighting Setup

The Cornell Box uses **3 point lights** configured in [`injectDirectLighting()`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp#L876-L920):

1. **Ceiling Light** (main)
   - Position: (0.0, 1.8, 0.0)
   - Color: White (1.0, 1.0, 1.0)
   - Intensity: 1.0

2. **Fill Light** (subtle)
   - Position: (-1.5, 1.0, 0.0)
   - Color: Cool white (0.8, 0.8, 0.9)
   - Intensity: 0.3

3. **Accent Light** (warm)
   - Position: (1.5, 0.8, 0.0)
   - Color: Warm white (1.0, 0.9, 0.7)
   - Intensity: 0.4

Plus ambient lighting term (0.05 intensity).

---

## What to Expect Visually

### With Analytic SDF Only (Current State)

When you load the Cornell Box:

1. **SDF Debug View** (Press 'D'):
   - You'll see cross-sections of the box geometry
   - Sharp transitions at walls (black→white)
   - Two rectangular boxes visible in center

2. **Main View**:
   - Currently shows empty space (raymarching not fully implemented)
   - Console confirms "Loading: Cornell Box (analytic SDF)"
   - 7 primitives uploaded to GPU

### After Phase 2 Implementation (Future)

Once cascade update algorithm is complete:

1. **Color Bleeding Visible**:
   - Red tint on floor near left wall
   - Green tint on floor near right wall
   - Soft color mixing in corners

2. **Soft Shadows**:
   - Boxes cast diffuse shadows
   - Multiple light sources create overlapping shadow regions

3. **Indirect Illumination**:
   - Areas not directly lit still receive bounced light
   - More realistic, natural-looking lighting

---

## Troubleshooting

### Problem: Scene doesn't change when clicking button

**Solution:** Check console output for:
```
[Demo3D] Loading: Cornell Box (analytic SDF)
[Demo3D] Uploaded 7 primitives to GPU (336 bytes)
```

If missing, check:
1. Shader loaded successfully: `sdf_analytic.comp`
2. No "[ERROR] Analytic SDF shader not loaded!" message

### Problem: Don't see anything different between scenes

**Explanation:** The main rendering pipeline (raymarching + cascade sampling) is not yet connected. Currently you can only verify scene changes through:
- Console messages
- SDF Debug View (Press 'D')
- Different primitive counts in upload messages

### Problem: Want to add custom scenes

**Solution:** Add new case in [`setScene()`](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp#L1047-L1130):

```cpp
case 3: {
    std::cout << "[Demo3D] Loading: Custom Scene" << std::endl;
    
    // Add your primitives here
    analyticSDF.addBox(...);
    analyticSDF.addSphere(...);
    
    break;
}
```

Then add corresponding button in [renderTutorialPanel()](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.h#L329-L329).

---

## Technical Implementation Details

### Primitive Storage

Primitives are stored in an **SSBO** (Shader Storage Buffer Object):

```cpp
struct GPUPrimitive {
    int type;          // 0=box, 1=sphere
    float position[3]; // Center position
    float scale[3];    // Half-extents (box) or radius (sphere)
    float color[3];    // RGB albedo
    float padding;     // Alignment to 48 bytes
};
```

**Memory Layout:**
- Each primitive: 48 bytes (std430 alignment)
- Cornell Box: 7 primitives × 48 bytes = 336 bytes
- Binding point: 3 (in inject_radiance.comp)

### Material Lookup

The shader dynamically finds the nearest primitive:

```glsl
vec3 sampleAlbedo(vec3 worldPos) {
    float minDist = 1e10;
    vec3 albedo = vec3(0.8); // Default white
    
    for (int i = 0; i < uPrimitiveCount; ++i) {
        float dist = evaluatePrimitive(primitives[i], worldPos);
        if (dist < minDist) {
            minDist = dist;
            albedo = primitives[i].color;
        }
    }
    
    return albedo;
}
```

This enables **arbitrary scene materials** without hardcoded positions!

---

## Next Steps

Now that you have the Cornell Box loaded:

### Immediate (Phase 1.5 Complete)
✅ Scene selection GUI buttons  
✅ Analytic SDF generation  
✅ Multi-light injection  
✅ Dynamic material lookup  

### Next Priority (Phase 2A)
⬜ Implement cascade update algorithm  
⬜ Connect raymarching to cascade data  
⬜ Validate color bleeding visually  

### Future Enhancements
⬜ Load OBJ/PLY mesh files  
⬜ Implement JFA-based SDF generation  
⬜ Add texture mapping support  
⬜ Create more test scenes (Sponza full, Stanford Dragon, etc.)

---

## References

- [Analytic SDF Implementation](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\analytic_sdf.cpp)
- [Scene Management Code](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\src\demo3d.cpp#L1047-L1130)
- [Material System Enhancement](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\doc\2\phase1.5\MATERIAL_SYSTEM_ENHANCEMENT.md)
- [Phase 0 Progress](file://c:\Git-repo-3rd\Radiance_Cascade_repos\radiance-cascades-demo\3d\doc\2\phase0\phase0_progress.md)

---

*Enjoy testing with the Cornell Box! This is the gold standard for validating global illumination algorithms.* 🎨✨
