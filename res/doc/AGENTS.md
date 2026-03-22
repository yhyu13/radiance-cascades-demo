# Shader Documentation - Radiance Cascades Demo

**Generated**: 2026-03-22  
**Location**: `res/shaders/`  
**Purpose**: Complete documentation of all GLSL shaders used in the 2D Radiance Cascades implementation

---

## 📚 Overview

The radiance cascades rendering pipeline consists of **11 shaders** that work together to produce real-time 2D soft shadows and global illumination effects. The shaders are organized into distinct stages:

### Pipeline Stages

```
┌─────────────────────────────────────────────────────────────────────┐
│                        RENDERING PIPELINE                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  STAGE 1: Scene Preparation                                         │
│  ┌──────────────┐     ┌──────────────┐                             │
│  │ prepscene    │────▶│   emission   │ (occlusion + emission maps) │
│  │   .frag      │     │     map      │                             │
│  └──────────────┘     └──────────────┘                             │
│                                                                     │
│  STAGE 2: Distance Field Generation (JFA)                          │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐       │
│  │  prepjfa     │────▶│     jfa      │────▶│  distfield   │       │
│  │   .frag      │     │   .frag      │     │   .frag      │       │
│  └──────────────┘     └──────────────┘     └──────────────┘       │
│         │                   │                      │               │
│         │              (5-7 passes)                │               │
│         ▼                                          ▼               │
│  ┌──────────────────────────────────────────────────────┐         │
│  │          DISTANCE FIELD BUFFER                       │         │
│  └──────────────────────────────────────────────────────┘         │
│                                                                     │
│  STAGE 3: Lighting Calculation                                      │
│  ┌────────────────────────────────────────────────────────┐        │
│  │                                                        │        │
│  │  ┌──────────────┐      ┌──────────────┐              │        │
│  │  │     gi.frag  │◀────▶│    rc.frag   │ (select one) │        │
│  │  │  (GI mode)   │      │ (RC mode)    │              │        │
│  │  └──────────────┘      └──────────────┘              │        │
│  │           │                      │                    │        │
│  │           └──────────┬───────────┘                    │        │
│  │                      ▼                                │        │
│  │            ┌──────────────────┐                      │        │
│  │            │ LIGHTING BUFFER  │                      │        │
│  │            └──────────────────┘                      │        │
│  └────────────────────────────────────────────────────────┘        │
│                                                                     │
│  STAGE 4: User Input & Composition                                 │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐       │
│  │   draw.frag  │────▶│  composite   │────▶│   final.frag │       │
│  │   (macOS)    │     │   (CPU)      │     │   .frag      │       │
│  └──────────────┘     └──────────────┘     └──────────────┘       │
│                                                                     │
│  DEBUG: broken.frag (checkerboard test pattern)                    │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Shader Categories

| Category | Shaders | Purpose |
|----------|---------|---------|
| **Vertex** | [`default.vert`](#defaultvert) | Basic transformation |
| **Scene Prep** | [`prepscene.frag`](#prep scenefrag), [`draw.frag`](#drawfrag), [`draw_macos.frag`](#draw_macosfrag) | Prepare scene geometry |
| **Distance Field** | [`prepjfa.frag`](#prepjf frag), [`jfa.frag`](#jf frag), [`distfield.frag`](#distfieldfrag) | Generate distance field |
| **Lighting** | [`gi.frag`](#gifrag), [`rc.frag`](#rcfrag) | Calculate illumination |
| **Output** | [`final.frag`](#finalfrag), [`broken.frag`](#brokenfrag) | Final composition & debug |

---

## 🔷 Vertex Shaders

### default.vert

**File**: `res/shaders/default.vert`

#### Purpose
Standard vertex shader that transforms 3D vertex positions through the model-view-projection matrix.

#### Code Location
Used by: All fragment shaders as the default vertex shader in Raylib

#### Inputs
```glsl
in vec3 vertexPosition;  // 3D vertex position
uniform mat4 mvp;        // Model-View-Projection matrix
```

#### Outputs
```glsl
gl_Position;  // Transformed clip-space position
```

#### Diagram
```
┌──────────────────────────────────────┐
│         VERTEX SHADER                │
│                                      │
│  vertexPosition (vec3)               │
│       │                              │
│       ▼                              │
│  ┌─────────────┐                     │
│  │ vec4(x,y,z, │                     │
│  │      1.0)   │                     │
│  └──────┬──────┘                     │
│         │                            │
│         ▼                            │
│  ┌─────────────┐                     │
│  │     mvp     │                     │
│  │    × vec4   │                     │
│  └──────┬──────┘                     │
│         │                            │
│         ▼                            │
│  gl_Position                         │
│  (clip space)                        │
└──────────────────────────────────────┘
```

#### Technical Details
- **GLSL Version**: 330 core
- **Instructions**: Single matrix multiplication
- **Use Case**: Generic 2D quad rendering for all post-processing effects

---

## 🔴 Fragment Shaders

### prepscene.frag

**File**: `res/shaders/prepscene.frag`  
**Stage**: Scene Preparation  
**Pass**: 1

#### Purpose
Prepares the scene texture by combining occlusion and emission maps, adding dynamic elements (orbs, mouse light, rainbow animation).

#### Inputs
```glsl
uniform sampler2D uOcclusionMap;    // Occlusion texture (white = solid)
uniform sampler2D uEmissionMap;     // Eission texture (white = emissive)
uniform vec2      uMousePos;        // Current mouse position
uniform float     uTime;            // Time in seconds
uniform int       uOrbs;            // Enable animated orbs (0/1)
uniform int       uRainbow;         // Enable rainbow animation (0/1)
uniform int       uMouseLight;      // Enable mouse light (0/1)
uniform float     uBrushSize;       // Brush size in normalized coordinates
uniform vec4      uBrushColor;      // Brush color (RGBA)
```

#### Outputs
```glsl
fragColor;  // Combined scene color (RGB) + alpha
```

#### Algorithm
1. Sample occlusion and emission maps
2. Normalize alpha channels (white → transparent, black → opaque)
3. Apply HSV→RGB conversion for rainbow animation
4. Render dynamic elements:
   - **Mouse light**: Circular brush at cursor position
   - **Orbs**: 6 colored spheres orbiting center + 2 oscillating bars
   - **Rainbow**: Time-based hue rotation

#### Diagram
```
┌─────────────────────────────────────────────────────────────┐
│                    PREPSCENE.FARG                           │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  uOcclusionMap ──┐                                          │
│                  ├──► [Combine] ──► [Alpha Normalize]      │
│  uEmissionMap ──┘                    │                      │
│                                      │                      │
│  uRainbow == 1? ──► [HSV→RGB] ──────┤                      │
│                   (time-based hue)   │                      │
│                                      │                      │
│  uMouseLight == 1? ──► [Circle SDF] ─┤                      │
│                      at uMousePos    │                      │
│                                      │                      │
│  uOrbs == 1? ──► [6 Orbiting Circles]                      │
│                  [2 Oscillating Bars] ─┘                    │
│                                      │                      │
│                                      ▼                      │
│                                 fragColor                   │
│                                 (scene buffer)              │
└─────────────────────────────────────────────────────────────┘
```

#### Dynamic Elements

**Orbs** (when `uOrbs == 1`):
```glsl
// 6 colored orbs orbiting center
for (int i = 0; i < 6; i++) {
  p = (vec2(cos(uTime/ORB_SPEED+i), sin(uTime/ORB_SPEED+i)) 
       * resolution.y/2 + 1) / 2 + CENTRE;
  if (sdfCircle(p, resolution.x/80))
    fragColor = vec4(hsv2rgb(vec3(i/6.0, 1.0, 1.0)), 1.0);
}

