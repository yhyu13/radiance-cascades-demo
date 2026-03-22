# gi.frag - Global Illumination Shader Diagram

**Purpose**: Traditional radiosity-based global illumination using uniform ray distribution

## Complete GI Pipeline

```mermaid
flowchart TD
    subgraph Inputs
        DF[uDistanceField<br/>From distfield.frag]
        SM[uSceneMap<br/>Original geometry]
        LF[uLastFrame<br/>Temporal accumulation]
    end
    
    subgraph Processing["gi.frag Algorithm"]
        A[Sample distance field<br/>at current pixel]
        B{Inside wall?<br/>dist < EPS}
        C[Skip - output scene color]
        D[Cast uRayCount rays<br/>360° uniform distribution]
        E[For each ray:<br/>Raymarch to find surface]
        F{Surface hit?}
        G[Sample scene color<br/>+ last frame accumulation]
        H[Apply propagation decay<br/>uPropagationRate]
        I[Mix direct/indirect<br/>uMixFactor]
        J[Accumulate all rays]
        K[Average: sum / rayCount]
        L[Add ambient term]
        M{sRGB conversion?<br/>uSrgb == 1}
        N[Convert linear→sRGB]
    end
    
    subgraph Output
        O[fragColor<br/>Final illuminated pixel]
    end
    
    DF --> A
    SM --> G
    LF --> G
    
    A --> B
    B -->|Yes| C
    B -->|No| D
    C --> O
    D --> E
    E --> F
    F -->|No| E
    F -->|Yes| G
    G --> H
    H --> I
    I --> J
    J --> K
    K --> L
    L --> M
    M -->|Yes| N
    M -->|No| O
    N --> O
    
    style Inputs fill:#e8f4ff
    style Processing fill:#fff4e8
    style Output fill:#e8ffe8
    style D fill:#ffffaa
```

## Ray Casting Pattern

```mermaid
flowchart LR
    subgraph "360° Uniform Distribution"
        direction TB
        R1["Ray 0<br/>angle = 0°"]
        R2["Ray 1<br/>angle = 360/rayCount"]
        R3["..."]
        R4["Ray N<br/>angle = N×step"]
    end
    
    Center["✦ Pixel"]
    
    R1 --- Center
    R2 --- Center
    R3 --- Center
    R4 --- Center
    
    formula["angle[i] = i × (2π / uRayCount) + noise"]
    formula -.-> R1
    
    style Center fill:#ff9999,stroke:#333,stroke-width:2px
    style R1 fill:#aaffaa
    style R2 fill:#aaffaa
    style R4 fill:#aaffaa
```

## Raymarching Algorithm Detail

```mermaid
flowchart TD
    A[Starting UV + direction]
    B[Initialize loop<br/>MAX_RAY_STEPS = 256]
    C[Sample distance field<br/>dist = texture.uv.r]
    D[March forward<br/>uv += dir × dist]
    E{Outside bounds?}
    F{Hit surface?<br/>dist < EPS}
    G[Sample scene color]
    H[Sample last frame<br/>for accumulation]
    I[Apply decay:<br/>lastFrame × propagationRate]
    J[Pick maximum:<br/>scene vs decayed lastFrame]
    K[Mix direct/indirect<br/>uMixFactor]
    L[Return hit color]
    M[Return black<br/>no surface hit]
    
    A --> B
    B --> C
    C --> D
    D --> E
    E -->|Yes| M
    E -->|No| F
    F -->|Yes| G
    F -->|No| B
    G --> H
    H --> I
    I --> J
    J --> K
    K --> L
    
    style A fill:#e8f4ff
    style L fill:#aaffaa
    style M fill:#ffaaaa
    style F fill:#ffffaa
```

## Temporal Accumulation

```mermaid
sequenceDiagram
    participant CF as Current Frame
    participant LF as Last Frame Buffer
    participant RM as Raymarcher
    participant OUT as Output
    
    CF->>RM: Cast ray from pixel
    RM->>RM: March through distance field
    RM->>RM: Surface hit detected
    
    RM->>CF: Sample scene map at hit UV
    Note over CF: Direct lighting
    
    RM->>LF: Sample last frame at same UV
    Note over LF: Indirect lighting from previous frame
    
    LF-->>RM: vec4 accumulated_radiance
    
    RM->>RM: Apply decay: lastFrame × uPropagationRate
    RM->>RM: max(scene, decayed_lastFrame)
    RM->>RM: Mix with uMixFactor
    
    RM->>OUT: Final color with GI
    
    Note over OUT: Combines direct + indirect lighting<br/>for realistic light bouncing
```

## Noise Application

