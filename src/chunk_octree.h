#pragma once

// Sparse voxel octree over chunk coordinates.
//
// WHAT THIS REPLACES
//
// ChunkManager kept every meshed chunk in a flat
// std::unordered_map<glm::ivec2, Chunk> and every in-flight meshing task in a
// second flat std::unordered_map<glm::ivec2, TaskResult>, with a flat
// std::unordered_set<glm::ivec2> of positions that have a task outstanding. A
// hash map has no spatial structure, so the only way to ask "which chunks are
// near the camera" was to visit all of them, and the per-frame spawn scan had no
// choice but to walk a fixed (2 * RENDER_DISTANCE_CHUNKS + 1)^2 grid -- 65 x 65
// = 4225 positions, every frame, whether or not the player had moved.
//
// The tree gives that question a structure. A node owns a square cell of
// 2^level chunk coordinates, so "is this cell anywhere near the camera" is one
// box test, and a subtree that is nowhere near the camera is dropped without
// touching the chunks under it. A node also carries aggregates -- how many
// leaves are resident below it, and whether every chunk coordinate in its cell
// is present -- so a region that is already fully streamed costs one test
// instead of one test per chunk.
//
// DELIBERATELY THE SAME SHAPE AS THE CONTAINERS IT REPLACES
//
// The map-like operations keep the names the unordered_map had, so the swap in
// ChunkManager is a change of type rather than a rewrite:
//
//   m_ProcessedChunks.contains(p)    ->  octree.contains(p)
//   m_ProcessedChunks.find(p)        ->  octree.find(p)           (T*)
//   m_ProcessedChunks.try_emplace    ->  octree.emplace(p, ...)   (T*)
//   m_ProcessedChunks.erase(p)       ->  octree.erase(p)          (size_t)
//   for (auto &e : map) e.first/.second
//                                  ->  octree.forEachValue(fn)
//   for (auto it = map.begin(); ...) erase(it)
//                                  ->  octree.forEachEntry(fn)   (fn -> keep?)
//
// That is deliberate. Those containers are being rewritten concurrently
// elsewhere in the tree, and a container swap is a much smaller conflict
// surface than a rewrite of every loop that touched them.
//
// WHY THE TOP LEVEL IS A HASH MAP
//
// Chunk coordinates are int32 and unbounded: the player can walk a long way from
// the origin. A single fixed-root octree would have to be re-anchored -- and
// therefore rebuilt -- whenever the camera left the root cell, which puts a
// several-thousand-entry rebuild spike on a periodic frame. Instead each
// 2^kSubtreeLevels cell of chunk coordinates is an independent subtree, and the
// map from cell to subtree is a hash map. The world is unbounded, every lookup
// is exactly kSubtreeLevels + 1 hops, and walking costs nothing extra.
//
// A QUADTREE, NOT AN OCTREE
//
// The issue asks for an octree. An octree splits a cube in three and so has
// eight children; this tree is keyed on glm::ivec2, because the world index is
// two-dimensional (Y is a column height, not an axis), so a cell is a square
// and splits in two. Four children. The structure, the traversal, the
// aggregates and the pruning are all as the issue describes; only the fan-out
// is halved.
//
// WHY kSubtreeLevels == 6
//
// A top-level cell is 2^6 = 64 chunk coordinates = 64 * LENGTH = 1024 world
// blocks across, twice the 512-block render distance, so the 65 x 65 chunk
// streaming window sits inside one to four top-level cells and the top level
// almost never prunes anything on its own. Six is a compromise between descent
// length (one fewer level is one fewer dependent load on every point lookup) and
// per-cell overhead.
//
// THE TRADE IS NOT ONE-SIDED
//
// A point lookup is 7 dependent pointer hops here against one hash probe and one
// bucket walk in the flat map. So tryGetGroundHeight -- the player/collision
// query, on the latency-sensitive path -- gets slower, and the per-frame
// traversals get much cheaper. That is a real cost, stated here rather than
// discovered later, and it is the reason locate() is worth keeping a small.
//
// MEMORY
//
// A node owns its children, so a subtree frees itself recursively. There is no
// recycling pool: a release is a delete. That is one allocation per newly
// created cell during streaming, which is what the unordered_map it replaces
// was already doing per entry, and it keeps the ownership rule simple enough to
// be obviously correct -- which matters because a traversal holds pointers into
// the tree while it removes things from it.
//
// ERASE-DURING-ITERATION
//
// forEachEntry() removes nodes while a walk is in progress. It therefore only
// clears the value and queues the leaf; the free happens after the walk, so no
// node the walk still holds a pointer to is released underneath it. Every node
// carries a parent pointer so the queue can prune upward without replaying a
// path. eraseSubtrees() does not need this: it only ever frees a subtree it has
// finished with, and nulls the slot immediately.
//
// Deliberately free of OpenGL and of any ChunkManager state, so the data
// structure can be exercised by the unit suite, which has no GL context --
// templated on the payload, so the tests instantiate it with an int.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