// Horizontal oscillating bar
p = vec2(cos(uTime/ORB_SPEED*4) * resolution.x/4, 0) + CENTRE;
if (sdfCircle(p, ORB_SIZE))
  fragColor = vec4(vec3(sin(uTime) + 1 / 2), 1.0);

// Vertical oscillating bar
p = vec2(0, sin(uTime/ORB_SPEED*4) * resolution.x/4) + CENTRE;
if (sdfCircle(p, ORB_SIZE))
  fragColor = vec4(vec3(cos(uTime) + 1 / 2), 1.0);
```

**Mouse Light** (when `uMouseLight == 1`):
```glsl
if (sdfCircle(uMousePos, uBrushSize*64))
  fragColor = uBrushColor;
```

#### Color Space Functions
```glsl
// RGB ↔ HSV conversion utilities
vec3 rgb2hsv(vec3 c);
vec3 hsv2rgb(vec3 c);
```

#### Usage in demo.cpp
```cpp
const Shader& scenePrepShader = shaders["prepscene.frag"];
// Set uniforms
SetShaderValue(scenePrepShader, GetShaderLocation(scenePrepShader, "uMousePos"), ...);
SetShaderValue(scenePrepShader, GetShaderLocation(scenePrepShader, "uTime"), ...);
```

---

### prepjfa.frag

**File**: `res/shaders/prepjfa.frag`  
**Stage**: Distance Field Preparation  
**Pass**: 1 (before JFA)

#### Purpose
Encodes texture coordinates into empty pixels to prepare for the Jump-Flood Algorithm. Converts the scene mask into a seed texture.

#### Inputs
```glsl
uniform sampler2D uSceneMap;  // Scene texture from prepscene.frag
```

#### Outputs
```glsl
fragColor;  // RGBA where:
            //   - Alpha == 1.0: encodes UV in RG channels
            //   - Alpha == 0.0: empty pixel (no data)
```

#### Algorithm
```
For each pixel:
  1. Sample uSceneMap
  2. If alpha == 1.0 (solid object):
     - Encode fragment UV coordinates into RG channels
     - Output: vec4(U, V, 0.0, 1.0)
  3. If alpha == 0.0 (empty space):
     - Keep as vec4(0.0)
