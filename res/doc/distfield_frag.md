# distfield.frag - Distance Field Extraction Shader Diagram

**Purpose**: Extract distance channel from JFA output to create compact distance field texture

## Pipeline Position

```mermaid
flowchart LR
    subgraph "JFA Pipeline"
        A[prepjfa.frag<br/>Encode seeds] --> B[jfa.frag<br/>Propagate seeds<br/>5-9 passes]
        B --> C[distfield.frag<br/>Extract distance]
        C --> D[rc.frag / gi.frag<br/>Use for lighting]
    end
    
    style A fill:#e8f4ff
    style B fill:#fff4e8
    style C fill:#aaffaa
    style D fill:#ffe8e8
```

## Data Flow Diagram

```mermaid
flowchart TD
    subgraph Input["Input: JFA Output Texture"]
        A["Pixel format:<br/>R = Nearest U coord<br/>G = Nearest V coord<br/>B = Distance value<br/>A = 1.0 (valid)"]
    end
    
    subgraph Processing["distfield.frag Processing"]
        B[Sample JFA texture<br/>at fragCoord]
        C[Extract B channel<br/>distance value]
        D[Replicate to RGB<br/>for visualization]
        E[Set alpha = 1.0]
    end
    
    subgraph Output["Output: Distance Field"]
        F["Pixel format:<br/>R = Distance<br/>G = Distance<br/>B = Distance<br/>A = 1.0"]
    end
    
    A --> B
    B --> C
    C --> D
    D --> E
    E --> F
    
    style A fill:#e8f4ff
    style F fill:#e8ffe8
    style C fill:#ffffaa
```

## Channel Extraction Process

```mermaid
sequenceDiagram
    participant FS as Fragment Shader
    participant T as Texture Sampler (JFA)
    participant O as Output Buffer
    
    FS->>T: Sample at fragCoord
    T-->>FS: vec4(U, V, dist, 1.0)
    
    FS->>FS: Extract blue channel
    Note over FS: float d = sample.b
    
    FS->>FS: Replicate to RGB
    Note over FS: vec3(d, d, d)
    
    FS->>O: Write vec4(d, d, d, 1.0)
    
    Note over O: Grayscale distance field<br/>Black = near surface<br/>White = far from surface
```

## Visualization Example

```
JFA OUTPUT (before distfield.frag):
┌─────────────────────────────────────┐
│ R,G = UV coords (position data)     │
│ B     = Distance (what we need)     │
│ A     = Validity flag               │
│                                     │
│ Pixel values (RGBA):                │
│ [0.2, 0.3, 0.05, 1.0]  ← 5% dist   │
│ [0.5, 0.5, 0.15, 1.0]  ← 15% dist  │
│ [0.8, 0.7, 0.40, 1.0]  ← 40% dist  │
└─────────────────────────────────────┘

After distfield.frag extraction:
┌─────────────────────────────────────┐
│ All channels = Distance             │
│                                     │
│ Pixel values (RGBA):                │
│ [0.05, 0.05, 0.05, 1.0]  ← Dark    │
│ [0.15, 0.15, 0.15, 1.0]  ← Medium  │
│ [0.40, 0.40, 0.40, 1.0]  ← Bright  │
└─────────────────────────────────────┘

Visual representation:
┌─────────────────┐
│ ░░░▒▒▓▓▓█       │  ░ = very close (dark)
│ ░░▒▒▒▓▓▓█       │  ▒ = medium distance
│ ░░▒▒▓▓▓██       │  ▓ = far (bright)
│ ░░░▒▒▓▓▓█       │  █ = furthest
└─────────────────┘
```

## Why Extract Distance Only?

```mermaid
mindmap
  root((Why extract B channel?))
    Performance[GPU Efficiency]
      Smaller data footprint
      Better cache usage
      No channel swizzling needed
    
    Simplicity[Cleaner Code]
      RC/GI shaders read single value
      No need to unpack RG channels
      Grayscale visualization easy
    
    Bandwidth[Memory Savings]
      Lighting shaders only need distance
      RG channels unused in later stages
      Reduces texture fetch overhead
    
    Precision[Distance Accuracy]
      Full 32-bit float for distance
      No precision loss from packing
      Critical for raymarching accuracy
```

## Mathematical Operation

