# AGENTS.md

2D Radiance Cascades demo. C++23, Raylib, ImGui, CMake. See `README.md` for upstream credit and `res/doc/AGENTS.md` for per-shader docs.

## Layout

```
src/                main.cpp (entry), demo.cpp/h (render loop, ImGui, shader uniforms)
                    config.h.in -> generated to build/config.h
lib/{raylib,imgui,rlImGui}/   git submodules, required for build
res/shaders/  res/textures/   GLSL + PNG, loaded at runtime via relative paths
References/          upstream paper + research images (read-only)
3d/                  SEPARATE 3D port with its own CMakeLists + CLAUDE.md — do not
                    confuse with this 2D project. Use ./build.sh here, ./build.ps1 there.
```

## Build & run

Requires CMake >= 3.25 and a C++23 compiler.

```bash
git submodule update --init   # first time only
./build.sh                    # build only
./build.sh -r                 # build and run
```

Manual:

```bash
cmake -S . -B build && cmake --build build
./build/radiance_cascades      # MUST be run from project root, see below
```

**Windows note:** `build.sh` is a bash script and is not invoked from this repo's PowerShell helper. On Windows use the manual `cmake` invocation above (the 3D sibling at `3d/build.ps1` does not apply to 2D).

**macOS:** `CMakeLists.txt` auto-links `IOKit`, `Cocoa`, `OpenGL` on `APPLE`. A `draw_macos.frag` variant exists in `res/shaders/` for canvas-passthrough differences.

**No tests, no linter, no formatter, no CI are configured.** Verification is: it builds, the window opens, ImGui responds, no new warnings.

## Resource path constraint (gotcha)

`main.cpp` bails with "Please run this file from the project root directory." if `./res/` is not next to the CWD. Shaders and textures are loaded by relative path. Never `cd build && ./build/...` — always launch from the repo root.

## Code conventions (observed in src/)

- `camelCase` variables, `PascalCase` types, lowercase structs with brace on same line.
- C++23 features allowed (`#include`/concepts/`std::expected` etc.). `CMAKE_CXX_EXTENSIONS OFF`.
- Group related state in the `Demo` class as private member structs (e.g. `user`, `debug`, per-shader settings). Avoid exposing fields publicly.
- ImGui debug panels are toggled via the `debug` flag in `demo.cpp`.

## RenderDoc

The original OpenGL renderdoc capture path is gated by `bRenderDoc` in `demo.cpp` (README calls it "fixed"). Leave that flag alone unless you are debugging capture; the readme still flags it as fragile across driver versions.

## Style of edits

- Read the full file before editing; this is a small codebase, full reads are cheap.
- Smallest safe change. Do not reformat unrelated code.
- After any non-trivial change: rebuild with `./build.sh` and run `./build/radiance_cascades` once from the project root to confirm the window opens.
- No secrets in code. `.gitignore` is the source of truth for ignored paths.

## Submodule gotcha

If a teammate reports "raylib headers not found" after a fresh clone, they skipped `git submodule update --init`. The submodules are not auto-initialized.