```

#### Diagram
```
┌─────────────────────────────────────────────────────────┐
│                   PREPJFA.FARG                          │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  uSceneMap                                              │
│     │                                                   │
│     ▼                                                   │
│  ┌─────────────┐                                        │
│  │ Sample pixel│                                        │
│  │ at fragCoord│                                        │
│  └──────┬──────┘                                        │
│         │                                               │
│         ├──────────────────┐                           │
│         │                  │                           │
│         ▼                  ▼                           │
│    alpha == 1.0?      alpha == 0.0?                    │
│         │                  │                           │
│         │ YES              │ NO                        │
│         │                  │                           │
│         ▼                  ▼                           │
│  ┌─────────────┐    ┌─────────────┐                   │
│  │ Encode UV   │    │ Keep zero   │                   │
│  │ RG = UV     │    │ vec4(0.0)   │                   │
│  │ A = 1.0     │    │             │                   │
│  └──────┬──────┘    └──────┬──────┘                   │
│         │                  │                           │
│         └──────────┬───────┘                           │
│                    │                                    │
│                    ▼                                    │
│               fragColor                                 │
│               (seed texture)                            │
└─────────────────────────────────────────────────────────┘
```

#### Why This Matters
The JFA algorithm needs **seed points** to propagate. This shader marks which pixels contain geometry by encoding their UV coordinates. The JFA pass then spreads these seeds to fill empty space with nearest-neighbor information.

#### Code Example
```glsl
void main() {
  vec2 fragCoord = gl_FragCoord.xy / textureSize(uSceneMap, 0);
  vec4 mask = texture(uSceneMap, fragCoord);
  
  if (mask.a == 1.0)
    mask = vec4(fragCoord, 0.0, 1.0);  // Encode UV
  
  fragColor = mask;
}
```

---

### jfa.frag

**File**: `res/shaders/jfa.frag`  
**Stage**: Distance Field Generation  
**Passes**: 5-7 (depends on `jfaSteps`)

#### Purpose
Implements the **Jump-Flood Algorithm (JFA)** to generate a distance field from seed points. Each pixel finds its nearest seed and stores the distance.

#### Inputs
```glsl
uniform sampler2D uCanvas;     // Seed texture from prepjfa.frag
uniform int       uJumpSize;   // Current jump offset (decreases each pass)
```

#### Outputs
```glsl
fragColor;  // RGBA where:
            //   - RG: UV coordinates of nearest seed
            //   - B: Distance to nearest seed
            //   - A: 1.0 (valid data)
```

#### Algorithm
```
For each pixel at (x, y):
  1. Gather 8 neighboring seeds at offsets:
     neighbors = [(x±jump, y±jump), (x±jump, y), (x, y±jump)]
  2. For each neighbor:
     - Skip if outside texture bounds
     - Skip if no seed data (alpha == 0)
     - Calculate Euclidean distance to seed
  3. Store closest seed's UV and distance
```

#### Diagram
```
┌─────────────────────────────────────────────────────────────┐
│                      JFA.FARG                               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  PASS 1: jump = N/2 (e.g., 256)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  ●───●───●  ◄── Sample 8 neighbors                 │    │
│  │  │ \ │ / │      at ±jump offset                   │    │
│  │  ●───✦───●  ◄── Current pixel                     │    │
│  │  │ / │ \ │                                        │    │
│  │  ●───●───●                                        │    │
│  └────────────────────────────────────────────────────┘    │
│                                                             │
│  PASS 2: jump = N/4 (e.g., 128)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  ●─●─●─●─●  ◄── Finer sampling                    │    │
│  │                                                    │    │
│  └────────────────────────────────────────────────────┘    │
│                                                             │
│  ... continue until jump = 1                                │
│                                                             │
│  OUTPUT:                                                    │
│  ┌─────────────┐                                           │
│  │ RG = UV of  │                                           │
│  │ nearest     │                                           │
│  │ seed        │                                           │
│  │ B  = Distance│                                          │
│  │ A  = 1.0    │                                           │
│  └─────────────┘                                           │
└─────────────────────────────────────────────────────────────┘
```

#### JFA Pass Sequence
```
Initial:  Seeds encoded in prepjfa.frag
Pass 1:   jump = 256  (coarse propagation)
Pass 2:   jump = 128
Pass 3:   jump = 64
Pass 4:   jump = 32
Pass 5:   jump = 16
Pass 6:   jump = 8
Pass 7:   jump = 4
Pass 8:   jump = 2
Pass 9:   jump = 1    (final refinement)

Result: Every pixel knows its nearest surface
```

#### Mathematical Core
```glsl
float closest = 1.0;
for (int Nx = -1; Nx <= 1; Nx++) {
  for (int Ny = -1; Ny <= 1; Ny++) {
    vec2 NTexCoord = fragCoord + (vec2(Nx, Ny) / resolution) * uJumpSize;
    vec4 Nsample = texture(uCanvas, NTexCoord);
    
    if (NTexCoord != clamp(NTexCoord, 0.0, 1.0)) continue;  // Bounds check
    if (Nsample.a == 0) continue;                            // Skip empty
    
    float d = length((Nsample.rg - fragCoord) * aspectRatio);
    if (d < closest) {
      closest = d;
      fragColor = vec4(Nsample.rg, d, 1.0);
    }
  }
}
```

#### Why JFA?
- **O(log n)** complexity vs O(n²) for brute-force
- **Parallel-friendly**: Each pixel independent
- **GPU-accelerated**: Single pass per jump size
- **Exact result**: Guarantees correct nearest neighbor

---

### distfield.frag

**File**: `res/shaders/distfield.frag`  
**Stage**: Distance Field Extraction  
**Pass**: 1 (after JFA completes)

#### Purpose
Extracts only the distance channel from the JFA output to create a compact distance field texture for the lighting shaders.

#### Inputs
```glsl
uniform sampler2D uJFA;  // JFA output (RG = UV, B = distance)
```

#### Outputs
```glsl
fragColor;  // vec4(distance, distance, distance, 1.0)
            // Replicated in all RGB channels for convenience
