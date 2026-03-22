# prepscene.frag - Scene Preparation Shader Diagram

**Purpose**: Combine occlusion and emission maps with dynamic elements (orbs, mouse light, rainbow animation)

## Complete Pipeline Diagram

```mermaid
flowchart TD
    subgraph Inputs
        O[uOcclusionMap<br/>Scene geometry]
        E[uEmissionMap<br/>Light sources]
        M[uMousePos<br/>Cursor location]
        T[uTime<br/>Animation clock]
    end
    
    subgraph Processing["Scene Preparation Stages"]
        S1[Sample Textures<br/>occlusion + emission]
        S2[Alpha Normalize<br/>white→transparent<br/>black→opaque]
        S3{uRainbow == 1?}
        S4[HSV→RGB Convert<br/>time-based hue shift]
        S5{uMouseLight == 1?}
        S6[Circle SDF<br/>at mouse position]
        S7{uOrbs == 1?}
        S8[Render 6 orbiting orbs<br/>+ 2 oscillating bars]
        S9[Composite All<br/>max emission/occlusion]
    end
    
    subgraph Output
        F[fragColor<br/>Scene buffer]
    end
    
    O --> S1
    E --> S1
    S1 --> S2
    S2 --> S3
    S3 -->|Yes| S4
    S3 -->|No| S9
    S4 --> S9
    M --> S5
    S5 -->|Yes| S6
    S5 -->|No| S7
    S6 --> S9
    T --> S7
    S7 -->|Yes| S8
    S7 -->|No| S9
    S8 --> S9
    S9 --> F
    
    style Inputs fill:#e8f4ff
    style Processing fill:#fff4e8
    style Output fill:#e8ffe8
```

## Dynamic Elements Rendering

```mermaid
flowchart LR
    subgraph MouseLight["Mouse Light (if enabled)"]
        M1[Check uMouseDown] --> M2[Circle SDF test]
        M2 --> M3[Apply uBrushColor]
    end
    
    subgraph Orbs["Animated Orbs (if enabled)"]
        O1[Calculate 6 orb positions<br/>sin/cos orbit] --> O2[SDF circle test]
        O2 --> O3[HSV color per orb<br/>i/6.0 hue]
        
        O4[Horizontal bar<br/>cos oscillation] --> O5[SDF test]
        O5 --> O6[sin hue animation]
        
        O7[Vertical bar<br/>sin oscillation] --> O8[SDF test]
        O8 --> O9[cos hue animation]
    end
    
    subgraph Rainbow["Rainbow Mode"]
        R1[Get current HSV] --> R2[Add uTime/8 to hue]
        R2 --> R3[Convert to RGB]
    end
    
    style MouseLight fill:#ffe8e8
    style Orbs fill:#e8ffe8
    style Rainbow fill:#e8e8ff
```

## Orb Movement Pattern

```mermaid
quadrantChart
    title "6 Orbiting Orbs + 2 Oscillating Bars"
    x-axis "Left" --> "Right"
    y-axis "Bottom" --> "Top"
    quadrant-1 "NE"
    quadrant-2 "NW"
    quadrant-3 "SW"
    quadrant-4 "SE"
    "Orb 0": [0.7, 0.8]
    "Orb 1": [0.5, 0.9]
    "Orb 2": [0.3, 0.8]
    "Orb 3": [0.2, 0.5]
    "Orb 4": [0.3, 0.2]
    "Orb 5": [0.5, 0.1]
    "H-Bar": [0.5, 0.5]
    "V-Bar": [0.5, 0.5]
```

## Color Space Conversion Flow

```mermaid
flowchart LR
    A[RGB Input] --> B[rgb2hsv conversion]
    B --> C{Animation mode?}
    C -->|Rainbow| D[Add time to hue<br/>h += uTime/8]
    C -->|Normal| E[Keep original]
    D --> F[hsv2rgb conversion]
    E --> F
    F --> G[RGB Output]
    
    style A fill:#ffe8e8
    style G fill:#e8ffe8
    style F fill:#e8e8ff
```

## Signed Distance Field Circle Function

```mermaid
flowchart TD
    A[fragCoord] --> B[Calculate distance<br/>to center point]
    B --> C{distance < radius?}
    C -->|Yes| D[Return true<br/>Inside circle]
    C -->|No| E[Return false<br/>Outside circle]
    
    formula["distance = length<fragCoord - center>"]
    formula -.-> B
    
    style D fill:#aaffaa
    style E fill:#ffaaaa
```

## Uniform Parameters

| Uniform | Type | Description | Range |
|---------|------|-------------|-------|
| `uOcclusionMap` | `sampler2D` | Scene geometry (white=solid) | Texture |
| `uEmissionMap` | `sampler2D` | Light sources (white=emissive) | Texture |
| `uMousePos` | `vec2` | Current mouse position | 0-resolution |
| `uTime` | `float` | Time in seconds | 0-∞ |
| `uOrbs` | `int` | Enable animated orbs | 0 or 1 |
| `uRainbow` | `int` | Enable rainbow animation | 0 or 1 |
| `uMouseLight` | `int` | Enable mouse light | 0 or 1 |
| `uBrushSize` | `float` | Brush size (normalized) | 0.0-1.0 |
| `uBrushColor` | `vec4` | Brush color (RGBA) | 0.0-1.0 |

## Code Structure

```glsl
void main() {
  // 1. Sample input textures
  vec4 o = texture(uOcclusionMap, fragCoord);
  vec4 e = texture(uEmissionMap, fragCoord);
  
  // 2. Normalize alpha channels
  if (o == vec4(1.0)) o = vec4(0.0);
  else o = vec4(0.0, 0.0, 0.0, 1.0);
  
  // 3. Apply rainbow animation
  if (uRainbow == 1 && e != vec4(0.0))
    e = hsv2rgb(vec3(hue + uTime/8, 1.0, 1.0));
  
  // 4. Composite emission over occlusion
  fragColor = max(e.a, o.a) == e.a ? e : o;
  
  // 5. Add mouse light
  if (uMouseLight == 1 && sdfCircle(uMousePos, uBrushSize*64))
    fragColor = uBrushColor;
  
  // 6. Add animated orbs
  if (uOrbs == 1) {
    // 6 orbiting colored spheres
    for (int i = 0; i < 6; i++) {
      p = (vec2(cos(uTime/ORB_SPEED+i), sin(uTime/ORB_SPEED+i)) 
           * resolution.y/2 + 1) / 2 + CENTRE;
      if (sdfCircle(p, resolution.x/80))
        fragColor = hsv2rgb(vec3(i/6.0, 1.0, 1.0));
    }
    // + 2 oscillating bars
  }
}
```

---

**File Location**: `res/shaders/prepscene.frag`  
**GLSL Version**: 330 core  
**Execution**: Once per frame  
**Output**: Scene buffer for JFA preprocessing
