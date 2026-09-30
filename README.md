# Linterra: A Voxel Engine in C++ & OpenGL

## What is Linterra?

Linterra is a from-scratch, Minecraft-style voxel engine written in modern C++ and OpenGL. This project is an ongoing exploration into building a voxel engine from scratch. It is not a playable game yet; the focus so far has been designing the underlying systems that make an infinite, block-based world possible.

![Screenshot of the game](docs/screenshot.png)

## Technical Stack

- **Language:** C++23
- **Graphics:** OpenGL 3.3+ on Apple / non-GPU-noise platforms; OpenGL 4.3+ elsewhere (GPU compute terrain path). Rendering goes through an `IRenderer` abstraction layer (`src/renderer/`) with an OpenGL backend in `src/renderer/opengl/`.
- **Libraries:**
  - **GLFW** — windowing & input
  - **GLAD** — OpenGL function loading (vendored)
  - **GLM** — mathematical foundations (matrices, vectors)
  - **Dear ImGui** — debug UI (vendored)
  - **stb_image** — texture loading (vendored)
  - **FastNoise-style noise** — procedural terrain (vendored)
  - **doctest** — unit testing (vendored)

> Note: SDL3 is not a dependency. The windowing backend lives entirely in the OpenGL renderer via GLFW (`glfwInit`, `ImGui_ImplGlfw_InitForOpenGL`); earlier revisions of `CMakeLists.txt` still referenced SDL3 and the ImGui SDL3 backend, which has since been removed.

---

## Building & Running

### Prerequisites