```

#### Algorithm
```
For each pixel:
  1. Sample JFA texture
  2. Extract blue channel (distance value)
  3. Replicate to RGB channels
  4. Output grayscale distance field
```

#### Diagram
```
┌─────────────────────────────────────────────────────────┐
│                  DISTFIELD.FARG                         │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  uJFA Texture                                           │
│  ┌───────────────────────────────┐                     │
│  │  RG = Nearest Seed UV         │                     │
│  │  B  = Distance to Surface     │◄── Read            │
│  │  A  = 1.0                     │                     │
│  └───────────────┬───────────────┘                     │
│                  │                                      │
│                  ▼                                      │
│         ┌────────────────┐                             │
│         │ Extract Blue   │                             │
│         │ Channel (B)    │                             │
│         └───────┬────────┘                             │
│                 │                                      │
│                 ▼                                      │
│         ┌────────────────┐                             │
│         │ Replicate to   │                             │
│         │ RGB            │                             │
│         └───────┬────────┘                             │
│                 │                                      │
│                 ▼                                      │
│         fragColor = vec4(dist, dist, dist, 1.0)        │
│                                                         │
│  Result: Grayscale distance field                      │
│  - Black = near surface                                │
│  - White = far from surface                            │
└─────────────────────────────────────────────────────────┘
```

#### Why Extract Distance Only?
1. **Bandwidth reduction**: RC/GI shaders only need distance, not UV
2. **Cache efficiency**: Smaller data footprint
3. **Simplified sampling**: No need to swizzle channels in lighting shaders

#### Usage
```cpp
// In demo.cpp, bind distance field to lighting shaders
SetShaderValue(rcShader, GetShaderLocation(rcShader, "uDistanceField"), ...);
```

---

### gi.frag

**File**: `res/shaders/gi.frag`  
**Stage**: Global Illumination (Alternative to RC)  
**Mode**: Traditional raymarching GI

#### Purpose
Performs traditional radiosity-based global illumination using uniform ray distribution. Simulates light bouncing off surfaces.

#### Inputs
```glsl
uniform sampler2D uDistanceField;   // From distfield.frag
uniform sampler2D uSceneMap;        // Original scene geometry
uniform sampler2D uLastFrame;       // Previous frame for accumulation
uniform int       uRayCount;        // Number of rays (e.g., 64)
uniform int       uSrgb;            // sRGB conversion flag
uniform int       uNoise;           // Per-pixel noise flag
uniform float     uPropagationRate; // Light decay factor
uniform float     uMixFactor;       // Direct/indirect mix
uniform int       uAmbient;         // Ambient light flag
uniform vec3      uAmbientColor;    // Ambient color
```

#### Outputs
```glsl
fragColor;  // Final illuminated color (RGB)
```

#### Algorithm
```
For each pixel:
  1. If inside wall (dist < EPS): skip
  2. Cast uRayCount rays in uniform angular distribution
  3. For each ray:
     - Raymarch through distance field
     - On surface hit: sample scene color + last frame
     - Accumulate indirect illumination
  4. Average all ray results
  5. Add ambient term
  6. Convert to sRGB if needed
```

#### Diagram
```
┌─────────────────────────────────────────────────────────────┐
│                      GI.FARG                                │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Pixel at fragCoord                                         │
│         │                                                   │
│         ▼                                                   │
│  ┌─────────────────┐                                        │
│  │ Check distance  │─── dist >= EPS? ──NO──► Output scene  │
│  │ from DF         │                                        │
│         │ YES                                             │
│         ▼                                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │           CAST RAYS (360° uniform)                  │   │
│  │                                                     │   │
│  │         ╱│╲                                         │   │
│  │        ╱ │ ╲                                        │   │
│  │       ╱  │  ╲                                       │   │
│  │      ◄───✦───►  ◄── uRayCount rays                  │   │
│  │       ╲  │  ╱       equal angular spacing           │   │
│  │        ╲ │ ╱                                        │   │
│  │         ╲│╱                                         │   │
│  └─────────────────────────────────────────────────────┘   │
│         │                                                   │
│         ▼                                                   │
│  ┌─────────────────┐                                        │
│  │ For each ray:   │                                        │
│  │ - Raymarch DF   │                                        │
│  │ - Hit surface?  │───YES──► Sample color                 │
│  │                 │                                        │
│  │                 │◄── Mix with uLastFrame                │
│  │                 │    (temporal accumulation)            │
│  └────────┬────────┘                                        │
│           │                                                 │
│           ▼                                                 │
│  ┌─────────────────┐                                        │
│  │ Sum all rays    │                                        │
│  │ Divide by count │                                        │
│  │ Add ambient     │                                        │
│  │ sRGB convert?   │───YES──► lin_to_srgb()                │
│  └────────┬────────┘                                        │
│           │                                                 │
│           ▼                                                 │
│      fragColor                                              │
└─────────────────────────────────────────────────────────────┘
```

#### Raymarching Function
```glsl
vec4 raymarch(vec2 uv, vec2 dir) {
  for (int i = 0; i < MAX_RAY_STEPS; i++) {
    float dist = texture(uDistanceField, uv).r;
    uv += dir * dist;
    
    if (uv outside bounds) break;
    
    if (dist < EPS) {  // Surface hit
      return vec4(
        mix(
          texture(uSceneMap, uv).rgb,
          max(
            texture(uLastFrame, uv).rgb,
            texture(uLastFrame, uv - dir*pixelSize).rgb * uPropagationRate
          ),
          uMixFactor
        ), 1.0);
    }
  }
  return vec4(0.0);  // No hit
}
```

#### Noise Implementation
```glsl
// Adds per-pixel angular offset to reduce banding
float n = 0.0;
if (uNoise == 1) 
  n = noise(fragCoord.xy * 2000);

