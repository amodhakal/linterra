# Linterra: A Voxel Engine in C++ & OpenGL

**Project Status:** In Development

> ⚠️ **Platform Notice:** Currently confirmed to build and run on macOS only, due to platform-specific OpenGL headers (`<OpenGL/gl.h>`). Adding Windows and Linux support is a key goal for the next phase.

## What is Linterra?


Linterra is a from-scratch, Minecraft-style voxel engine written in modern C++ and OpenGL. This project is an ongoing exploration into building a voxel engine from scratch. It is not a playable game yet; the focus so far has been designing the underlying systems that make an infinite, block-based world possible.

![Screenshot of the game](/docs//screenshot.png)

## Implemented Features

### Milestone 5 — Chunk Pipeline Throughput, Thread Safety, and RAII Cleanup

This milestone focused on reducing chunk-generation stalls, removing key thread-safety hazards, and tightening resource lifetime management across the render pipeline.

**Chunk Generation Performance**
- Added an extended chunk-border heightmap cache so border exposure checks use direct array lookups instead of repeatedly calling noise functions.
- Consolidated duplicated chunk distance math into a shared helper in `ChunkManager` for both culling and spawn decisions.
- Moved chunk GPU upload commit to a main-thread-ready gate so worker threads only prepare mesh CPU data before signaling upload readiness.

**Threading & Race Condition Fixes**
- Added mutex-protected access around processing containers used by worker/main thread handoff.
- Ensured promotion/removal of in-flight chunk tasks is synchronized to avoid data races under load.

**Code Quality & Maintainability**
- Corrected configuration constant typos (`JUMP_VELOCITY`, `DEFAULT_PITCH`) and aligned usages.
- Standardized integer typing across touched systems toward `<cstdint>`-based types.
- Split `Player` implementation out of the header into `player.cpp` to reduce header bloat and avoid ODR-risk patterns.
- Removed dead commented callback code and deleted the unused `image.cpp` stub.

**OpenGL Resource Lifetime Improvements**
- Added RAII cleanup for shader programs (`glDeleteProgram`) via `Shader` destructor and safe move semantics.
- Improved chunk/texture resource move and cleanup behavior to prevent leaks or double-delete scenarios when objects are transferred.

### Milestone 4 — Architecture Overhaul & Memory Optimization

This milestone focused on restructuring the engine to support massive render distances (up to 64 chunks) while heavily optimizing memory consumption and setting up the foundation for asynchronous processing.

**Vertex Compression & Meshing Updates**
- Drastically compressed the vertex format from 28 bytes down to just 4 bytes per vertex.
- Removed greedy meshing to accommodate the new vertex layout and texture system. Combined with compression, this successfully halved memory usage at a 64-chunk render distance (dropping from 3.2 GB to 1.5 GB).

**Texture System Upgrade**
- Switched from an array of individual samplers to a unified `GL_TEXTURE_2D_ARRAY`.
- Improved GPU rendering performance (framerate nearly doubled at high chunk counts) and streamlined how block textures are accessed.

**Multithreading Foundation**
- Implemented a persistent work thread pool to begin offloading heavy operations (like chunk generation) from the main render thread.

**Terrain Data Enhancements**
- Transitioned from 3D heightmaps to 2D heightmaps to streamline terrain generation data and surface calculations.

### Milestone 3 — Textures & Performance

This milestone added textures, basic gravity and collision, and drastically improved performance.

**Perf Improvement**
- Added Element Buffers for each face
- Added greedy meshing to reduce triangle count
- Overall, with 24 chunks, speed: 54 fps -> **120 fps** and memory: 1.6 GB -> **0.3MB** *(Note: Greedy meshing was later superseded in M4 by vertex compression)*

**Textures**
- Added two basic textures for the landscape

**Gravity && Collisions**
- Added gravity option where user will fall down to the Earth
- Added ground collisions such that the user can stand on the landscape

### Milestone 2 — World Rendering & Performance

This milestone added actual voxel content, terrain generation, rendering efficiency, and early performance passes.

**Voxel Meshing System**
- Per-chunk face culling: only visible faces are emitted
- Generates a vertex buffer for each chunk at creation time
- Significantly reduces geometry vs. naïve full-cube rendering

**GPU Geometry Upload**
- Each chunk owns a VAO and VBO for its mesh
- Static draw buffers; draw calls are per chunk
- Deterministic creation and teardown of GPU resources

**View-Frustum Culling**
- Each chunk performs frustum intersection tests against camera planes
- Out-of-view chunks are skipped entirely in the render loop
- Big performance gains as world scale increases

**Noise-Based Procedural Terrain**
- Heightmap generation using layered Perlin noise
- Produces hills, slopes, and believable terrain variation across infinite chunks

**Block Storage System**
- Chunks contain a fixed 3D block array with typed block IDs
- Enables meaningful terrain data, not placeholder geometry

**Camera Math Improvements**
- Corrected right/front/up vector derivation
- More stable and consistent movement/orientation behavior

**Shader & Error Handling Improvements**
- Better visibility for shader compilation errors
- Validation for shader program linking
- Basic logging hooks added in critical paths

### Milestone 1 — Engine Foundations

The initial milestone focused on building the foundation required for an infinite voxel world.

**Dynamic Chunk Management**
- A `ChunkManager` loads and unloads chunks based on camera position
- Chunks stored in an `std::unordered_map` keyed by a custom `glm::vec2` hash
- Fixed render distance; out-of-range chunks are pruned each frame
- Supports a theoretically infinite world while keeping memory bounded

**First-Person Camera**
- Standard fly-through camera with yaw/pitch mouse-look
- WASD + Space/Shift movement
- Adjustable speed, sensitivity, and FOV

**Modern Shader Abstraction**
- A `Shader` class handles reading, compiling, linking shader programs
- Uniform location caching to reduce driver calls

**Basic Rendering Pipeline**
- Window and input via GLFW
- OpenGL loading via GLAD
- Core render loop with event dispatching and input callbacks

---

## Technical Stack

- **Language:** C++23
- **Graphics:** OpenGL 3.3+
- **Libraries:**
  - **GLFW** — windowing & input
  - **GLAD** — OpenGL function loading
  - **GLM** — mathematical foundations (matrices, vectors)

---

## Building & Running

### Prerequisites

- macOS 12+
- C++20/23-capable compiler (Clang recommended)
- CMake 3.15+
- Homebrew recommended:
  ```bash
  brew install glfw glm
  ```

### Build

```bash
git clone https://github.com/amodhakal/linterra.git
cd linterra
mkdir build && cd build
cmake ..
make
```

### Run

```bash
./linterra
```