- macOS 12+ / Linux / Windows 10+
- C++23-capable compiler (GCC >= 13, Clang >= 17, or MSVC >= 19.33 — the engine uses `<print>`)
- CMake 3.20+ (3.16–3.19 cannot represent `CMAKE_CXX_STANDARD 23`)
- [just](https://github.com/casey/just) (optional — convenience recipes)

**macOS (Homebrew):**

```bash
brew install glfw glm
```

**Linux (Debian/Ubuntu):**

```bash
sudo apt install libglfw3-dev libglm-dev
```

**Windows:**

- Install via vcpkg: `vcpkg install glfw3 glm`

Both the game and the test target configure and build with these commands. CI builds both, so a break in the game binary is caught on every push. See [CONTRIBUTING.md](CONTRIBUTING.md) for dev workflow details.

### Build

With CMake directly:

```bash
git clone https://github.com/amodhakal/linterra.git
cd linterra
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Or with `just` (see `justfile` for all recipes):

```bash
just build    # release build
just dev      # debug build with sanitizers
```

### Run

```bash
./build/linterra
```

### Unit Tests

There are two test executables, both built alongside the game and both run by
`ctest`.

**`linterra_tests`** is a doctest-based unit suite for the pure (no-GL-context)
subsystems — procedural noise, camera math, frustum culling, `ThreadPool`,
`PackedVertex` bit layout, `IO`, and `Player` mouse-look. It links
`linterra_core`, the same static library the game links, so a test can never
pass against a private copy of the code the game does not use. It needs only
GLM at runtime, so it runs headless.

**`linterra_smoke`** covers the GL path, which the unit suite cannot reach at
all: it creates a *hidden* GLFW window — a real offscreen context with nothing
on screen — and drives the actual engine classes through shader compilation,
uniform resolution, buffer and VAO creation, chunk mesh upload, draw
submission, the offscreen framebuffer, and teardown. It needs a windowing
system, so on a headless Linux machine run it under `xvfb-run`.

```bash
# configure + build everything (tests are ON by default)
cmake -S . -B build
cmake --build build

# build + run the unit suite directly, or via ctest
cmake --build build --target linterra_tests
./build/linterra_tests

# run both (add --output-on-failure for detail on a red run)
ctest --test-dir build

# headless Linux: the smoke test needs a display
xvfb-run -a ctest --test-dir build
```

Or simply:

```bash
just test
```

To omit the test targets (e.g. when only building the game):

```bash
cmake -S . -B build -DBUILD_TESTS=OFF -DBUILD_SMOKE_TEST=OFF
```

The doctest framework is vendored at `vendor/doctest/include/doctest/doctest.h`
— no extra dependency to fetch.

---

## Roadmap

Milestones 1–10 are shipped (see [Implemented Features](#implemented-features) below). M11–M19 are the planned sequence, and they are strictly ordered — each gates the next.

| Milestone | Title | Issues | Scope |
| --- | --- | --- | --- |
| ~~M10~~ | Build, CI & Safety Net | — | Shipped: headless smoke tests, game in CI, ASan/UBSan, shader validation, macOS runner |
| M11 | Render Correctness: GPU Terrain & Culling | 10 | Fix culling, SSBO slots, winding |
| M12 | Resource Lifetime, Shutdown & Error Reporting | 14 | Shutdown order, GL error attribution |
| M13 | Threading, Chunk Pipeline & Player Physics | 11 | Physics query, race windows |
| M14 | Renderer Abstraction & Backend Portability | 9 | Split `IRenderer`, Metal/Vulkan |
| M15 | Streaming & Draw-Path Performance | 10 | Draw-call sorting, greedy meshing |
| M16 | Rendering Quality: Lighting, Water & Terrain | 9 | AO, water, biome variety |
| M17 | Gameplay: Interaction, Persistence & UI | 7 | Block editing, collision, HUD |
| M18 | Documentation, Licensing & Code Hygiene | 9 | License, naming, dead code |
| M19 | Spatial Partitioning & LOD | 1 | Sparse voxel octree |

Full dependency graph and issue lists: [docs/roadmap.md](docs/roadmap.md).

---

## Implemented Features

### Milestone 10 — Build, CI & Safety Net

This milestone turned "it builds on my machine" into an automated safety net. Previously the doctest suite covered pure math with no GL context, so the entire GL path — shader loading, buffer and VAO creation, chunk upload, the offscreen framebuffer, resource teardown — shipped unverified, and a break in the game binary could not be caught by any check.

**Build Correctness**
- Enabled compiler warnings that were previously absent: `-Wall -Wextra -Wpedantic -Wshadow` (and `/W4` on MSVC) via a shared `linterra_warnings` INTERFACE target, so the game and the test target cannot drift apart. All three defects this surfaced are fixed: a `-Wreorder-ctor` in `Player`, a double→float narrowing, and an anonymous struct inside a union in `PackedVertex` (a GNU extension, now a named `Fields` member with the bit layout unchanged).
- Raised the CMake floor from 3.16 to 3.20, since `CMAKE_CXX_STANDARD 23` is only understood from 3.20, and added an explicit toolchain gate (GCC ≥ 13, Clang ≥ 17, MSVC ≥ 19.33) so a missing `<print>` fails at configure time with a message naming the cause.
- Removed the duplicated renderer source list. The recursive glob already covered `src/renderer/opengl/*.cpp`, and the `if(USE_OPENGL)` block re-declared the same paths — which also meant `-DUSE_OPENGL=OFF` never actually excluded anything. The option now warns instead of silently doing nothing.
- `compile_commands.json` is emitted by default, so clangd, clang-tidy, and IDE tooling work with no extra flags.
- Fixed the `justfile`: `just fmt` had been skipping every `.hpp` file — seven of them, including the whole renderer interface. The misleading `clippy` alias is replaced by a standalone `tidy` recipe plus a `tidy-build` fallback, and `fmt-check` was added.

**CI Coverage**
- The build now runs on **macOS as well as Ubuntu**. This is not redundant: the engine is developed and run on macOS, and it is the only platform that compiles the `__APPLE__` branches — the `USE_GPU` noise selection in `config.h`, the `_NSGetExecutablePath` path in `io.cpp`, and the forward-compat and 3.3-context hints in the OpenGL renderer.
- **`linterra_smoke`**, a new headless executable that creates a *hidden* GLFW window — a real offscreen GL context with nothing on screen — and drives the actual engine classes through the sequence `Application` uses. It covers shader compilation and linking, uniform resolution, buffer and VAO creation, chunk mesh generation and GPU upload, draw submission, the offscreen framebuffer, and context teardown, asserting on observable state rather than pixels. 34 checks.
- **Shader validation** with `glslangValidator`, in its own job. Shaders are compiled by the driver at runtime, so a syntax error previously surfaced only as a broken frame on a machine with a GL context.
- **ASan + UBSan** over the suite, via a new `-DLINTERRA_SANITIZE` option. `just dev` had been able to do this locally but nothing ran it automatically. The option replaces a `CMAKE_CXX_FLAGS` string that never reached the C sources (`vendor/glad/src/glad.c`) or the link line reliably.

**Test Coverage**
- **`linterra_core`**, a static library of the GL-free engine sources (`camera`, `frustum`, `io`, `player`, `threadpool`) linked by both the game and the tests. Previously `linterra_tests` named `src/camera.cpp` and `src/frustum.cpp` directly, compiling a second copy — the suite was testing code the game did not use, and nothing would have caught the two drifting apart.
- **Four more subsystems under test**: `ThreadPool` (task completion, genuine concurrency, and enforcement of the pending-task cap that stops one frame flooding the pool), `PackedVertex` (the 4-byte vertex format pinned bit for bit), `IO` (byte-exact reads, CRLF and lone-CR preservation, the executable-directory fallback), and `Player` (mouse-look, pitch clamping, view-vector normalisation). The suite grew from 14 test cases / 554 assertions to **40 / 675**.

**Notes**
- The smoke test runs on the Linux runner under `xvfb-run` with Mesa's software rasteriser. A GitHub macOS runner has no window server session, so no CGL context can be created there at all; the macOS job still compiles every Apple-specific branch and runs the unit suite.
- `-Wconversion` is deliberately *not* enabled yet: it emits 250+ findings, nearly all mechanical narrowing in the GL upload paths. That cleanup is outstanding.

### Milestone 9 — GPU-Accelerated Terrain Generation & Platform-Adaptive Noise Fallback

This milestone introduced GPU compute shader acceleration for procedural terrain heightmap generation via Shader Storage Buffer Objects (SSBOs), along with platform-adaptive noise selection.

**GPU Compute Terrain Generation**
- Added `terrain.comp`: parallelizes FBM noise evaluation on the GPU using compute shaders (`local_size_x = 16`, `local_size_y = 16`).
- Integrated Shader Storage Buffer Objects (`BufferType::Storage`) to store generated heightmaps and read them back via `IRenderer::getBufferSubData`.
- Split chunk generation into independent heightmap generation (CPU vs GPU) and CPU-parallelized vertex meshing (`Chunk::generateMesh`).

**Platform-Adaptive Noise Flag**
- Added `Constants::Noise::USE_GPU` flag to control whether noise generation runs on the CPU or GPU.
- Implemented automatic fallback for macOS (`#if defined(__APPLE__)`), setting `USE_GPU = false` to avoid compilation errors on OpenGL 4.1 contexts while defaulting to GPU acceleration on OpenGL 4.3+ platforms.

### Milestone 8 — Split Fog into a Post-Process Pass

This milestone decoupled fog from the scene's fragment shader by moving it into a dedicated post-process stage, so general rendering and atmospheric effects are now independent subsystems.

**Offscreen Render Target**

- Added `Framebuffer` (`src/framebuffer.h` / `src/framebuffer.cpp`): owns an FBO with an RGBA color texture and a depth renderbuffer, resized on window changes. The scene's view-space distance is carried in the color alpha channel so the fog pass can reconstruct it without a depth-texture attachment.

**Two-Stage Render Pipeline**

- Scene pass (`render.vert` / `render.frag`): samples the texture atlas, applies directional face lighting, and writes shaded color + view distance — no fog math.
- Fog pass (`fog.vert` / `fog.frag`): a single attribute-less fullscreen triangle composites exponential fog over the offscreen scene using `uFogStart` / `uFogEnd` / `uFogColor`.
- `Application::update()` now renders the world into the framebuffer, then binds it as a sampler for the fog shader onto the default framebuffer. Window resize forwards to `Framebuffer::resize`.

**Shader Reorganization**

- Replaced the combined `shaders.vert` / `shaders.frag` with four focused files under `shaders/`: `render.vert`, `render.frag`, `fog.vert`, `fog.frag`. Paths live in `Constants` (`RENDER_VERTEX_PATH`, etc.).

### Milestone 7 — Unit Testing & CI

This milestone added a regression safety net so math-heavy subsystems can be validated automatically on every push, rather than verified by eye.

**Test Framework**

- Vendored [doctest](https://github.com/doctest/doctest) (single-header) at `vendor/doctest/include/doctest/doctest.h` — no external fetch step, matching the project's vendor-everything approach.
- New `linterra_tests` CMake target (gated by `BUILD_TESTS`, ON by default) compiles only the pure, no-GL-context subsystems: procedural `Noise`, `Camera` math, and `Frustum` culling.

**Test Coverage**

- `Noise`: fbm determinism, bounded `[-1,1]` output, continuity, seed round-trip/isolation.
- `Camera`: constructor placement, finite view/projection matrices, and a regression guard for the right-vector.
- `Frustum`: near-chunk inclusion, far-plane culling, side-plane culling, determinism.
- 14 test cases / 554 assertions, all passing (as of Milestone 7; the suite has since grown — see Milestone 10).

**Decoupling & Bug Fixes Surfaced**

- `config.h` now guards GLAD/GLFW behind a `LINTERRA_NO_OPENGL` macro so pure math compiles without GL headers.
- Tests caught and fixed a real bug: `Camera::getRight()` computed `cross(m_Up, m_WorldUp)` (zero vector → NaN via `normalize`), silently corrupting frustum side-plane culling. Now uses `cross(m_Front, m_Up)`.
- Fixed case-sensitive includes (`"frustum.h"` → `"Frustum.h"`) in `frustum.cpp`/`manager.cpp` that warned on macOS and would break the build on Linux/Windows.

**Continuous Integration**

- Added `.github/workflows/ci.yml`: on every push and PR, Ubuntu + clang installs deps, configures with `BUILD_TESTS=ON`, builds the default target set, and runs `ctest --output-on-failure`. (Extended in Milestone 10 with a macOS runner, shader validation, and sanitizers.)
- `ctest` exits non-zero on any failure, so a red test fails the job and blocks the merge on protected branches.

### Milestone 6 — Renderer Abstraction & Backend Portability

This milestone introduced a renderer abstraction layer to decouple the engine from OpenGL, enabling future support for Metal, Vulkan, and other graphics APIs through a unified interface. Also added cross-platform support for Linux and Windows.

**Cross-Platform Support**
- Made `GLFW_OPENGL_FORWARD_COMPAT` conditional for macOS only (`#if defined(__APPLE__)`).
- Added platform-specific build instructions for macOS (Homebrew), Linux (apt), and Windows (vcpkg).
- Project now builds and runs on macOS, Linux, and Windows.

**Renderer Interface Architecture**

- Created `IRenderer` interface with pure virtual methods for all core rendering operations (buffers, shaders, textures, draw calls, state management).
- Abstracted resource types: `IBuffer`, `IVertexArray`, `IShader`, `IShaderProgram`, `ITexture`.
- Defined enumerations for `BufferType`, `BufferUsage`, `ShaderType`, `TextureType`, `PrimitiveType`, `DataType`, `IndexType`, `Feature`, and `RenderBackend`.

**OpenGL Backend Implementation**

- Implemented full OpenGL backend: `OpenGLRenderer`, `OpenGLBuffer`, `OpenGLVertexArray`, `OpenGLShader`, `OpenGLShaderProgram`, `OpenGLTexture`.
- All OpenGL-specific code is now contained within `src/renderer/opengl/`.
- Existing `Shader`, `Texture`, and `Chunk` classes refactored to accept `IRenderer*` and delegate to the interface.

**Factory Pattern**

- Added `createRenderer(RenderBackend)` factory function for runtime backend selection.
- CMake build options added: `USE_OPENGL`, `USE_METAL`, `USE_VULKAN` (Metal/Vulkan not yet implemented).

**Codebase Refactoring**

- `Application` now owns the `IRenderer` instance and passes it to all rendering components.
- `ChunkManager` propagates the renderer to `Chunk` instances and `TaskResult` objects.
- Fixed RAII patterns for move-only resources across the codebase.

**Future Extensibility**

- To add Metal support: create `src/renderer/metal/` with implementations of the interface classes.
- To add Vulkan support: create `src/renderer/vulkan/` with implementations of the interface classes.
- Switching backends requires only changing the CMake option—no code changes needed in `Shader`, `Texture`, `Chunk`, `ChunkManager`, or `Application`.

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
- Overall, with 24 chunks, speed: 54 fps -> **120 fps** and memory: 1.6 GB -> **0.3MB** _(Note: Greedy meshing was later superseded in M4 by vertex compression)_

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