for (float i = 0.0; i < TWO_PI; i += TWO_PI / uRayCount) {
  float angle = i + n;  // Add noise offset
  vec4 hitcol = raymarch(fragCoord, direction);
  radiance += hitcol;
}
```

#### Color Space Handling
```glsl
// sRGB ↔ Linear conversion
vec3 lin_to_srgb(vec3 rgb);
vec3 srgb_to_lin(vec3 rgb);
```

#### Performance Characteristics
- **Complexity**: O(rayCount × maxSteps) per pixel
- **Quality**: High but expensive
- **Use case**: Reference implementation, small scenes

---

### rc.frag

**File**: `res/shaders/rc.frag`  
**Stage**: Radiance Cascades (Primary lighting method)  
**Passes**: `cascadeAmount` (typically 4-5)

#### Purpose
Implements the **Radiance Cascades** algorithm for efficient high-quality soft shadows. Uses hierarchical probe grids to achieve O(log n) performance.

#### Inputs
```glsl
uniform sampler2D uDistanceField;   // Distance field
uniform sampler2D uSceneMap;        // Scene geometry
uniform sampler2D uDirectLighting;  // Direct illumination buffer
uniform sampler2D uLastPass;        // Previous cascade result
uniform vec2      uResolution;      // Screen resolution
uniform int       uBaseRayCount;    // Rays per cascade (e.g., 4)
uniform int       uCascadeIndex;    // Current cascade level (0 to N)
uniform int       uCascadeAmount;   // Total cascades
uniform int       uCascadeDisplayIndex; // Debug display
uniform float     uBaseInterval;    // Starting interval in pixels
uniform int       uDisableMerging;  // Disable cascade merging
uniform float     uMixFactor;       // Direct/indirect mix
uniform int       uAmbient;         // Ambient light flag
uniform vec3      uAmbientColor;    // Ambient color
```

#### Outputs
```glsl
fragColor;  // Radiance value for current cascade
```

#### Key Concept: Probe Hierarchy
```
CASCADE 0: Finest level
┌───┬───┬───┬───┐
│ ● │ ● │ ● │ ● │  ← 4×4 probes
├───┼───┼───┼───┤      (16 total)
│ ● │ ● │ ● │ ● │
├───┼───┼───┼───┤
│ ● │ ● │ ● │ ● │
├───┼───┼───┼───┤
│ ● │ ● │ ● │ ● │
└───┴───┴───┴───┘

CASCADE 1: Coarser level
┌───────┬───────┐
│   ●   │   ●   │  ← 2×2 probes
├───────┼───────┤      (4 total)
│   ●   │   ●   │
└───────┴───────┘