```mermaid
flowchart LR
    A[Fragment UV]
    B[Generate pseudo-random<br/>noise = randUV × 2000]
    C{Noise enabled?<br/>uNoise == 1}
    D[Add noise to ray angle]
    E[Uniform angles<br/>no offset]
    
    A --> B
    B --> C
    C -->|Yes| D
    C -->|No| E
    
    D --> F[angle[i] = baseAngle + noise]
    E --> F
    
    F --> G[Cast rays with angular offset]
    
    G --> H[Reduces banding artifacts]
    G --> I[Breaks up regular patterns]
    
    style C fill:#ffffaa
    style H fill:#aaffaa
    style I fill:#aaffaa
```

## Color Space Conversion Flow

```mermaid
flowchart TD
    subgraph "Linear Color Space (Internal)"
        L1[Scene colors in linear space]
        L2[Lighting calculations]
        L3[Accumulated radiance]
    end
    
    subgraph "Conversion Decision"
        D{sRGB output?<br/>uSrgb == 1}
    end
    
    subgraph "Output Options"
        O1[Keep linear<br/>Direct output]
        O2[Convert to sRGB<br/>lin_to_srgb]
    end
    
    L3 --> D
    D -->|No| O1
    D -->|Yes| O2
    
    O1 --> F[fragColor]
    O2 --> F
    
    note1["Why convert?<br/>Monitors expect sRGB<br/>Correct gamma display"]
    note1 -.-> O2
    
    style L1 fill:#e8f4ff
    style O2 fill:#ffffaa
    style F fill:#e8ffe8
```

## HSV↔RGB Conversion Functions

```glsl
// These utilities are included but NOT used in gi.frag
// They ARE used in prepscene.frag and draw.frag

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), 
                 step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), 
                 step(p.x, c.r));
    
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), 
                d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
```

## Uniform Parameters

| Uniform | Type | Description | Typical Values |
|---------|------|-------------|----------------|
| `uDistanceField` | `sampler2D` | Distance field from distfield.frag | Texture |
| `uSceneMap` | `sampler2D` | Scene geometry colors | Texture |
| `uLastFrame` | `sampler2D` | Previous frame accumulation buffer | Texture |
| `uRayCount` | `int` | Number of rays to cast | 32-128 |
| `uSrgb` | `int` | Enable sRGB output conversion | 0 or 1 |
| `uNoise` | `int` | Add per-pixel angular noise | 0 or 1 |
| `uPropagationRate` | `float` | Light decay factor | 0.9-1.5 |
| `uMixFactor` | `float` | Direct/indirect light mix | 0.0-1.0 |
| `uAmbient` | `int` | Enable ambient term | 0 or 1 |
| `uAmbientColor` | `vec3` | Ambient light color | (1,1,1) |

## Performance Characteristics

```mermaid
quadrantChart
    title "GI Shader Performance Profile"
    x-axis "Low Quality" --> "High Quality"
    y-axis "Fast" --> "Slow"
    quadrant-1 "Sweet Spot ✓"
    quadrant-2 "Reference Quality"
    quadrant-3 "Too Slow"
    quadrant-4 "Real-time"
    
    "64 rays, no noise": [0.7, 0.3]
    "128 rays, noise": [0.9, 0.15]
    "32 rays": [0.5, 0.6]
    "256 rays": [0.95, 0.05]
    
    note1["Typical config:<br/>64 rays, MAX_STEPS=256<br/>~30-60 FPS on modern GPU"]
    note1 -.-> "64 rays, no noise"
```

## Mathematical Core

```glsl
// Ray casting loop
for (float i = 0.0; i < TWO_PI; i += TWO_PI / uRayCount) {
  // Add noise offset if enabled
  float angle = i + n;
  
  // Calculate ray direction
  vec2 dir = vec2(cos(angle) * aspect, sin(angle));
  
  // March along ray
  vec4 hitcol = raymarch(fragCoord, dir);
  
  // Accumulate radiance
  radiance += hitcol;
}

// Average all samples
radiance /= uRayCount;

// Add constant ambient term
radiance += vec4(uAmbientColor * uAmbient * 0.01, 1.0);
```

## Comparison with Radiance Cascades

```mermaid
flowchart TB
    subgraph "GI Approach (This Shader)"
        G1[Cast rays uniformly<br/>in all directions]
        G2[Single pass per pixel]
        G3[O(rayCount × steps)]
        G4[High quality but expensive]
    end
    
    subgraph "RC Approach (rc.frag)"
        R1[Hierarchical cascades<br/>coarse to fine]
        R2[Multiple passes<br/>cascade levels]
        R3[Ocascades × baseRays]
        R4[Similar quality, faster]
    end
    
    G1 --> G2 --> G3 --> G4
    R1 --> R2 --> R3 --> R4
    
    G4 --> C[Final Result]
    R4 --> C
    
    style G4 fill:#ffffaa
    style R4 fill:#aaffaa
```

---

**File Location**: `res/shaders/gi.frag`  
**GLSL Version**: 330 core  
**Execution**: Once per frame (alternative to rc.frag)  
**Performance**: O(rayCount × maxSteps) per pixel  
**Use Case**: Reference implementation, high-quality offline rendering