```
For each pixel at location (x, y):

Input from JFA:
  jfa[x, y] = (u_nearest, v_nearest, d, 1.0)

Where:
  u_nearest = U coordinate of nearest surface
  v_nearest = V coordinate of nearest surface
  d         = Euclidean distance to nearest surface

Operation:
  distance_field[x, y] = (d, d, d, 1.0)

Output:
  All RGB channels contain the same distance value
  Alpha set to 1.0 for validity
```

## Uniform Parameters

| Uniform | Type | Description |
|---------|------|-------------|
| `uJFA` | `sampler2D` | JFA output texture containing UV + distance data |

*(Note: `uResolution` is commented out in shader, uses textureSize instead)*

## Code Implementation

```glsl
void main() {
  // Calculate normalized fragment coordinate
  // Using textureSize for automatic resolution detection
  vec2 fragCoord = gl_FragCoord.xy / textureSize(uJFA, 0);
  
  // Sample the JFA texture
  vec4 jfaSample = texture(uJFA, fragCoord);
  
  // Extract distance from blue channel
  float distance = jfaSample.b;
  
  // Replicate to all RGB channels for grayscale output
  fragColor = vec4(vec3(distance), 1.0);
}
```

## Alternative Implementation (Commented Out)

```glsl
// Original version with explicit uniform:
uniform vec2 uResolution;

void main() {
  vec2 fragCoord = gl_FragCoord.xy / uResolution;
  fragColor = vec4(vec3(texture(uJFA, fragCoord).b), 1.0);
}

// Why changed?
// - textureSize() is more flexible
// - No need to manually set uResolution uniform
// - Automatically adapts to texture dimensions
```

## Distance Field Usage

```mermaid
flowchart TB
    A[Distance Field<br/>from distfield.frag] --> B{Lighting Mode?}
    
    B -->|GI Mode| C[gi.frag<br/>Raymarch distance field]
    B -->|RC Mode| D[rc.frag<br/>Interval raymarching]
    
    C --> E[Cast rays using distance<br/>to find surfaces]
    D --> F[March through specific<br/>distance intervals]
    
    E --> G[Accumulate radiance<br/>from all directions]
    F --> H[Hierarchical cascade<br/>lighting calculation]
    
    G --> I[Final illumination buffer]
    H --> I
    
    style A fill:#aaffaa
    style I fill:#ffe8e8
```

## Raymarching Connection

```
Distance Field Value Usage:

In gi.frag or rc.frag:

  for (int i = 0; i < MAX_RAY_STEPS; i++) {
    // Read distance from field
    float dist = texture(uDistanceField, uv).r;
    
    // March ray forward by that distance
    uv += direction * dist;
    
    // Check if hit surface (distance < epsilon)
    if (dist < EPS) {
      // Surface intersection found!
      return surfaceColor;
    }
  }

The distance field tells the raymarcher:
"You can safely travel 'dist' units without hitting anything"
This makes raymarching efficient and accurate!
```

## Visual Quality Comparison

```
Without Distance Field Extraction:
┌─────────────────────────────────────┐
│ RC/GI shaders would need to:        │
│ 1. Sample JFA texture               │
│ 2. Swizzle .b channel               │
│ 3. Ignore RG data                   │
│                                     │
│ Extra instructions per pixel:       │
│ - More texture bandwidth            │
│ - More register pressure            │
│ - Slower performance                │
└─────────────────────────────────────┘

With Distance Field Extraction:
┌─────────────────────────────────────┐
│ RC/GI shaders simply:               │
│ 1. Sample distance field            │
│ 2. Use .r channel directly          │
│                                     │
│ Benefits:                           │
│ ✓ Single texture fetch              │
│ ✓ Clean code                        │
│ ✓ Faster execution                  │
│ ✓ Better GPU utilization            │
└─────────────────────────────────────┘
```

## Performance Impact

```mermaid
barChart-beta
    title "Texture Fetch Comparison"
    x-axis ["Without Extraction", "With Extraction"]
    y-axis "Relative Cost" 0 --> 100
    
    bar [100, 30]
    
    note1["Needs to sample JFA,<br/>swizzle channels"]
    note2["Direct sample,<br/>ready to use"]
    
    note1 -.-> "Without Extraction"
    note2 -.-> "With Extraction"
```

---

**File Location**: `res/shaders/distfield.frag`  
**GLSL Version**: 330 core  
**Execution**: Once per frame (after JFA completes)  
**Output**: Compact grayscale distance field for lighting shaders
