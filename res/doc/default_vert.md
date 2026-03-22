# default.vert - Vertex Shader Diagram

**Purpose**: Transform 3D vertex positions through model-view-projection matrix

## Flow Diagram

```mermaid
flowchart TD
    A[vertexPosition<br/>vec3] --> B[Expand to vec4<br/>vertexPosition, 1.0]
    B --> C[Multiply by mvp<br/>mat4 × vec4]
    C --> D[gl_Position<br/>Clip-space coordinates]
    
    style A fill:#e1f5ff
    style D fill:#fff4e1
    style C fill:#f0e1ff
```

## Data Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant VBO as Vertex Buffer
    participant VS as Vertex Shader
    participant GP as GPU Pipeline
    
    App->>VBO: Upload vertices (vec3)
    App->>VS: Set mvp uniform
    App->>GP: Draw call
    
    loop Each vertex
        VBO->>VS: Fetch vertexPosition
        VS->>VS: Create vec4(x,y,z,1.0)
        VS->>VS: Multiply by mvp
        VS->>GP: Output gl_Position
    end
    
    GP->>GP: Rasterize primitives
```

## Architecture Context

```mermaid
graph LR
    subgraph "Rendering Pipeline"
        A[Vertex Data] --> B[default.vert]
        B --> C[Fragment Shaders]
        C --> D[Framebuffer]
    end
    
    subgraph "GPU Stages"
        B -.->|Transform| E[Clipping]
        E -.->|Project| F[Rasterization]
        F -.->|Fragments| C
    end
    
    style B fill:#ff9999
```

## Key Parameters

| Parameter | Type | Purpose |
|-----------|------|---------|
| `vertexPosition` | `in vec3` | Input vertex position in object space |
| `mvp` | `uniform mat4` | Model-View-Projection transformation matrix |
| `gl_Position` | `out vec4` | Transformed position in clip space |

## Mathematical Operation

```
gl_Position = MVP × [x, y, z, 1]ᵀ

Where:
- MVP = Projection × View × Model
- Result is in homogeneous clip coordinates
- GPU performs perspective division automatically
```

## Usage in Pipeline

```cpp
// Set MVP matrix before drawing
SetShaderValue(shader, GetShaderLocation(shader, "mvp"), mvpMatrix, SHADER_UNIFORM_MAT4);

// Vertex attribute setup (Raylib handles this internally)
// Location 0: vertexPosition (vec3)
```

---

**File Location**: `res/shaders/default.vert`  
**GLSL Version**: 330 core  
**Used By**: All fragment shaders via Raylib's default vertex processing
