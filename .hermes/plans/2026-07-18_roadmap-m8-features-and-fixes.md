# Linterra — Post-M8 Features & Bug Fixes (Subagent Roadmap)

> **For Hermes:** Execute via subagent-driven-development. Reviewed base = `7e2ff58`
> ("refactor: split shaders into pieces (m8)", body: `Reviewed by Amodh Dhakal`).
> Reconciliation rule: when a lane regresses, run `git diff 7e2ff58 <branch>` or
> `git diff 7e2ff58 -- <file>` — NEVER a bare `git diff` of the working tree.

**Goal:** Add the highest-value gameplay/rendering features and fix latent bugs,
parallelized across subagents that own *disjoint file sets*.

**Baseline (verified):** `linterra` + `linterra_tests` both build; 553/553 assertions pass;
tree clean at `7e2ff58`. So no breakage exists yet — all items are additive or corrective.

**Reconciliation command (per lane, when something goes wrong):**
```bash
git diff 7e2ff58 feature/m8-<lane>            # whole lane delta vs reviewed commit
git diff 7e2ff58 -- src/chunk.cpp             # single-file delta
git log --oneline 7e2ff58..feature/m8-<lane>  # commit-by-commit
```

---

## Phase 0 — Foundation (SEQUENTIAL, 1 subagent, ~15 min)
Establishes the shared vocabulary so parallel lanes only *read* `config.h`.
- Create branch `feature/m8-foundation` from `7e2ff58`.
- Fix `DEAULT_PITCH` → `DEFAULT_PITCH` in `src/config.h:47-48` + `src/camera.cpp:21`.
- Remove dead `VERTEX_PATH`/`FRAGMENT_PATH` (`src/config.h:19-20`).
- Remove unused `MAX_GENERATION_THREADS` (`src/config.h:69`).
- Add a constants block: `WATER_LEVEL`, `BIOME_SCALE`, `CAVE`, `ORE` params,
  new `RenderDistance` option, texture-slot enums.
- Generate missing texture assets under `resources/blocks/` (grass_side.png,
  grass_bottom.png, stone.png, dirt_side.png, water.png) via a stdlib-only
  PNG writer (zlib+struct) — solid colors, no external deps.
- Commit: `chore: foundation constants + asset stubs`.

## Parallel Lanes (dispatch after Foundation; ≤3 concurrent per max_concurrent_children)
Each lane = its own `feature/m8-<lane>` branch from `7e2ff58`, disjoint file ownership.

### Lane A — Terrain & World Gen  (owns: src/chunk.cpp, src/chunk.h, tests/terrain*)
- F1 Caves (3D noise threshold on block emission)
- F2 Biome system (biome noise → amplitude/palette per chunk)
- F3 New block types + side/bottom textures (extend `BlockType`, `blockTextureId`)
- F4 Ore distribution (separate noise, replaces stone in bands)
- F5a Water: emit WATER blocks below `WATER_LEVEL` (translucent handled in Lane D)

### Lane B — Player & Physics  (owns: src/player.cpp)
- F6 Terrain collision (horizontal + ground, sample heightmap)
- F7 Enable gravity by default + tuned jump (`config.h` read)
- F8 Double jump
- F9 Sprint (Shift modifier already wired; extend)

### Lane C — Camera & Frustum  (owns: src/camera.cpp, src/frustum.cpp)
- F10 Scroll-wheel FOV zoom (camera math; application wiring left to Lane D)
- F11 Dynamic aspect ratio on resize (fixes B5 — `m_Aspect` frozen at ctor)
- F17 Vertical/back-face frustum culling refinement

### Lane D — Render Pipeline & Integration  (owns: shaders/*, src/framebuffer.*, src/application.cpp, src/texture.cpp)
- F5b Water translucent blend + bind WATER texture
- F12 Exponential fog (match README claim)
- F13 Invert fog (config toggle)
- F14 Crosshair overlay (fullscreen-tri + tiny VAO, or 2nd draw)
- F15 Resolution scaling for perf (Framebuffer resize factor)
- F16 Sky gradient vs flat clear
- B5 application aspect wiring, B8 abstraction cleanup (replace raw `glBindFramebuffer`/`glViewport` with `m_Renderer->`)

### Lane E — Health, Tests & Docs  (owns: src/config.h [post-foundation], src/threadpool.*, README.md, tests/*, .github/workflows/ci.yml)
- B1/B7 config cleanup (done in foundation; verify)
- F18 Expand unit tests (camera aspect, frustum edge cases, noise, threadpool)
- F19 Update README milestones M9
- F20 Dead-code/abstraction cleanup

## Review gates (per skill)
Each task: implementer subagent → spec-compliance reviewer → code-quality reviewer.
Proceed only when both PASS. Final integration reviewer over all lanes.

## Risks
- `application.cpp` is the convergence point → owned exclusively by Lane D to avoid
  parallel-edit conflicts (hard rule: never two subagents edit the same file).
- `config.h` edited only by Foundation (seq) then Lane E (after Foundation) — never concurrent.
- GL-context subsystems (chunk.cpp, application.cpp, texture.cpp, framebuffer.cpp)
  CANNOT be unit-tested headless; tests stay limited to noise/camera/frustum/threadpool.
