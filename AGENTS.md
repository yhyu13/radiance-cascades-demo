# AGENTS.md - Radiance Cascades Demo

**Last Updated**: 2026-03-22  
**Project**: 2D Radiance Cascades Implementation  
**Tech Stack**: C++23, Raylib, ImGui, CMake  

---

## 🎯 Project Overview

This is a demonstration of the **Radiance Cascades** 2D lighting technique for soft shadows (penumbra effects). The project uses Raylib for rendering and ImGui for debugging UI.

### Key Components
- **`src/main.cpp`** - Application entry point, window initialization, main render loop
- **`src/demo.cpp/h`** - Core radiance cascades implementation and scene management
- **`lib/raylib/`** - Graphics framework (submodule)
- **`lib/imgui/`** - Immediate mode GUI (submodule)
- **`lib/rlImGui/`** - Raylib-ImGui integration bridge
- **`res/`** - Shaders, textures, and resources (required at runtime)

---

## 📋 Working Agreements

### 1. Accuracy & Recency (REQUIRED)

When tasks require current information:

```bash
# Establish current timestamp
date -Is  # ISO format: 2026-03-22T...
```

**Primary Sources** (in priority order):
1. Official Raylib docs: https://www.raylib.com/
2. ImGui documentation: https://github.com/ocornut/imgui
3. Radiance Cascades paper: https://github.com/Raikiri/RadianceCascadesPaper
4. This repo's References/ directory

**For API/dependency questions**: Use Context7 MCP with library pinning:
- `use library /raylib/raylib`
- `use library /ocornut/imgui`

### 2. Container-First Policy

**CRITICAL**: Never install system packages on host. Use containers for all tooling.

#### Existing Docker Workflow
If no container exists, create minimal Dockerfile:

```dockerfile
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    libgl1-mesa-dev \
    libx11-dev \
    libxrandr-dev \
    libxi-dev \
    libxcursor-dev \
    libxinerama-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN git submodule update --init \
    && mkdir build && cd build \
    && cmake .. && make

CMD ["./build/radiance_cascades"]
```

### 3. Editing Guidelines

**Before editing**:
1. Read relevant source files completely
2. Understand existing coding style (camelCase variables, C++23 features)
3. Make smallest safe change
4. Preserve existing conventions

**Coding Style Observed**:
```cpp
// Variable naming: camelCase
int screenWidth;
float brushSize;
Texture2D cursorTex;

// Structs: lowercase with braces on same line
struct WindowData {
  ImGuiWindowFlags flags = 0;
  bool open              = true;
};

// Class members: private sections with grouped concerns
class Demo {
  public:
    void render();
  private:
    struct { ... } user;
    bool debug;
    // shader settings grouped by category
};
```

**After editing** (MANDATORY):
```bash
# Rebuild from project root
./build.sh

# Or manually
cd build && make

# Run from project root (NOT build directory)
./build/radiance_cascades
```

### 4. Resource Loading Constraint

**CRITICAL**: The executable MUST run from the project root directory where `res/` exists.

```cpp
// From main.cpp
if (!DirectoryExists("res")) {
  printf("Please run this file from the project root directory.\n");
  return 0;
}
```

**Never**: Move or copy executable to other locations without `res/` folder.

### 5. Secrets & Sensitive Data

- No secrets in code (tokens, keys, credentials)
- Check `.gitignore` before adding config files
- Redact any sensitive output in terminal logs

---

## 🔧 Baseline Workflow

### Starting Any Task

1. **Goal Definition**
   - What needs to be achieved?
   - Acceptance criteria?
   - Constraints (time, safety, scope)?

2. **Context Gathering**
   ```bash
   # Check current state
   git status
   git log --oneline -5
   
   # Review relevant files
   # Read .agent/CONTINUITY.md if exists
   ```

3. **Recency Check**
   - Does this depend on current dates/versions?
   - If yes → Apply sourcing rules above

4. **Clarification**
   - Ask targeted questions for ambiguous requirements
   - Confirm before irreversible changes

### Implementation Pattern

```bash
# 1. Explore
ls -la
cat README.md
find . -name "*.cpp" -o -name "*.h"

# 2. Plan
# Document in .agent/CONTINUITY.md for complex tasks

# 3. Implement
# Use edit_file for minimal, reviewable changes

# 4. Verify
./build.sh -r  # Build and run
# Check for compilation errors
# Test functionality

# 5. Document
# Update CONTINUITY.md with decisions/discoveries
```

---

## 📝 CONTINUITY.md Protocol

**Location**: `.agent/CONTINUITY.md` (create if missing)

### When to Update
- Starting new session
- Making architectural decisions
- Discovering important behaviors
- Completing major milestones
- Course corrections mid-implementation

### Format Template

```markdown
# Continuity Log - Radiance Cascades Demo

## [PLANS]
- 2026-03-22T10:50Z [USER]: Initial task - Create AGENTS.md
- YYYY-MM-DDTHH:MMZ [TAG]: Description

## [DECISIONS]
- 2026-03-22T10:50Z [CODE]: Chose X approach because Y
  - Evidence: src/demo.cpp line 42-58

## [DISCOVERIES]
- 2026-03-22T10:50Z [TOOL]: Discovered that shaders must be loaded from res/shaders/
  - Impact: Cannot run from build/ directory

## [PROGRESS]
- 2026-03-22T10:50Z [ASSUMPTION]: Switching to Y approach due to Z constraint
  - Reason: Original plan incompatible with Raylib v5.0

## [OUTCOMES]
- 2026-03-22T10:50Z [MILESTONE]: Completed AGENTS.md
  - Achieved: Full working agreements documented
  - Remaining: None
  - Lesson: Container-first policy essential for cross-platform builds
```