CASCADE 2: Even coarser
┌───────────────┐
│       ●       │  ← 1×1 probe
└───────────────┘     (1 total)
```

#### Diagram
```
┌─────────────────────────────────────────────────────────────┐
│                    RC.FARG (Single Cascade)                 │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  get_probe_info(uCascadeIndex)                              │
│         │                                                   │
│         ▼                                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Calculate probe properties:                         │   │
│  │ - spacing = baseRayCount^index                      │   │
│  │ - size = 1.0 / spacing                              │   │
│  │ - rayCount = baseRayCount^(index+1)                 │   │
│  │ - intervalStart/End = distance range                │   │
│  └────────────────────┬────────────────────────────────┘   │
│                       │                                     │
│                       ▼                                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  For each ray in baseRayCount:                        │  │
│  │                                                       │  │
│  │    1. Calculate ray angle                            │  │
│  │       angle = (baseIndex + i) / rayCount * TWO_PI    │  │
│  │                                                       │  │
│  │    2. Raymarch in interval [start, end]              │  │
│  │       deltaRadiance = radiance_interval(...)         │  │
│  │                                                       │  │
│  │    3. If no hit and not last cascade:                │  │
│  │       Sample from uLastPass (coarser cascade)        │  │
│  │       (MERGING STEP)                                 │  │
│  │                                                       │  │
│  │    4. Accumulate radiance                            │  │
│  └────────────────────┬──────────────────────────────────┘  │
│                       │                                     │
│                       ▼                                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Average accumulated radiance                        │   │
│  │  Add ambient term                                    │   │
│  │  Convert to sRGB (first cascade only)                │   │
│  └────────────────────┬─────────────────────────────────┘   │
│                       │                                     │
│                       ▼                                     │
│                  fragColor                                  │
└─────────────────────────────────────────────────────────────┘
```

#### Interval Raymarching
```glsl
vec4 radiance_interval(vec2 uv, vec2 dir, float a, float b) {
  uv += a * dir;  // Start at interval begin
  float travelledDist = a;
  
  for (int i = 0; i < MAX_RAY_STEPS; i++) {
    float dist = texture(uDistanceField, uv).r;
    uv += dir * dist;
    
    if (outside bounds) break;
    
    if (dist < EPS) {  // Surface hit
      return mix(sceneColor, directLighting, uMixFactor);
    }
    
    travelledDist += dist;
    if (travelledDist >= b) break;  // Reached interval end
  }
  return vec4(0.0);  // No hit in interval
}
```

#### Cascade Merging
```glsl
// If current cascade finds no surface, borrow from coarser cascade
if (deltaRadiance.a == 0.0 && uDisableMerging != 1.0) {
  // Calculate corresponding position in next cascade
  up.position = vec2(
    mod(index, up.spacing), 
    floor(index / up.spacing)
  ) * up.size;
  
  // Sample previous cascade
  deltaRadiance += texture(uLastPass, uv);
}
```

#### Why Radiance Cascades?
1. **Performance**: O(cascadeAmount × baseRayCount) vs O(totalRays)
2. **Quality**: Hierarchical approach captures both fine and coarse details
3. **Memory**: Reuses previous frame via temporal accumulation
4. **Scalability**: Adjust cascadeAmount for quality/speed tradeoff

#### Typical Configuration
```cpp
// From demo.cpp
rcRayCount = 4;        // Base rays per cascade
cascadeAmount = 5;     // 5 cascades
// Total effective rays: 4^5 = 1024 equivalent
// Actual cost: 5 cascades × 4 rays = 20 raymarch operations
```

---

### draw.frag

**File**: `res/shaders/draw.frag`  
**Stage**: User Input (Drawing)  
**Platform**: Windows/Linux

#### Purpose
Renders user brush strokes when mouse is pressed. Creates smooth lines by interpolating between mouse positions.

#### Inputs
```glsl
uniform vec2      uMousePos;       // Current mouse position
uniform vec2      uLastMousePos;   // Previous mouse position
uniform int       uMouseDown;      // Mouse button state (0/1)
uniform float     uBrushSize;      // Normalized brush size
uniform vec4      uBrushColor;     // Brush color (RGBA)
uniform float     uTime;           // Time in seconds
uniform int       uRainbow;        // Rainbow mode flag (0/1)
```

#### Outputs
```glsl
fragColor;  // Brush color if inside stroke, otherwise unchanged
```

#### Algorithm
```
If mouse is pressed:
  1. Interpolate 16 points between lastMousePos and mousePos
  2. For each interpolated point:
     - Test if current fragment is inside circle (SDF)
     - If yes, set fragColor to brushColor
  3. Rainbow mode: animate hue over time
```

#### Diagram
```
┌─────────────────────────────────────────────────────────┐
│                    DRAW.FARG                            │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  uMouseDown == 0? ──YES──► Early exit (return)         │
│         │ NO                                            │
│         ▼                                               │
│  ┌──────────────────────────────────────────────────┐   │
│  │  LERP_AMOUNT = 16                                │   │
│  │                                                  │   │
│  │  for i = 0 to 15:                                │   │
│  │    interpPos = mix(uLastMousePos, uMousePos,     │   │
│  │                    i/16.0)                       │   │
│  │                                                  │   │
│  │    if sdfCircle(interpPos, uBrushSize*64):      │   │
│  │      if uRainbow == 1:                           │   │
│  │        color = hsv2rgb(vec3(uTime, 1, 1))       │   │
│  │      else:                                       │   │
│  │        color = uBrushColor                      │   │
│  │      fragColor = color                          │   │
│  └──────────────────────────────────────────────────┘   │
│                                                         │
│  Result: Smooth line connecting last and current pos   │
└─────────────────────────────────────────────────────────┘
```

#### Line Smoothing
```glsl
#define LERP_AMOUNT 16.0
for (float i = 0; i < LERP_AMOUNT; i++) {
  if (sdfCircle(mix(uMousePos, uLastMousePos, i/LERP_AMOUNT), 
                uBrushSize*64))
    sdf = true;
}
```

**Why 16 samples?** Prevents gaps when moving mouse quickly.

#### Rainbow Mode
```glsl
if (uRainbow == 1)
  fragColor = hsv2rgb(vec3(uTime, 1.0, 1.0));  // Animated hue
else
  fragColor = uBrushColor;
```

---

### draw_macos.frag

**File**: `res/shaders/draw_macos.frag`  
**Stage**: User Input (Drawing)  
**Platform**: macOS

#### Purpose
macOS variant of draw.frag that preserves the canvas texture when not drawing.

#### Differences from draw.frag
- **Additional input**: `uCanvas` texture
- **Behavior**: Samples canvas when not drawing (transparency handling)
- **Reason**: macOS OpenGL context differences require explicit texture sampling

#### Code Difference
```glsl
// draw.frag (Windows/Linux)
void main() {
  if (uMouseDown == 0) return;  // Early exit
  // ... draw brush ...
}

