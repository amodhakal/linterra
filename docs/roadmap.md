# Roadmap

Milestones 1 through 12 are shipped and documented in the [Implemented Features](../README.md#implemented-features) section of the README. What follows is the planned sequence: M13 through M19. M10 was planned across 11 issues, of which #19 and #21 were already resolved on `main` when the milestone was picked up and were closed with evidence; the other nine shipped as a stack of pull requests. Each milestone is labelled with the matching `M10`..`M19` GitHub label so you can filter the issue list to just that slice. The milestones are **strictly ordered** — every one of them gates at least one other, because each depends on correctness or stability guarantees established by the ones before it. Attempting M15 before M12, or M17 before M13, produces work that has to be thrown away.

---

## Milestone dependency graph

```
                          ┌──────────────────────────────────────────┐
                          │  M18  Documentation, Licensing, Hygiene    │
                          │  (independent)                    SHIPPED │
                          └──────────────────────────────────────────┘

   ┌─────────────────────────────────────────────────────────────────────┐
   │                                                                     │
   │  M10  Build, CI & Safety Net  SHIPPED ◄── gates EVERYTHING below    │
   │  headless smoke test + game-target-in-CI                             │
   └─────────────────────────────────────────────────────────────────────┘
              │                        │                      │
              ▼                        ▼                      ▼
   ┌────────────────────────┐  ┌────────────────────┐  ┌──────────────────┐
   │ M11  Render Correctness │  │ M12  Resource      │  │ M14  Renderer    │
   │ GPU terrain & culling  │  │ Lifetime, Shutdown │  │ Abstraction &    │
   │                   SHIP │  │ & Error Reporting  │  │ Portability      │
   └───┬───────┬───────┬────┘  └───┬──────────┬────┘  └────────┬─────────┘
       │       │       │            │          │               │
       │       │       │            │          │               │
       ▼       │       │            ▼          ▼               │
   ┌────────┐  │       │     ┌────────────────────────┐         │
   │ M13    │  │       │     │ M16  Rendering Quality │         │
   │Threading│ │       │     │ Lighting, Water,       │         │
   │ & Physics│ │       │     │ Terrain                │         │
   └───┬────┘  │       │     └───┬──────────────┬─────┘         │
       │       │       │         │              │               │
       │       │       └─────────┼──────────────┘               │
       │       │                 ▼                              │
       │       │         ┌──────────────┐                       │
       │       │         │ M17 Gameplay │◄──────────────────────┘
       │       │         │ Interaction, │   (biomes/blocks give you
       │       │         │ Persistence, │    something to interact with)
       │       │         │ UI           │                       │
       │       │         └──────────────┘                       │
       │       │                                              │
       ▼       ▼                                              ▼
   ┌────────────────────────┐                        ┌──────────────────────┐
   │ M15  Streaming &       │                        │ M19  Spatial         │
   │ Draw-Path Performance  │                        │ Partitioning & LOD   │
   └────────────────────────┘                        └──────────────────────┘
    perf on a wrong frustum is          LOD inherits every culling bug
    meaningless; greedy meshing 2.0      and must survive a backend swap
    needs a settled render pipeline
```

### The dependencies, stated plainly

| Blocker | Why |
| --- | --- |
| **M10 → everything** | The existing doctest suite runs pure math with **no GL context** — noise, camera matrices, frustum planes. Every bug in M11–M13 lives in code the suite cannot reach, which is exactly why several shipped anyway. A headless smoke-test executable plus a game-target-in-CI build is what makes those bugs detectable. Every other milestone's work is unverifiable until this lands. |
| **M11 → M13** | Player physics queries the terrain to resolve ground height and collision. A wrong terrain query means a wrong walkable surface. |
| **M11 → M15** | Performance tuning is meaningless on a broken frustum. If chunks that should be culled are being drawn (or vice versa), every measurement downstream is noise. |
| **M11 → M19** | LOD selection is layered directly on top of the culling hierarchy. An SVO inherits every culling bug and multiplies its blast radius by the number of levels. |
| **M12 → M15** | Performance work churns GPU resources hard — rebinding, reallocating, re-uploading. Doing it against a lifecycle with no defined shutdown order produces leaks and use-after-free that look exactly like performance problems. |
| **M12 → M16** | Same reason. Water transparency, depth-texture attachments, and new shader passes all allocate and resize framebuffers and textures mid-frame. |
| **M13 → M17** | Block placement and breaking need a reliable collision-and-resolution query against the voxel grid. Gameplay built on an unfixed physics query is built twice. |
| **M14 → M19** | SVO is a large, backend-touching subsystem. Portability has to hold — the abstraction has to be clean enough that a new structure does not leak backend types — before adding a spatial structure that inherits them. |
| **M16 → M17** | You need biomes, block variety, and water to have anything meaningful to interact with. Placing a block in a flat grey test world does not validate the feature. |
| **M16 → M15 (soft)** | Greedy meshing 2.0 (#79) is filed under M15, and it is more tractable once the render pipeline has settled in M16: a stable vertex layout, single-sourced shader constants, and a depth texture instead of depth-packed-into-alpha all change the mesher's inputs. |
| **M18 → nothing** | Documentation, licensing, naming, and dead-code removal are independent of engine behaviour and can be worked in parallel with any milestone, including M10. |

---

## Milestones

### M10 — Build, CI & Safety Net — SHIPPED

**Status: shipped.** See the Milestone 10 section of the [README](../README.md#implemented-features) for the full write-up. The planned scope was: turn the project from "builds on my machine" into something with an automated safety net. The doctest suite covered pure math with no GL context, so the entire GL path — shader loading, chunk upload, the offscreen framebuffer, resource teardown — shipped unverified. The milestone added a headless smoke-test executable that exercises that path without a visible window, brought the game target into CI, added a macOS runner, shader validation, and sanitizers, extracted a shared `linterra_core` library, and enabled the compiler warnings that would have caught several of the bugs now filed under M11–M13.

Two planned items turned out to be already done and were closed with evidence rather than re-implemented: **#19** (the SDL3 dependency was removed by #24; `CMakeLists.txt` already requires and links GLFW) and **#21** (CI already built the full default target set). One deliberate omission: `-Wconversion` is not enabled yet, as it emits 250+ findings, nearly all mechanical narrowing in the GL upload paths.

Historical scope as planned:

**Build Correctness**
- Fix the CMake dependency declaration so the build requires and links exactly the libraries actually used, and correct the declared minimum CMake version.
- Resolve the duplicated renderer sources in the target's source list.
- Export `compile_commands.json` by default so clangd and clang-tidy work out of the box.
- Fix the `justfile` clippy alias and the broken `fmt` find expression.

**CI Coverage**
- Build the **game** target in CI, not just `linterra_tests` — a break in the game binary is caught on every push.
- Add a headless smoke-test executable for GL-path regression testing.
- Add a shader validation step so a shader that fails to compile or link fails CI rather than a user's run.
- Enable sanitizers (ASan/UBSan) in CI.

**Test Coverage**
- Extract a `linterra_core` static library so the test target links the same code the game does, rather than a parallel copy that can drift.
- Add unit tests for core subsystems that currently have none.

**Platform Matrix**
- Add a macOS CI runner — the primary development platform's code paths are currently never compiled anywhere.

Issues: #19, #21, #65, #67, #68, #69, #73, #74, #89, #154, #155

### M11 — Render Correctness: GPU Terrain & Culling — SHIPPED

**Status: shipped.** See the Milestone 11 section of the [README](../README.md#implemented-features) for the full write-up.

The planned scope was: frustum side planes computed with degrees where radians were intended, bottom faces wound backwards, every GPU chunk writing to SSBO slot 0 because a compute uniform was never registered, SSBO slots recycled before the deferred readback completed, and a missing read barrier. The high bug density and the small size of the individual fixes made this the highest-value milestone in the roadmap.

**What the measurements changed.** Two of the three defects worked in the final stack were not conditional. The `uSlot` gap meant every GPU chunk computed a base offset of `0`; slot recycling meant **100% of readbacks (60416 / 60416) returned another chunk's heights**, with a slot overwritten 16 times before its consumer read it. The milestone's own premise — that these were wrong-terrain-if-the-bound-were-violated defects — understated the case: on any non-Apple build the GPU terrain path did not work at all, and nothing reported it.

Two further findings contradicted the plan rather than completing it. **The storage buffer had no allocation**: `convertBufferUsage` returned `GL_*_DRAW` for every buffer type, which is `GL_INVALID_ENUM` for `GL_SHADER_STORAGE_BUFFER`, so the heightmap SSBO's `glBufferData` had been failing silently on every non-Apple build. And **a latent null-pointer crash** — `glMemoryBarrier` is a GLAD function pointer that is null on the development platform's context — was exposed only by moving the barrier to the read site, which is where the milestone's own plan wanted it.

**What could not be verified here.** `Constants::Noise::USE_GPU` is a `constexpr false` on Apple, so every fix in this milestone is provable as a uniform-registration or scheduling *property* and not as resulting terrain. Two smoke checks skip on macOS for the same reason (GL 4.1, `GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS == 0`); Linux CI runs them under `xvfb` against Mesa. That is a coverage gap inherited from M10's decision to keep the GPU path off on Apple, not a gap introduced here — but it is the reason the M11 write-up separates "measured" from "proven by construction".

The fixes were made reachable by extracting two GL-free units the unit suite can actually link: `GpuSlotPool`, a free-list carrying the slot-lifetime policy, and `Constants::TERRAIN_COMPUTE_UNIFORMS`, checked against `terrain.comp` in both directions by `tests/test_shader_uniforms.cpp`.

Planned scope as planned:

**Frustum Culling**
- Fix the side-plane computation using degrees instead of radians, which collapses the planes at low FOV and wastes draws across the frustum.
- Repair the broken frustum culling path and strengthen the tests around plane-normal and AABB conventions so the sign errors cannot regress.
- Update the camera aspect ratio on window resize.

**GPU Terrain Path**
- Register the `uSlot` compute uniform so each GPU chunk writes its own SSBO slot instead of all of them overwriting slot 0.
- Stop recycling SSBO slots before the deferred readback completes, which currently lets a chunk read another chunk's heightmap.
- Fix the missing read barrier in `getBufferSubData`, and give it a way to signal failure.
- Initialize chunk heightmaps; make `getHighestBlockY` non-public and bounds-checked.
- Fix undefined behaviour in the negative-`float`-to-`uint16_t` heightmap conversion.

**Geometry**
- Re-wind the bottom (-Y) block faces so they stop being silently back-face culled.

Issues: #4, #12, #66, #125, #126, #127, #135, #136, #140, #151

### M12 — Resource Lifetime, Shutdown & Error Reporting — SHIPPED

**Status: shipped.** See the Milestone 12 section of the [README](../README.md#implemented-features) for the full write-up.

The planned scope was: destroy GL resources before `glfwTerminate()`, stop the thread pool draining on shutdown, stop exceptions escaping workers, fix stale shader state across reloads, validate framebuffer resizes before caching them, and install `glfwSetErrorCallback` + `GL_DEBUG_OUTPUT`.

**What the work actually found.** Two of the planned items turned out to be **latent rather than live**, and saying so changes what the milestone is worth:

- **#130 (stale uniform locations) has no caller.** The `Application` constructor loads each shader exactly once, so no reload path exists to go stale. This is a fix for what a hot-reload or context-recreate feature *would* hit — real, but not currently reachable.
- **#131 (moved-from shader state) likewise.** The move is exercised, but nothing inspects a stage after moving it.

Everything else was live. The headline items were not cosmetic: **every GL object in the engine was being deleted after the context that owned them had been destroyed**, an exception from a worker **took the whole process down** rather than one chunk, and **100% of GL errors were unattributable** — arriving at the end of a frame, from the wrong frame, for a call up to 30 lines earlier.

**Three defects were found that the milestone did not plan for**, all of them preconditions of the planned fixes:

- **`glMemoryBarrier` is a null GLAD function pointer on the development platform** — a latent crash. Surfaced when #151 moved the barrier to the read site, which is where the milestone's own plan wanted it.
- **The storage buffer had no allocation at all.** `convertBufferUsage` returned `GL_*_DRAW` for every buffer type, which is `GL_INVALID_ENUM` for `GL_SHADER_STORAGE_BUFFER`; the heightmap SSBO's `glBufferData` had been failing on every non-Apple build.
- **`terminateWindowing` could be called twice** once the destructor took ownership of it, so it is now idempotent.

**A recurring theme worth recording.** Four separate issues (#128, #142, #143, #147) turned out to be the *same* class of defect: code that treated a **refusal** as **success**. `resizeOffscreenTarget` refused a 0x0 allocation and the caller rendered into framebuffer 0; it rejected an incomplete framebuffer and cached the size anyway; it was unreachable from GLAD and the caller armed a callback over it; `getBufferSubData` failed and the caller consumed the untouched buffer. In every case the fix is the same shape — report the refusal, and make the caller act on it — and in every case a test that asserted only "no crash" passed regardless. The tests that actually caught them all read state back (`GL_FRAMEBUFFER_BINDING`, `glGetUniformLocation`, a drain counter) rather than inferring from the absence of an error.

Planned scope as planned:

**Shutdown Ordering**
- Destroy all GL resources before `glfwTerminate()` tears down the context.
- Make `ThreadPool` destruction discard the pending backlog instead of draining it, so quitting cannot hang for seconds.
- Stop exceptions escaping worker tasks into `std::terminate`.

**Reload & Move Semantics**
- Clear stale uniform locations across `Shader::load` / `loadCompute` so a reload does not write through dangling indices.
- Make a moved-from `OpenGLShader` stop reporting `isCompiled()` while its handle is zero.
- Validate the framebuffer resize *before* caching the new size, so a rejected resize does not permanently wedge the offscreen target.
- Register the framebuffer resize callback only after GLAD is loaded, and never let `resizeOffscreenTarget` throw from inside a GLFW C callback.

**Validation & Diagnostics**
- Reject zero-sized framebuffers and textures before they reach the driver; close the framebuffer/texture feedback loop.
- Fix `setTextureParameter` and `generateMipmaps` to honor the texture's actual type instead of hardcoding `GL_TEXTURE_2D_ARRAY`.
- Route texture loading through the same resource-path resolution shaders use.
- Install a `glfwSetErrorCallback` and enable `GL_DEBUG_OUTPUT`, so GL errors carry call-site attribution.
- Add top-level error handling in `main` so startup failures report rather than abort.

Issues: #128, #129, #130, #131, #141, #142, #143, #144, #146, #147, #148, #157

**Note on the issue list:** it names `opengl_renderer.cpp` throwing on an incomplete framebuffer as one of the six startup failures #144 must handle. That throw no longer exists — #142 removed it, because `resizeOffscreenTarget` is reached from a GLFW C callback where an escaping exception is undefined behaviour. Five of the six remain, and #144's handler now also covers the paths #142 and #143 introduced.

### M13 — Threading, Chunk Pipeline & Player Physics

This milestone fixes the chunk pipeline's concurrency hazards and the player's physics. The common thread is *uncertainty*: worker tasks capture a raw `TaskResult` reference into an `unordered_map`, the processing mutex provides ordering that it does not actually guarantee, `getPositionHighestY` resolves the wrong chunk for half of every chunk column, and `deltaTime` is unclamped, so one frame hitch teleports the player hundreds of blocks. Each of these is a correctness bug that reads as a random glitch.

**Physics**
- Fix gravity handling and add real collision response.
- Resolve the correct chunk in `getPositionHighestY` for the back half of every chunk, return a defined result for unstreamed chunks instead of snapping the player above the world, and honor the water surface plane.
- Clamp `deltaTime` so a single hitch cannot teleport the player.
- Run the physics update before rendering, so physics and rendering never disagree within a frame.

**Threading**
- Close the race window on `m_ProcessingPositions`.
- Stop worker tasks capturing a raw `TaskResult` reference into an `unordered_map`.
- Replace `m_ProcessingMutex` with something that provides real ordering guarantees, rather than the false confidence it gives today.
- Reduce `ThreadPool` queue allocations caused by `std::function`.

**Input**
- Remove the arbitrary angular snap on the first mouse event.

Issues: #14, #16, #48, #132, #133, #134, #137, #138, #139, #145, #158

### M14 — Renderer Abstraction & Backend Portability

This milestone finishes the job Milestone 6 started: the abstraction layer exists, but it leaks. `IRenderer` is a god-interface, the hot buffer paths use `dynamic_cast`, the `Texture` class reaches around its own abstraction, and the engine hard-requires OpenGL 4.3 despite having a complete CPU-noise path that works on 3.3. This milestone is the prerequisite for M19 — a new spatial structure should not be able to leak backend types into the engine.

**Interface Design**
- Split the `IRenderer` god-interface into focused interfaces, each sized to the subsystem that consumes it.
- Fix the leaky abstraction in the `Texture` class so it talks only through the interface.
- Make `Texture` / `Shader` move assignment behave correctly.
- Make the `io.h` interface static and non-instantiable.
- Have `blockTextureId` honor its `BlockNormal` parameter.
- Avoid `dynamic_cast` in hot buffer paths.

**Backends**
- Throw from `renderer_factory` for unsupported backends instead of returning something unusable.
- Complete the Metal and Vulkan backends that Milestone 6 left as build options only.
- Fall back to the CPU-noise path instead of hard-requiring OpenGL 4.3, so the engine runs on 3.3 contexts.

Issues: #33, #51, #54, #55, #60, #62, #64, #88, #156

### M15 — Streaming & Draw-Path Performance

This milestone does the actual optimization work, and it is the milestone where ordering matters most. Every item below is meaningless against a wrong frustum (M11) or an undefined resource lifecycle (M12), so this milestone is deliberately placed after both. The theme is hoisting invariant work out of the per-chunk loop and giving the streaming scheduler hysteresis instead of thrashing.

**Draw Path**
- Hoist shader binding out of the per-chunk render loop, and bind the texture once outside the upload loop.
- Sort draw calls front-to-back by distance to improve early-Z rejection.
- Stop doing two string hashes per uniform setter call.

**Chunk Streaming**
- Precompute spiral offsets and gate the spawn scan instead of walking the full grid each frame.
- Batch multiple chunks per heightmap compute dispatch.
- Consolidate the double bookkeeping of processing/processed chunks.
- Avoid the `shrink_to_fit` reallocation after vertex upload.
- Add chunk unload throttling and hysteresis so the scheduler stops churning at the render-distance boundary.

**Meshing**
- Implement greedy meshing 2.0, built on the vertex layout and shader constants settled in M16.

Issues: #45, #46, #47, #49, #50, #52, #53, #79, #87, #153

### M16 — Rendering Quality: Lighting, Water & Terrain

This milestone raises visual quality and gives the world enough variety to be worth looking at. It also cleans up the render pipeline's rough edges — magic numbers baked into shaders, a stale comment that no longer describes the fog being computed, and view depth smuggled through the alpha channel instead of a depth attachment. Those changes to the pipeline are also what make greedy meshing 2.0 in M15 tractable.

**Pipeline Cleanup**
- Single-source the shader magic numbers and share the enums instead of duplicating them across shaders.
- Replace the depth-packed-into-alpha hack with a real depth texture attachment.
- Fix the fog to use radial view distance rather than axial `clip.w`.
- Correct the `fog.frag` comment to describe the fog actually implemented.

**Lighting**
- Add ambient occlusion for voxels.
- Add a day/night cycle with sun lighting.

**Water & Terrain**
- Render transparent water with blending and animated UVs.
- Expand biome and block variety.
- Integrate terrain diffusion behind a pluggable `HeightProvider` interface, so terrain shaping no longer requires editing the generator itself.

Issues: #57, #58, #59, #80, #81, #82, #83, #92, #152

### M17 — Gameplay: Interaction, Persistence & UI

This milestone turns the engine into something you can actually play with. It depends on M13 for a collision query that resolves correctly and on M16 for a world with biomes, water, and enough block variety to interact with. The scope here is deliberately modest: block editing that persists, real voxel collision, a config file, and a small amount of UI polish.

**Interaction & Physics**
- Add block breaking and placing via raycast.
- Implement real collision physics against the voxel grid.

**Persistence**
- Persist edited chunks to disk so world changes survive a restart.
- Add a settings/config file.

**UI & Input**
- Wire up scroll-wheel FOV zoom.
- Add sound and a basic HUD.
- Add a screenshot and keybind system.

Issues: #77, #78, #84, #85, #86, #90, #91

### M18 — Documentation, Licensing & Code Hygiene — SHIPPED

**Status: shipped.** See the Milestone 18 section of the [README](../README.md#implemented-features) for the full write-up.

As planned, this milestone changed no engine behaviour and was therefore worked **in parallel with M10** rather than after it — the two touched disjoint files, which is what made the parallelism safe rather than merely possible. M10 owned `CMakeLists.txt`, `ci.yml`, the `justfile`, `tests/`, and the README build sections; M18's documentation items were ordered first precisely so the two streams would not collide, with the source-level hygiene items (#61, #56) last.

One planned item turned out to be already done and was closed with evidence: **#75**, the doubled slash in the README screenshot path, was corrected in commit `6408afe`.

Two findings worth recording, because both contradicted the milestone's own premise:

  - **#56's naming inconsistency was two identifiers.** 75 of the 77 `m_`-prefixed members already used PascalCase; the outliers were `m_firstFrame` and `m_lastFrame`. Functions, parameters, locals, classes, and constants were already consistent. The change is a two-identifier rename plus a `.clang-tidy` that declares the convention — not the repo-wide reformat the issue title implies.
  - **#76's premise was partly stale.** The Windows instructions were not referencing SDL3 (that was removed in M9); they were simply missing the vcpkg toolchain file and the triplet, which is why they could not have worked.

Historical scope as planned:

**Documentation & Licensing**
- Add a LICENSE file at the repo root.
- Record doctest's provenance and license alongside the vendored copy.
- Flesh out `CONTRIBUTING.md`.
- Fix the README's documented files, versions, and flags that do not exist (#159), plus the double slash in the screenshot path.
- Update the Windows build instructions to current dependencies.

**Code Hygiene**
- Adopt consistent naming conventions and commit `clang-format` / `clang-tidy` configs.
- Remove dead and commented-out code.
- Remove the unused `TaskResult::pass()` wrapper.

Issues: #56, #61, #63, #70, #71, #72, #75, #76, #159

### M19 — Spatial Partitioning & LOD

This milestone replaces the flat chunk map with a sparse voxel octree and adds level-of-detail management. It is last because it is the most structurally invasive change in the roadmap, and because both of its prerequisites are load-bearing: LOD selection is layered directly on top of the culling hierarchy, so it inherits every bug M11 fixes and multiplies the blast radius by the number of levels; and the abstraction layer has to be clean enough first (M14) that a new spatial structure does not become a vector for leaking backend types into the engine.

**Scope**
- Introduce a sparse voxel octree replacing the flat `unordered_map`-keyed chunk storage.
- Add LOD selection and management across octree levels.
- Preserve correct streaming, culling, and meshing behaviour through the new structure.

Issues: #6

---

## Suggested execution order

Work the milestones in numeric order. The numbering is not arbitrary; each number is the earliest point at which the work is verifiable or correct.

1. **M10 first.** It is pure enabling work with no dependencies of its own, and it is the only milestone that makes the rest verifiable. The headless smoke-test executable is what will prove that M11's fixes actually fixed something. **Shipped.**
2. **M11 second.** It has the highest bug density in the roadmap and the fixes are individually small. Several of its issues are visible rendering errors that are currently shipped — wrong culling, invisible faces, chunks rendering other chunks' terrain — so it also has the highest visible payoff. **Shipped.** What it actually found was worse than the plan claimed: two of the three GPU-path defects were total rather than conditional, so on every non-Apple build the terrain path produced the heightmap of whichever chunk was dispatched last.
3. **M12 third.** Establishes defined resource ownership and real error reporting before any further work churns GPU state. Doing this before the performance work is what stops M15 from turning leaks into mysteries. **Shipped.** What it found was narrower and more systematic than planned: four of its issues were the same defect — a refusal treated as a success — and two of its twelve were latent rather than live.
4. **M14 alongside M12.** These two touch different files and share no issues, so they parallelize cleanly. **M12 is now shipped; M14 is the only remaining milestone with nothing outstanding above it.**
5. **M13 next.** The physics and concurrency fixes need M11's terrain correctness and M12's error reporting to be trustworthy; they are also best verified with the M10 smoke test watching for regressions.
6. **M16 next.** Settles the render pipeline — stable vertex layout, single-sourced constants, real depth attachment — which is the input greedy meshing depends on.
7. **M17.** Gameplay, on top of fixed physics and a varied world.
8. **M15.** Performance work, once the frustum is right and the resource lifecycle is defined. Every measurement taken before this point is unreliable.
9. **M19.** Last. SVO plus LOD, on correct culling and a clean abstraction.

**Parallelization.** M18 has no dependencies and carried no risk of blocking; it is shipped. M14 runs alongside M12 for the same reason, and is still available to start — with M11 done, **M12 and M14 are now the two milestones with nothing outstanding above them.**

**What M12's landing changed.** M15 loses its last remaining blocker: it depended on M11 (correct frustum) and M12 (defined resource lifecycle), and both are now done — so **M15 is unblocked**. M16 was blocked on M12 alone and is now unblocked too, which makes M13, M15, M16 and M17 available, with M17 still waiting on M13 and M16 both. M14 remains independent of everything and can run in parallel.

**What M11's landing changed.** M13's stated blocker ("`getPositionHighestY` resolves the wrong chunk for half of every chunk column, so physics work lands on an unreliable terrain query") is satisfied as of this milestone: the terrain query now reads back the chunk that actually wrote the slot. M13 remains blocked on M12 only, for the error-reporting dependency. M15 and M19 both lose M11 from their dependency list, leaving M15 blocked on M12 alone.

**Do not start early, and why.**
- ~~**Do not begin M13 before M11.**~~ Satisfied. The terrain query is correct as of M11, though note that M11 proved the property by construction and measurement rather than by rendering — the GPU path is off on Apple, so the terrain correctness M13 now depends on is verified on Linux CI.
- **Do not begin M15 before M12.** Performance numbers taken against a broken frustum or an undefined resource lifecycle are not data — they are noise that will be optimized against.
- **Do not begin M17 before M13 and M16.** Block placement needs a correct collision query, and a world worth interacting with.
- **Do not begin M19 before M11 and M14.** LOD inherits every culling bug and multiplies its severity per level, and it should not be built on an abstraction that still leaks backend types.