**Provenance Tags**: `[USER]`, `[CODE]`, `[TOOL]`, `[ASSUMPTION]`, `[MILESTONE]`

**Anti-bloat Rules**:
- Facts only, no transcripts
- ISO timestamps required
- Mark unknowns as `UNCONFIRMED`
- Compress older items into `[MILESTONE]` bullets

---

## ✅ Definition of Done

A task is complete when ALL criteria are met:

- [ ] Requested change implemented OR question answered
- [ ] Code compiles without errors (`./build.sh` succeeds)
- [ ] Application runs without crashes
- [ ] Warnings addressed or explicitly documented as acceptable
- [ ] Documentation updated (README, comments, AGENTS.md)
- [ ] Impact explained (what changed, where, why)
- [ ] `.agent/CONTINUITY.md` updated if task affected goals/state
- [ ] Follow-ups listed for any intentional leftovers

### Verification Checklist

```bash
# Build verification
./build.sh
# Expected: No errors, binary created at build/radiance_cascades

# Runtime verification
./build/radiance_cascades
# Expected: Window opens, renders scene, ImGui functional

# Code quality (if tools available)
# - clang-format check
# - cppcheck static analysis
# - valgrind memory check (Linux)
```

---

## 🚨 Known Issues & Constraints

### Current Limitations

1. **Resource Path Dependency**
   - Executable must run from project root
   - Cannot run directly from `build/` directory
   - Reason: Shaders/textures loaded via relative paths

2. **Platform Support**
   - Primary: Linux/macOS (via `build.sh`)
   - Windows: Requires manual CMake adaptation
   - OpenGL renderdoc debugging broken for v1.42

3. **Submodule Dependencies**
   - Network required for initial setup
   - `git submodule update --init` mandatory before build

4. **Missing CI/CD**
   - No automated testing
   - No continuous integration
   - Manual verification required

### Safety Constraints

- **READ-only remote calls** unless explicitly instructed otherwise
- **NEVER** destructive API calls to production
- **NEVER** print secrets to terminal
- Workspace-scoped writes only

---

## 🛠 Tool Usage Guidelines

### Build Tools

| Tool | Purpose | Command |
|------|---------|---------|
| `cmake` | Build configuration | `cmake ..` from build/ |
| `make` | Compilation | `make` from build/ |
| `build.sh` | Automated build | `./build.sh [-r]` |
| `git` | Version control | Standard git commands |

### Debugging Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| RenderDoc | Graphics debugging | Not working for OpenGL v1.42 |
| GDB/LLDB | Native debugging | Standard C++ debugging |
| ImGui | Runtime UI debugging | Built-in via rlImGui |

### Editor Recommendations

- **VS Code**: C/C++ extension, CMake Tools
- **CLion**: Full CMake support, refactoring
- **Vim/Neovim**: clangd, cmake.vim plugins

---

## 📚 Reference Materials

### Essential Reading

1. **Radiance Cascades Paper**
   - Location: `References/` directory
   - Author: Alexander Sannikov (@Raikiri)
   - URL: https://github.com/Raikiri/RadianceCascadesPaper

2. **Raylib Documentation**
   - Primary: https://www.raylib.com/
   - Examples: `lib/raylib/examples/`

3. **ImGui Documentation**
   - Primary: https://github.com/ocornut/imgui
   - Backends: `lib/imgui/docs/BACKENDS.md`

4. **GM Shaders Articles**
   - Yaazarai GI: https://mini.gmshaders.com/p/yaazarai-gi
   - Jason McGhee: https://jason.today/
   - m4xc: https://m4xc.dev/

### Code Organization

```
radiance-cascades-demo/
├── src/                    # Main application code
│   ├── main.cpp           # Entry point, window management
│   ├── demo.h             # Demo class declaration
│   └── demo.cpp           # Core RC implementation
├── lib/                    # Third-party libraries
│   ├── raylib/            # Graphics framework
│   ├── imgui/             # UI framework
│   └── rlImGui/           # Integration layer
├── res/                    # Runtime resources
│   ├── shaders/           # GLSL shaders
│   └── textures/          # PNG textures
├── References/            # Research papers & images
├── build/                 # Build output (generated)
├── CMakeLists.txt         # Build configuration
├── build.sh               # Build automation
└── AGENTS.md              # This file
```

---

## 🔄 Session Startup Checklist

At the start of each new session:

1. [ ] Read `.agent/CONTINUITY.md` (if exists)
2. [ ] Check `git status` for uncommitted changes
3. [ ] Review recent commits: `git log --oneline -5`
4. [ ] Verify build works: `./build.sh`
5. [ ] Note current date/time in ISO format
6. [ ] Identify task goals and constraints

---

## 🆘 Escalation & Help

### When to Ask User

- Ambiguous requirements after thorough analysis
- Need for destructive operations (remote APIs, data deletion)
- Security-sensitive changes
- Major architectural decisions
- Breaking changes to existing API

### Preferred Communication

- Clear, specific questions
- Include context and attempted solutions
- Provide options with trade-offs when applicable
- State assumptions explicitly

---

## 📌 Quick Reference Commands

```bash
# Setup
git clone <repo-url>
cd radiance-cascades-demo
git submodule update --init

# Build
./build.sh        # Build only
./build.sh -r     # Build and run

# Manual build
mkdir build && cd build
cmake ..
make
cd ..
./build/radiance_cascades  # From project root!

# Git workflow
git status
git diff
git add <files>
git commit -m "message"
git push

# Debugging
# Run in gdb (Linux)
gdb ./build/radiance_cascades

# Check resource paths
ls res/shaders/
ls res/textures/
```

---

**End of AGENTS.md**

*This document serves as the canonical guide for AI assistants working on this repository. Always refer to it before making changes.*