// draw_macos.frag (macOS)
void main() {
  if (uMouseDown == 0) {
    fragColor = texture(uCanvas, uv);  // Preserve canvas
    return;
  }
  // ... draw brush ...
  else
    fragColor = texture(uCanvas, uv);  // Also preserve when not drawing
}
```

---

### final.frag

**File**: `res/shaders/final.frag`  
**Stage**: Final Composition  
**Pass**: 1 (last shader in pipeline)

#### Purpose
Simple pass-through shader that outputs the final rendered canvas to screen.

#### Inputs
```glsl
uniform vec2      uResolution;  // Screen resolution
uniform sampler2D uCanvas;      // Final composited texture
```

#### Outputs
```glsl
fragColor;  // Final screen color
```

#### Algorithm
```
Sample uCanvas at current fragment coordinate
Output sampled color
```

#### Diagram
```
┌─────────────────────────────────────────────────────────┐
│                   FINAL.FARG                            │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  uCanvas                                                │
│     │                                                   │
│     ▼                                                   │
│  ┌─────────────┐                                        │
│  │ Sample at   │                                        │
│  │ fragCoord   │                                        │
│  └──────┬──────┘                                        │
│         │                                               │
│         ▼                                               │
│    fragColor                                            │
│    (display output)                                     │
└─────────────────────────────────────────────────────────┘
```

#### Code
```glsl
void main() {
  fragColor = texture(uCanvas, gl_FragCoord.xy / textureSize(uCanvas, 0));
}
```

---

### broken.frag

**File**: `res/shaders/broken.frag`  
**Stage**: Debug  
**Purpose**: Checkerboard test pattern

#### Purpose
Debug shader for verifying rendering pipeline. Not used in normal operation.

#### Pattern
```glsl
#define N       25
#define PRIMARY   vec4(1.0f, 0.0f, 1.0f, 1.0f)   // Magenta
#define SECONDARY vec4(0.0f, 0.0f, 0.0f, 1.0f)   // Black

void main() {
  vec2 pos = mod(gl_FragCoord.xy, vec2(N));
  
  if ((pos.x > N/2 && pos.y > N/2) ||
      (pos.x < N/2 && pos.y < N/2))
    fragColor = PRIMARY;    // Magenta squares
  else
    fragColor = SECONDARY;  // Black squares
}
```

#### Diagram
```
┌─────────────────────────────────────────────────────────┐
│                  BROKEN.FARG                            │
│                                                         │
│  Checkerboard pattern (25px squares)                   │
│                                                         │
│  ┌───────┬───────┬───────┐                             │
│  │ ████  │ ░░░░  │ ████  │  ████ = Magenta            │
│  │ ████  │ ░░░░  │ ████  │  ░░░░ = Black              │
│  ├───────┼───────┼───────┤                             │
│  │ ░░░░  │ ████  │ ░░░░  │                             │
│  │ ░░░░  │ ████  │ ░░░░  │                             │
│  ├───────┼───────┼───────┤                             │
│  │ ████  │ ░░░░  │ ████  │                             │
│  └───────┴───────┴───────┘                             │
└─────────────────────────────────────────────────────────┘
```

#### Usage
- **Debugging**: Verify viewport is rendering correctly
- **Alignment**: Check texture coordinate mapping
- **Skipped**: Automatically excluded from loading in demo.cpp

---

## 🔄 Complete Rendering Pipeline

### Frame-by-Frame Execution

```
┌─────────────────────────────────────────────────────────────────┐
│                         FRAME START                             │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ STEP 1: Process Input                                           │
│ - Keyboard (demo.cpp::processKeyboardInput)                    │
│ - Mouse (demo.cpp::processMouseInput)                          │
│ - ImGui UI (demo.cpp::renderUI)                                │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ STEP 2: Update Scene (if changed)                              │
│ - Upload occlusion/emission maps                               │
│ - Set shader uniforms                                          │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ STEP 3: Render to Display Buffer                               │
│ ┌─────────────────────────────────────────────────────────┐    │
│ │ A. GI MODE                                              │    │
│ │    ┌──────────────┐                                     │    │
│ │    │ gi.frag      │──► Single pass, full raymarch       │    │
│ │    └──────────────┘                                     │    │
│ │                                                         │    │
│ │ B. RC MODE (Radiance Cascades)                          │    │
│ │    CASCADE 0: rc.frag (finest)                          │    │
│ │         │                                               │    │
│ │         ▼                                               │    │
│ │    CASCADE 1: rc.frag                                   │    │
│ │         │                                               │    │
│ │         ▼                                               │    │
│ │    ...                                                  │    │
│ │         │                                               │    │
│ │         ▼                                               │    │
│ │    CASCADE N: rc.frag (coarsest)                        │    │
│ └─────────────────────────────────────────────────────────┘    │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ STEP 4: Composite                                               │
│ - Draw display buffer to screen                                 │
│ - Overlay ImGui UI                                              │
│ - Apply cursor texture                                          │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│ STEP 5: Present                                                 │
│ - Swap buffers                                                  │
│ - Wait for vsync                                                │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
                    FRAME END
