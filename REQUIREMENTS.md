Requirements Document: Linterra Voxel Engine — Performance & Quality Fixes
Overview
Address performance bottlenecks and code quality issues in the chunk generation pipeline.

---

PART A: Performance Optimizations
OPT-1: Pre-compute Extended Heightmap for Border Lookups
File: src/chunk.cpp
Problem:  
getNeighborHeight() (chunk.cpp:31-44) calls Noise::fbm() on every border block access. At chunk boundaries this causes repeated expensive noise calculations.
Proposed solution:

1. Before mesh generation, compute a (LENGTH+1) x (LENGTH+1) extended heightmap with padding row/column for all 4 neighbors
2. Replace getNeighborHeight() with direct m_ExtendedHeightMap[nx][nz] lookup
3. isBlockExposed() becomes a simple O(1) array access
   Acceptance criteria:

- [ ] getNeighborHeight() lambda removed
- [ ] Extended heightmap computed once per chunk before mesh gen
- [ ] isBlockExposed() uses array lookup only

---

OPT-2: Extract Chunk Distance Calculation to Shared Helper
Files: src/manager.cpp, src/manager.h
Problem:  
Distance to camera computed twice — once for culling (lines 21-40) and once for spawn checks (lines 66-105). Same center/distance math duplicated.
Proposed solution:

1. Add getChunkDistanceSquared(glm::vec2 chunkPos, const glm::vec3& cameraPos) helper in manager.cpp
2. Add constexpr cached values for chunk center offset math
3. Replace both duplication sites with single function call
   Acceptance criteria:

- [ ] Distance calculation happens once per chunk position
- [ ] No duplicate center/distance math in render()

---

OPT-3: Move GPU Buffer Upload Off Main Thread
Files: src/manager.cpp, src/chunk.cpp, src/chunk.h, src/manager.h
Problem:  
pass() (chunk.cpp:213-242) calls OpenGL functions synchronously on the main thread, stalling render while chunks are promoted.
Proposed solution (Option A — Double-buffer with async commit):

1. Add std::atomic<bool> uploadReady to TaskResult
2. Worker thread: compute mesh → signal ready → signal uploadReady
3. Main thread: if uploadReady, call pass() to commit to GPU
4. Chunk retains pre-uploaded vertex/index vectors until GPU commit
   Changes needed:

- manager.h: add std::atomic<bool> uploadReady to TaskResult
- chunk.cpp: pass() stays same, called on main thread
- manager.cpp: move pass() after upload ready check, worker sets uploadReady
  Acceptance criteria:
- [ ] pass() called on main thread only when data is ready
- [ ] No GL calls in worker thread context

---

PART B: Bug Fixes
BUG-1: Race Condition on m_ProcessingPositions
File: src/manager.cpp, src/manager.h
Problem:  
m_ProcessingPositions (unordered_set) accessed unsafely — main thread reads contains() at line 94 while worker threads may be inserting via the lambda at line 98.
Proposed solution:

1. Add std::mutex m_ProcessingMutex to ChunkManager
2. Protect all accesses to m_ProcessingPositions and m_ProcessingChunks with lock scope
3. Use std::lock_guard for RAII-style mutex management
   Acceptance criteria:

- [ ] m_ProcessingPositions access is thread-safe
- [ ] No data races between main and worker threads

---

PART C: Code Quality Fixes
CQ-1: Typos in Config and Player
Files: src/config.h, src/player.h
Location
config.h:23
config.h:33
player.h:55
Proposed solution:

1. Rename in config.h (line 23): JUMP_VELOCTY → JUMP_VELOCITY
2. Rename in config.h (line 33): DEAULT_PITCH → DEFAULT_PITCH
3. player.h:55 uses Constants::Camera::JUMP_VELOCTY — will auto-fix if config.h is corrected
   Acceptance criteria:

- [ ] All typos corrected
- [ ] Code compiles and runs with renamed constants

---

CQ-2: Type Inconsistency — Standardize on <cstdint>
Files: src/config.h, src/chunk.h, src/chunk.cpp, src/player.h, src/application.cpp
Problem:  
Inconsistent use of type aliases:

- config.h uses: uint, int, float
- chunk.h uses: uint32_t, uint8_t, ushort
- texture.cpp uses: unsigned int
- player.h uses: mixed
  Proposed solution:

1. Audit all .h and .cpp files for type usage
2. Standardize on <cstdint> types:
   - uint8_t instead of uint / unsigned char
   - uint32_t instead of uint / unsigned int
   - int32_t instead of int
   - uint16_t for ushort / uint16_t
3. Remove <sys/types.h> where not needed (use <cstdint> instead)
   Files to audit:

- config.h
- chunk.h
- chunk.cpp
- player.h
- application.cpp
- manager.cpp
- texture.cpp
  Acceptance criteria:
- [ ] All source files use <cstdint> types consistently
- [ ] No bare uint, int, unsigned int outside of third-party headers

---

CQ-3: Player Implementation in Header — Violates ODR
File: src/player.h
Problem:  
All Player methods (~130 lines) are implemented inline in the header file. This causes:

- Code bloat at every inclusion point
- Potential ODR violations if header is included across translation units differently
- Slower compilation (header parsed repeatedly)
  Proposed solution:

1. Move all Player method implementations from player.h to player.cpp
2. Keep declarations in header, implementations in source file
3. Remove inline keyword (implicit for class methods, but explicit is fine)
   Acceptance criteria:

- [ ] player.h contains only declarations (class definition, method signatures)
- [ ] player.cpp contains all method implementations
- [ ] Code compiles and runs identically

---

CQ-4: Dead Code Cleanup
Files: src/application.cpp, src/image.cpp
Problem:

- application.cpp:136-137: Commented-out scroll callback code (TODO)
- image.cpp: 6 lines, completely unused (stb_image via vendor)
  Proposed solution:

1. Remove commented-out scroll code in application.cpp
2. Either delete image.cpp entirely or document why it exists
3. If image.cpp is intentional stub, add // TODO: implement comment
   Acceptance criteria:

- [ ] No commented-out dead code in application.cpp
- [ ] image.cpp either deleted or has documented purpose

---

CQ-5: Missing RAII Wrappers for OpenGL Resources
Files: src/shader.h, src/chunk.h, src/texture.h
Problem:  
Raw OpenGL IDs managed manually:

- Texture has destructor (good)
- Shader has no destructor — GL program may leak
- Chunk has manual cleanup() method — error-prone, must call manually
  Proposed solution:

1. Add RAII wrapper for OpenGL shader programs in Shader class
2. Add destructor to Shader that calls glDeleteProgram
3. Keep Chunk::cleanup() but add RAII-style GLChunk wrapper if appropriate
4. Consider std::unique_ptr for OpenGL resource management
   Acceptance criteria:

- [ ] Shader destructor calls glDeleteProgram(m_Id)
- [ ] All OpenGL resources cleaned up automatically on destruction

---