template <class T> class ChunkOctree {
public:
  using ChunkCoord = glm::ivec2;

  // Levels below a top-level cell. A level-0 cell is a single chunk; a
  // top-level cell is 2^kSubtreeLevels chunks per axis.
  static constexpr int kSubtreeLevels = 6;
  static constexpr int kSubtreeSide = 1 << kSubtreeLevels;

  // What the caller wants done with a cell the walk has reached.
  enum class Action {
    Descend,
    // Drop the whole subtree: no further visits, and the chunks below are
    // neither drawn nor offered for spawning.
    Skip,
  };

  /** One cell of the tree.
   *
   *  `origin` is the lowest chunk coordinate the cell covers and `level` fixes
   *  its side: 1 << level chunk coordinates per axis. A level-0 node holds a
   *  value; a node above that holds children and aggregates. */
  struct Node {
    glm::ivec2 origin{0, 0};
    std::uint32_t level = 0;
    // Chunks resident anywhere below this node. For a level-0 node this is 1
    // exactly when the value is present.
    std::uint32_t chunkCount = 0;
    // True when every chunk coordinate in this cell is resident. A level-0 node
    // is complete when it holds a value; above that all four children must exist
    // and all four be complete.
    bool complete = false;
    // The cell above, or null for a top-level node. Present so an erase can
    // prune upward without replaying a descent path.
    Node *parent = nullptr;
    // FOUR children, not eight. The issue calls this an octree, and an octree
    // has eight because it splits a cube in three. This tree is keyed on
    // glm::ivec2 -- the world is 2D in the index, with Y being a column height
    // rather than an axis -- so a cell is a square and splits in two. Sizing the
    // array at eight leaves four slots permanently null, and every "all
    // children present" test then fails forever, which silently turns the
    // completeness optimisation off: correct, but 4225 cells per frame.
    static constexpr std::size_t kChildCount = 4;
    std::array<std::unique_ptr<Node>, kChildCount> children{};
    std::optional<T> value;

    [[nodiscard]] int side() const { return 1 << static_cast<int>(level); }
    [[nodiscard]] bool hasValue() const { return value.has_value(); }
    [[nodiscard]] bool hasChildren() const {
      return std::any_of(children.begin(), children.end(),
                         [](const std::unique_ptr<Node> &child) {
                           return child != nullptr;
                         });
    }
  };

  /** What a traversal cost. Counted rather than guessed: these are the numbers
   *  the per-frame spawn and eviction work is measured in. */
  struct WalkStats {
    // Single-chunk cells handed to the visitor -- the direct equivalent of one
    // iteration of the old per-chunk grid scan.
    std::size_t cellsVisited = 0;
    // Whole cells reported as entirely absent, each standing for 4^level
    // chunks. This is where an unstreamed region costs O(1) per cell instead of
    // O(chunks).
    std::size_t emptyCellsReported = 0;
    // Nodes examined, including the ones the visitor then skipped.
    std::size_t nodesVisited = 0;
    // Cells dropped whole because the visitor said Skip.
    std::size_t subtreesSkipped = 0;
  };

  ChunkOctree() = default;
  ChunkOctree(const ChunkOctree &) = delete;
  ChunkOctree &operator=(const ChunkOctree &) = delete;

  [[nodiscard]] std::size_t size() const { return m_Size; }
  [[nodiscard]] bool empty() const { return m_Size == 0; }

  /** Drop everything, destroying every value. */
  void clear() {
    m_Top.clear();
    m_EraseQueue.clear();
    m_Size = 0;
    m_LiveNodes = 0;
  }

  /** True when a chunk is resident here. O(kSubtreeLevels). */
  [[nodiscard]] bool contains(const ChunkCoord &position) const {
    const Node *node = findNode(position);
    return node != nullptr && node->hasValue();
  }

  [[nodiscard]] T *find(const ChunkCoord &position) {
    return findNode(position) != nullptr ? &*findNode(position)->value : nullptr;
  }

  [[nodiscard]] const T *find(const ChunkCoord &position) const {
    const Node *node = findNode(position);
    return node != nullptr ? &*node->value : nullptr;
  }

  /** Insert a value if the chunk is absent, and return a pointer to it either
   *  way. This is the unordered_map's try_emplace: the same "existing or new"
   *  contract, but returning the pointer directly because the tree has no
   *  iterator to hand back.
   *
   *  The returned pointer stays valid until the value is erased or the tree is
   *  cleared. That is the stability the map gave and the manager depends on it:
   *  a worker task captures `TaskResult &` and dereferences it long after the
   *  enqueue call returned. */
  template <class... Args>
  T *emplace(const ChunkCoord &position, Args &&...args) {
    if (T *existing = find(position)) {
      return existing;
    }

    const glm::ivec2 top = topCellFor(position);
    auto &root = m_Top[topKey(top.x, top.y)];
    if (root == nullptr) {
      root = std::make_unique<Node>();
      root->origin = glm::ivec2(top.x * kSubtreeSide, top.y * kSubtreeSide);
      root->level = static_cast<std::uint32_t>(kSubtreeLevels);
      root->parent = nullptr;
      m_LiveNodes += 1;
    }

    Node *node = root.get();
    for (int childLevel = kSubtreeLevels; childLevel > 0; --childLevel) {
      const int index = childIndex(position, childLevel);
      std::unique_ptr<Node> &slot =
          node->children[static_cast<std::size_t>(index)];
      if (slot == nullptr) {
        slot = std::make_unique<Node>();
        slot->origin =
            node->origin +
            glm::ivec2((index & 1) << (childLevel - 1),
                       ((index >> 1) & 1) << (childLevel - 1));
        slot->level = static_cast<std::uint32_t>(childLevel - 1);
        slot->parent = node;
        m_LiveNodes += 1;
      }
      node = slot.get();
    }

    node->value.emplace(std::forward<Args>(args)...);
    node->complete = true;
    ++m_Size;
    updateAggregates(node, /*added=*/true);
    return &*node->value;
  }

  /** Remove a chunk, destroying its value. Returns 1 if it was there. */
  std::size_t erase(const ChunkCoord &position) {
    Node *node = findNode(position);
    if (node == nullptr || !node->hasValue()) {
      return 0;
    }
    node->value.reset();
    --m_Size;
    // Every ancestor's aggregates describe this value, so every one of them has
    // to hear about it -- not just the nearest. pruneUpward below stops at the
    // first ancestor that still has other children, and folding the aggregate
    // update into it left every ancestor above that point reporting a count
    // that was one too high and a cell that still claimed to be complete.
    updateAggregates(node, /*added=*/false);
    if (m_WalkDepth > 0) {
      // A traversal is holding a pointer into this subtree. Defer the free, or
      // the walk would be standing on a deleted node when it returns.
      m_EraseQueue.push_back(node);
    } else {
      pruneUpward(node);
    }
    return 1;
  }

  /** Visit every resident chunk in a deterministic order: top-level cells
   *  ascending, then child index 0..7 within each cell. */
  template <class F> std::size_t forEachValue(F &&fn) const {
    std::size_t visited = 0;
    for (const uint64_t key : sortedKeys()) {
      const auto it = m_Top.find(key);
      if (it != m_Top.end()) {
        visited += forEachValueImpl(it->second.get(), fn);
      }
    }
    return visited;
  }

  /** Visit every resident chunk, letting the visitor say whether to keep it.
   *
   *  fn(const glm::ivec2 &position, T &value) -> bool. Returning false erases
   *  the entry. This is the promote loop's shape: it promotes or drops entries
   *  as it walks, which a begin()/erase(it) loop expresses and a callback
   *  expresses without an iterator that can be invalidated mid-walk.
   *
   *  Erasures are queued and applied after the walk. */
  template <class F> std::size_t forEachEntry(F &&fn) {
    std::size_t visited = 0;
    ++m_WalkDepth;
    for (const uint64_t key : sortedKeys()) {
      const auto it = m_Top.find(key);
      if (it != m_Top.end()) {
        visited += forEachEntryImpl(it->second.get(), fn);
      }
    }
    --m_WalkDepth;
    if (m_WalkDepth == 0) {
      for (Node *node : m_EraseQueue) {
        pruneUpward(node);
      }
      m_EraseQueue.clear();
    }
    return visited;
  }

  /** Walk the chunk coordinates in [lo, hi) and report what is not already
   *  there.
   *
   *  This replaces the old spawn loop's unconditional walk of the
   *  (2 * RENDER_DISTANCE_CHUNKS + 1)^2 grid, and it has to answer the same
   *  question: which positions in the window have no meshed chunk? Two
   *  properties make that affordable, and both come from the tree:
   *
   *  1. A cell that already holds every chunk in it is reported once and
   *     skipped whole. A settled region costs a handful of cell tests instead
   *     of one hash lookup per chunk per frame.
   *  2. A cell with no subtree at all is reported ONCE for the whole cell, not
   *     once per chunk. An unstreamed region is the common case on a cold start
   *     and the old loop spent 4225 iterations discovering it.
   *
   *  visit(int level, const glm::ivec2 &origin, const Node *node) -> Action runs
   *  once per cell reached, before descending. Skip drops the subtree. `node`
   *  is null when no subtree exists there.
   *
   *  fn(int level, const glm::ivec2 &origin, const T *value) then reports:
   *    - level == 0: this one chunk, with `value` null if it is not resident.
   *    - level  > 0: the whole cell is unstreamed. `origin` is its lowest chunk
   *      coordinate, it covers 2^level x 2^level chunks, and none of them is
   *      resident. `value` is always null.
   *
   *  The walk is driven by the REGION, not by the tree: the top-level cells
   *  overlapping [lo, hi) are enumerated whether or not they have a subtree.
   *  Driving it from the tree instead -- iterating only the top-level cells that
   *  exist -- looks equivalent and is not. On a cold start, or anywhere outside
   *  the streamed area, the tree is empty there, so a tree-driven walk reports
   *  nothing at all, and a spawn scan that cannot see unstreamed terrain never
   *  streams anything.
   *
   *  Deterministic: top-level cells ascending, then child index 0..7. */
  template <class P, class F>
  WalkStats forEachCellIn(const glm::ivec2 &lo, const glm::ivec2 &hi, P &&visit,
                          F &&fn) const {
    WalkStats stats;
    if (hi.x <= lo.x || hi.y <= lo.y) {
      return stats;
    }
    // Half-open [lo, hi) mapped onto the top-level grid. hi - 1 is the last
    // coordinate in the range, so the last cell index is floorShift(hi - 1).
    const int lastX = floorShift(hi.x - 1, kSubtreeLevels);
    const int lastZ = floorShift(hi.y - 1, kSubtreeLevels);
    for (int cellX = floorShift(lo.x, kSubtreeLevels); cellX <= lastX; ++cellX) {
      for (int cellZ = floorShift(lo.y, kSubtreeLevels); cellZ <= lastZ;
           ++cellZ) {
        const glm::ivec2 origin{cellX * kSubtreeSide, cellZ * kSubtreeSide};
        if (!cellsOverlap(lo, hi, kSubtreeLevels, origin)) {
          continue;
        }
        const auto it = m_Top.find(topKey(cellX, cellZ));
        forEachCellInImpl(lo, hi,
                          it != m_Top.end() ? it->second.get() : nullptr,
                          kSubtreeLevels, origin, visit, fn, stats);
      }
    }
    return stats;
  }

  /** Erase every value in the subtrees the visitor selects.
   *
   *  visit(int level, const glm::ivec2 &origin) -> bool returns true to evict
   *  that whole cell. onEvict(T &value) runs for each value before it is
   *  destroyed -- that is where a GL resource release belongs, because the
   *  octree knows nothing about VBO/VAO lifetimes.
   *
   *  A cell may only be selected for eviction if every chunk in it is being
   *  evicted, which is the caller's business to guarantee: evicting a subtree
   *  whose parent is still wanted frees the parent too. */
  template <class P, class F> std::size_t eraseSubtrees(P &&visit, F &&onEvict) {
    std::size_t evicted = 0;
    // Collected first: erasing a top-level entry invalidates the iterator.
    std::vector<uint64_t> emptied;
    for (const auto &entry : m_Top) {
      if (visit(static_cast<int>(entry.second->level), entry.second->origin)) {
        evicted += destroyDescendants(entry.second.get(), onEvict);
        emptied.push_back(entry.first);
        continue;
      }
      evicted += eraseSubtreeImpl(entry.second.get(), visit, onEvict);
    }
    for (const uint64_t key : emptied) {
      m_LiveNodes -= 1;
      m_Top.erase(key);
    }
    return evicted;
  }

  /** Live nodes, for measurement. */
  [[nodiscard]] std::size_t nodeCount() const { return m_LiveNodes; }
  [[nodiscard]] std::size_t topLevelCellCount() const { return m_Top.size(); }
  [[nodiscard]] int subtreeLevels() const { return kSubtreeLevels; }

  /** True when every chunk coordinate in the cell is resident. `level` is the
   *  level of the cell whose lowest chunk coordinate is `origin`. */
  [[nodiscard]] bool isCellComplete(int level, const glm::ivec2 &origin) const {
    if (level < 0 || level > kSubtreeLevels) {
      return false;
    }
    const Node *node = findNodeLevel(origin, level);
    return node != nullptr && node->complete;
  }

  /** Chunks resident below a cell. Zero for a cell with no node. */
  [[nodiscard]] std::size_t cellCount(int level, const glm::ivec2 &origin) const {
    if (level < 0 || level > kSubtreeLevels) {
      return 0;
    }
    const Node *node = findNodeLevel(origin, level);
    return node != nullptr ? node->chunkCount : 0;
  }

  // --- chunk-coordinate arithmetic, exposed because the traversal predicates
  // the manager writes are stated in terms of it -----------------------------

  /** The top-level cell a chunk coordinate belongs to, by floor division. */
  [[nodiscard]] static glm::ivec2 topCellFor(const glm::ivec2 &position) {
    return {floorShift(position.x, kSubtreeLevels),
            floorShift(position.y, kSubtreeLevels)};
  }

  /** The lowest chunk coordinate of the top-level cell holding `position`. */
  [[nodiscard]] static glm::ivec2 topCellOrigin(const glm::ivec2 &position) {
    return topCellFor(position) * kSubtreeSide;
  }

  /** Which of the four children of the cell at `parentLevel` holds `position`.
   *
   *  A cell at level L has side 2^L, so it splits at 2^(L-1) and the bit that
   *  names the child is bit (L - 1) of each coordinate. The parameter is
   *  therefore the PARENT's level, and getting that wrong is an off-by-one that
   *  clears the wrong child slot rather than failing to compile.
   *
   *  Two bits, one per axis. The y bit is the more significant one, which is
   *  arbitrary but has to be fixed: the child origin derived in emplace() reads
   *  the same index, so the two have to agree. */
  [[nodiscard]] static int childIndex(const glm::ivec2 &position,
                                      int parentLevel) {
    const int bit = 1 << (parentLevel - 1);
    return ((position.x & bit) != 0 ? 1 : 0) | ((position.y & bit) != 0 ? 2 : 0);
  }

  /** True when the half-open cell [origin, origin + 2^level) meets [lo, hi). */
  [[nodiscard]] static bool cellsOverlap(const glm::ivec2 &lo,
                                         const glm::ivec2 &hi, int level,
                                         const glm::ivec2 &origin) {
    const glm::ivec2 cellHi = origin + glm::ivec2(1 << level);
    return !(cellHi.x <= lo.x || origin.x >= hi.x || cellHi.y <= lo.y ||
             origin.y >= hi.y);
  }

  /** floor(a / 2^shift), without relying on the sign of a right shift.
   *
   *  A chunk at index p owns [p*L, (p+1)*L), so its index is a floor of the
   *  quotient; truncating instead would send the whole back half of the world
   *  into the wrong cell, which is the #132 bug one level up. Clearing the low
   *  bits and dividing is exact for two's-complement negatives. */
  [[nodiscard]] static int floorShift(int a, int shift) {
    const int mask = (1 << shift) - 1;
    return (a - (a & mask)) / (1 << shift);
  }

private:
  [[nodiscard]] static uint64_t topKey(int cx, int cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(cz));
  }

  [[nodiscard]] Node *parentOf(Node *node) const { return node->parent; }

  [[nodiscard]] static bool computeComplete(const Node *node) {
    if (node->level == 0) {
      return node->hasValue();
    }
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child == nullptr || !child->complete) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] Node *findNode(const glm::ivec2 &position) {
    const glm::ivec2 top = topCellFor(position);
    const auto it = m_Top.find(topKey(top.x, top.y));
    if (it == m_Top.end()) {
      return nullptr;
    }
    Node *node = it->second.get();
    for (int childLevel = kSubtreeLevels; childLevel > 0; --childLevel) {
      const auto &slot = node->children[static_cast<std::size_t>(
          childIndex(position, childLevel))];
      if (slot == nullptr) {
        return nullptr;
      }
      node = slot.get();
    }
    return node;
  }

  [[nodiscard]] const Node *findNode(const glm::ivec2 &position) const {
    const glm::ivec2 top = topCellFor(position);
    const auto it = m_Top.find(topKey(top.x, top.y));
    if (it == m_Top.end()) {
      return nullptr;
    }
    const Node *node = it->second.get();
    for (int childLevel = kSubtreeLevels; childLevel > 0; --childLevel) {
      const auto &slot = node->children[static_cast<std::size_t>(
          childIndex(position, childLevel))];
      if (slot == nullptr) {
        return nullptr;
      }
      node = slot.get();
    }
    return node;
  }

  // The same descent, stopping at an arbitrary level, for the cell-level
  // completeness query. `level` is the level of the cell whose origin is given.
  [[nodiscard]] Node *findNodeLevel(const glm::ivec2 &origin, int level) {
    const glm::ivec2 top =
        {floorShift(origin.x, kSubtreeLevels),
         floorShift(origin.y, kSubtreeLevels)};
    const auto it = m_Top.find(topKey(top.x, top.y));
    if (it == m_Top.end()) {
      return nullptr;
    }
    Node *node = it->second.get();
    for (int childLevel = kSubtreeLevels; childLevel > level; --childLevel) {
      const auto &slot = node->children[static_cast<std::size_t>(
          childIndex(origin, childLevel))];
      if (slot == nullptr) {
        return nullptr;
      }
      node = slot.get();
    }
    return node;
  }

  [[nodiscard]] const Node *findNodeLevel(const glm::ivec2 &origin,
                                          int level) const {
    return const_cast<ChunkOctree *>(this)->findNodeLevel(origin, level);
  }

  // Recompute every ancestor's aggregates after a leaf was added or removed.
  // Both quantities are derived, never incremented ad hoc, so they cannot drift
  // from the tree: a count that is one too high makes a cell look fuller than it
  // is, and a completeness flag stuck true makes the spawn scan skip a cell that
  // has a hole in it -- which is a chunk that never streams in.
  void updateAggregates(Node *from, bool added) {
    for (Node *up = from; up != nullptr; up = parentOf(up)) {
      if (added) {
        ++up->chunkCount;
      } else {
        --up->chunkCount;
      }
      up->complete = computeComplete(up);
    }
  }

  // Unlink one childless, value-less node from its parent. The unique_ptr reset
  // IS the delete for that node -- there is exactly one owner per node, and it
  // is the owning pointer, never this function.
  //
  // The parent's aggregates are NOT touched here: they describe the whole cell
  // and were already updated by updateAggregates over the full ancestor chain.
  void unlinkFromParent(Node *node) {
    Node *parent = node->parent;
    // The parent's level, because that is what childIndex takes: a parent cell
    // of side 2^L splits at 2^(L-1), so bit (L-1) of the coordinate names the
    // child. Off by one here and the wrong slot is cleared, leaving the node
    // parented-but-unreachable and accumulating silently over a long session.
    const auto index = static_cast<std::size_t>(
        childIndex(node->origin, static_cast<int>(parent->level)));
    parent->children[index].reset();
    m_LiveNodes -= 1;
  }

  // Prune upward from a node that no longer holds a value. Stops at the first
  // ancestor that still has a value or a child, so one erase is O(depth) in the
  // common case and O(nodes in the cell) when it empties a whole subtree.
  void pruneUpward(Node *node) {
    while (node != nullptr && !node->hasValue() && !node->hasChildren()) {
      Node *parent = node->parent;
      if (parent == nullptr) {
        // A top-level node is owned by the unique_ptr in m_Top, so erasing the
        // map entry IS the delete. Deleting it here too would be a double free
        // -- a crash, not a leak, and one that only reproduces when an entire
        // top-level cell empties.
        m_LiveNodes -= 1;
        m_Top.erase(topKey(floorShift(node->origin.x, kSubtreeLevels),
                           floorShift(node->origin.y, kSubtreeLevels)));
        return;
      }
      // Read the parent out first. unlinkFromParent drops the unique_ptr that
      // owns `node`, so the next line would otherwise read node->parent out of
      // freed memory -- and on a recycled allocation that read can name a
      // plausible-looking address, which is the worst way for it to fail.
      unlinkFromParent(node);
      node = parent;
    }
  }

  template <class F>
  std::size_t forEachValueImpl(Node *node, F &fn) const {
    if (node->level == 0) {
      if (node->hasValue()) {
        fn(node->origin, *node->value);
        return 1;
      }
      return 0;
    }
    std::size_t visited = 0;
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child != nullptr) {
        visited += forEachValueImpl(child.get(), fn);
      }
    }
    return visited;
  }

  template <class F>
  std::size_t forEachEntryImpl(Node *node, F &fn) {
    if (node->level == 0) {
      if (!node->hasValue()) {
        return 0;
      }
      // Returned whether or not it is kept, so the caller can tell "visited"
      // from "survived".
      const bool keep = fn(node->origin, *node->value);
      if (!keep) {
        node->value.reset();
        --m_Size;
        // Same aggregate bookkeeping as erase(), for the same reason: the node
        // is only unlinked after the walk, so nothing else would notice.
        updateAggregates(node, /*added=*/false);
        m_EraseQueue.push_back(node);
      }
      return 1;
    }
    std::size_t visited = 0;
    for (const std::unique_ptr<Node> &child : node->children) {
      if (child != nullptr) {
        visited += forEachEntryImpl(child.get(), fn);
      }
    }
    return visited;
  }

  template <class P, class F>
  void forEachCellInImpl(const glm::ivec2 &lo, const glm::ivec2 &hi,
                         const Node *node, int level, const glm::ivec2 &origin,
                         P &visit, F &fn, WalkStats &stats) const {
    if (!cellsOverlap(lo, hi, level, origin)) {
      return;
    }
    if (node != nullptr) {
      ++stats.nodesVisited;
    }
    if (visit(level, origin, node) == Action::Skip) {
      ++stats.subtreesSkipped;
      return;
    }
    if (level == 0) {
      ++stats.cellsVisited;
      fn(0, origin, node != nullptr ? &*node->value : nullptr);
      return;
    }
    if (node == nullptr) {
      // No subtree here at all, so every chunk in this cell is unstreamed. One
      // report for the whole cell; descending would be 4^level reports of the
      // same fact, which is the 4225-iteration scan all over again.
      ++stats.emptyCellsReported;
      fn(level, origin, nullptr);
      return;
    }
    const int half = 1 << (level - 1);
    for (std::size_t index = 0; index < Node::kChildCount; ++index) {
      const glm::ivec2 childOrigin =
          origin +
          glm::ivec2(static_cast<int>(index & 1u) * half,
                     static_cast<int>((index >> 1) & 1u) * half);
      forEachCellInImpl(lo, hi, node->children[index].get(), level - 1,
                        childOrigin, visit, fn, stats);
    }
  }

  // Frees every node *below* `node` and returns how many values were evicted.
  // `node` itself is left standing: its owner destroys it, which is either a
  // unique_ptr slot in its parent or the entry in m_Top. Doing the delete here
  // as well would double-free the node the moment a whole cell emptied.
  template <class F>
  std::size_t destroyDescendants(Node *node, F &onEvict) {
    std::size_t evicted = 0;
    if (node->level == 0) {
      if (node->hasValue()) {
        onEvict(*node->value);
        node->value.reset();
        --m_Size;
        updateAggregates(node, /*added=*/false);
        ++evicted;
      }
      return evicted;
    }
    for (std::unique_ptr<Node> &child : node->children) {
      if (child != nullptr) {
        evicted += destroyDescendants(child.get(), onEvict);
        // destroyDescendants left `child` itself alive and childless; dropping
        // the unique_ptr here is the single delete for it.
        m_LiveNodes -= 1;
        child.reset();
      }
    }
    return evicted;
  }

  template <class P, class F>
  std::size_t eraseSubtreeImpl(Node *node, P &visit, F &onEvict) {
    if (visit(static_cast<int>(node->level), node->origin)) {
      const std::size_t evicted = destroyDescendants(node, onEvict);
      // Everything below is gone, so this cell is now empty and has to be
      // unlinked -- leaving it parented would keep an empty shell in the tree
      // and make the cell look occupied to a completeness query.
      if (node->parent != nullptr) {
        unlinkFromParent(node);
      }
      return evicted;
    }
    std::size_t evicted = 0;
    for (std::unique_ptr<Node> &child : node->children) {
      if (child != nullptr) {
        evicted += eraseSubtreeImpl(child.get(), visit, onEvict);
      }
    }
    return evicted;
  }

  // The top level is a hash map, so its iteration order is undefined. Sorting
  // the cell keys makes every traversal reproducible, which is what lets a test
  // assert on the order and not only on the set. The number of top-level cells
  // is small -- one to four in normal play -- so the sort is not worth avoiding.
  [[nodiscard]] std::vector<uint64_t> sortedKeys() const {
    std::vector<uint64_t> keys;
    keys.reserve(m_Top.size());
    for (const auto &entry : m_Top) {
      keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
  }

  std::unordered_map<uint64_t, std::unique_ptr<Node>> m_Top;
  std::vector<Node *> m_EraseQueue;
  std::size_t m_Size = 0;
  std::size_t m_LiveNodes = 0;
  int m_WalkDepth = 0;
};