```

### Resource Flow

```
┌──────────────────────────────────────────────────────────────────┐
│                    TEXTURE FLOW                                  │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  USER INPUT                                                       │
│  ┌─────────────┐                                                 │
│  │ Mouse/Keys  │                                                 │
│  └──────┬──────┘                                                 │
│         │                                                        │
│         ▼                                                        │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ OCCLUSION MAP           │ EMISSION MAP                     │ │
│  │ (white = solid)         │ (white = light source)           │ │
│  └─────────────────────────┬─────────────────────────────────┘ │
│                            │                                    │
│                            ▼                                    │
│                   ┌────────────────┐                           │
│                   │ prepscene.frag │                           │
│                   └────────┬───────┘                           │
│                            │                                    │
│                            ▼                                    │
│                   ┌────────────────┐                           │
│                   │ SCENE BUFFER   │                           │
│                   └────────┬───────┘                           │
│                            │                                    │
│         ┌──────────────────┼──────────────────┐                │
│         │                  │                  │                │
│         ▼                  ▼                  ▼                │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐          │
│  │ prepjfa.frag│   │  draw.frag  │   │  (direct)   │          │
│  └──────┬──────┘   └──────┬──────┘   └──────┬──────┘          │
│         │                 │                  │                 │
│         ▼                 │                  │                 │
│  ┌─────────────┐          │                  │                 │
│  │  jfa.frag   │          │                  │                 │
│  │ (5-7 passes)│          │                  │                 │
│  └──────┬──────┘          │                  │                 │
│         │                 │                  │                 │
│         ▼                 │                  │                 │
│  ┌─────────────┐          │                  │                 │
│  │distfield.fag│          │                  │                 │
│  └──────┬──────┘          │                  │                 │
│         │                 │                  │                 │
│         │                 │                  │                 │
│         └─────────────────┼──────────────────┘                 │
│                           │                                    │
│                           ▼                                    │
│                  ┌────────────────┐                           │
│                  │ RC or GI mode  │                           │
│                  │   rc.frag      │                           │
│                  │   gi.frag      │                           │
│                  └───────┬────────┘                           │
│                          │                                    │
│                          ▼                                    │
│                 ┌─────────────────┐                          │
│                 │ LIGHTING BUFFER │                          │
│                 └────────┬────────┘                          │
│                          │                                    │
│                          ▼                                    │
│                 ┌─────────────────┐                          │
│                 │  final.frag     │                          │
│                 └────────┬────────┘                          │
│                          │                                    │
│                          ▼                                    │
│                 ┌─────────────────┐                          │
│                 │    SCREEN       │                          │
│                 └─────────────────┘                          │
│                                                               │
└──────────────────────────────────────────────────────────────────┘
```

---

## 📊 Performance Comparison

### Shader Complexity

| Shader | Passes | Instructions | GPU Cost |
|--------|--------|--------------|----------|
| default.vert | 1 | ~5 | Negligible |
| prepscene.frag | 1 | ~50 | Low |
| prepjfa.frag | 1 | ~15 | Low |
| jfa.frag | 5-7 | ~40 | Medium (×passes) |
| distfield.frag | 1 | ~5 | Negligible |
| gi.frag | 1 | ~200+ | Very High |
| rc.frag | 4-5 | ~150 | Medium (×cascades) |
| draw.frag | 1 | ~30 | Low |
| final.frag | 1 | ~5 | Negligible |

### Memory Bandwidth

| Buffer | Format | Size (1920×1080) | Purpose |
|--------|--------|------------------|---------|
| Scene | RGBA8 | 8 MB | Geometry |
| JFA | RGBA32F | 32 MB | Distance field seeds |
| Distance Field | R32F | 8 MB | Raymarching |
| Lighting | RGBA8/RGBA16F | 8-16 MB | Final illumination |
| Last Frame | RGBA8 | 8 MB | Temporal accumulation |

**Total VRAM**: ~64-72 MB for full pipeline

---

## 🛠 Debugging Tips

### Common Issues

1. **Black screen**
   - Check if shaders compiled: `GetShaderId()` returns valid ID
   - Verify resource paths relative to executable
   - Inspect `broken.frag` output first

2. **Incorrect distances**
   - Ensure JFA completed all passes
   - Check `distfield.frag` visualization
   - Verify `uResolution` uniform is correct

3. **Artifacts at cascade boundaries**
   - Adjust `uBaseInterval` parameter
   - Increase `cascadeAmount`
   - Check merging logic in `rc.frag`

4. **Performance issues**
   - Reduce `uRayCount` in GI mode
   - Lower `cascadeAmount` in RC mode
   - Decrease `MAX_RAY_STEPS`

### Visualization Techniques

```cpp
// In demo.cpp, you can visualize intermediate buffers:
debugShowBuffers = true;
displayBuffer = &jfaBufferA;    // Show JFA seeds
displayBuffer = &distFieldBuf;  // Show distance field
displayBuffer = &radianceBufferA; // Show cascade result
```

---

## 📖 References

### Academic Papers
- **Radiance Cascades Paper**: Alexander Sannikov - https://github.com/Raikiri/RadianceCascadesPaper
- **Jump Flood Algorithm**: http://www.cs.unc.edu/~dm/UNC/COMP258/Papers/jfa.pdf

### Shader Resources
- **Raylib Shaders**: https://www.raylib.com/examples.html
- **GLSL Reference**: https://www.khronos.org/opengl/wiki/Core_Language_(GLSL)

### Related Articles
- GM Shaders: https://gmshaders.com
- Yaazarai GI: https://mini.gmshaders.com/p/yaazarai-gi
- Jason McGhee: https://jason.today/

---

**End of Shader Documentation**

*Individual shader diagrams are available in `res/doc/` directory as separate files.*
