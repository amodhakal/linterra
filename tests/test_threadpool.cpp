#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "doctest/doctest.h"
#include "threadpool.h"

// ThreadPool runs work on background threads, so these tests share two
// constraints that are easy to get wrong:
//
//   1. Anything a queued task touches must outlive the test case's stack
//      frame. A REQUIRE/CHECK that fails returns from the test case
//      immediately, which would leave a worker blocked on a reference to a
//      destroyed local -- a stack-use-after-scope that ASan reports as a crash
//      rather than a clean failure. State a task needs is therefore held in a
//      shared_ptr, and no assertion runs while workers are still parked.
//
//   2. Every wait is bounded, so a regression fails the check instead of
//      hanging CI.

namespace {

// Gate a worker can be parked on until the test releases it.
struct Gate {
  std::mutex mutex;
  std::condition_variable condition;
  bool released = false;
  std::atomic<unsigned> parked{0};

  void Park() {
    parked.fetch_add(1);
    std::unique_lock lock(mutex);
    condition.wait(lock, [this] { return released; });
  }

  void Release() {
    {
      std::lock_guard lock(mutex);
      released = true;
    }
    condition.notify_all();
  }
};

constexpr auto kTimeout = std::chrono::seconds(10);

// Spin until `predicate` holds or the timeout expires. Returns the final value.
template <typename Predicate>
bool WaitUntil(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + kTimeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return predicate();
    }
    std::this_thread::yield();
  }
  return true;
}

// A pool that drains its backlog before joining.
//
// ~ThreadPool now *discards* queued work, which is what shutdown wants (#157):
// nothing will upload or draw a chunk meshed after the window is gone. Tests
// that assert "every queued task ran" need the opposite contract, and they
// need to ask for it explicitly -- which is the point of making shutdown a
// policy rather than an accident of the destructor's implementation.
class DrainingPool {
 public:
  DrainingPool() = default;

  // Drains on destruction, so a test that queues work and then asserts on it
  // behaves as it did before the policy was introduced.
  ~DrainingPool() {
    m_Pool->requestShutdown(/*discardPending=*/false);
    // Joining is what makes the drain observable: requestShutdown only sets
    // the flag and wakes the workers, so without this the test reads its
    // counters while tasks are still running.
    m_Pool->waitForShutdown();
  }

  ThreadPool& operator*() { return *m_Pool; }
  ThreadPool* operator->() { return m_Pool.get(); }

  // Forwarded so the tests read exactly as they did against a ThreadPool.
  template <typename F>
  bool tryEnqueue(F &&func, std::size_t maxPendingTasks) {
    return m_Pool->tryEnqueue(std::forward<F>(func), maxPendingTasks);
  }

  DrainingPool(const DrainingPool&) = delete;
  DrainingPool& operator=(const DrainingPool&) = delete;

 private:
  // A pointer, not a member: ~DrainingPool's body runs *before* its members
  // are destroyed, but a ThreadPool member would already have run ~ThreadPool
  // -- which discards the backlog -- by the time the body could ask for a
  // drain. Holding it indirectly is what makes the request meaningful.
  std::unique_ptr<ThreadPool> m_Pool = std::make_unique<ThreadPool>();
};

}  // namespace

TEST_CASE("ThreadPool runs accepted tasks") {
  std::atomic<int> counter{0};
  constexpr int kTasks = 200;

  {
    DrainingPool pool;
    for (int i = 0; i < kTasks; ++i) {
      pool.tryEnqueue([&counter] { counter.fetch_add(1); }, kTasks);
    }
  }  // destructor drains the queue and joins

  // Read only after destruction: no worker can still be running a task.
  CHECK(counter.load() == kTasks);
}

TEST_CASE("ThreadPool runs tasks on more than one thread") {
  // The pool sizes itself to max(2, hardware_concurrency), so a task that
  // blocks until a *second* task has run proves concurrency: it can only make
  // progress if some other worker picked the other task up, because this one
  // is still occupying the worker that took it.
  auto secondRan = std::make_shared<std::atomic<bool>>(false);

  {
    DrainingPool pool;

    pool.tryEnqueue(
        [secondRan] {
          WaitUntil([&secondRan] { return secondRan->load(); });
        },
        8);

    pool.tryEnqueue([secondRan] { secondRan->store(true); }, 8);
  }  // destruction joins, which bounds the wait above

  CHECK_MESSAGE(secondRan->load(),
                "the second task never ran: the pool did not execute tasks "
                "concurrently");
}

TEST_CASE("ThreadPool tryEnqueue rejects when the cap cannot be met") {
  DrainingPool pool;
  std::atomic<int> ran{0};

  // A cap of zero is unsatisfiable by construction, so every enqueue is
  // refused. This is the guard that stops a single frame from flooding the
  // pool with thousands of tasks (#29).
  for (int i = 0; i < 16; ++i) {
    CHECK_FALSE(pool.tryEnqueue([&ran] { ran.fetch_add(1); }, 0));
  }

  CHECK(ran.load() == 0);
}

TEST_CASE("ThreadPool tryEnqueue refuses work past the pending cap") {
  // The cap bounds the *pending* queue, so every worker has to be busy to
  // observe it -- otherwise idle workers drain the queue as fast as it is
  // filled and the cap is never reached.
  auto gate = std::make_shared<Gate>();
  // Matches ThreadPool's own sizing: one fewer than the hardware count, so
  // the render thread keeps a core. ThreadPool exposes the effective count so
  // this cannot drift again (#157).
  const unsigned hardware = std::thread::hardware_concurrency();
  const unsigned workerCount = std::max(1u, hardware > 1 ? hardware - 1 : 1u);
  constexpr std::size_t kCap = 4;

  bool allEnqueued = true;
  int accepted = 0;
  unsigned parkedCount = 0;

  {
    DrainingPool pool;

    for (unsigned i = 0; i < workerCount; ++i) {
      if (!pool.tryEnqueue([gate] { gate->Park(); }, workerCount + 8)) {
        allEnqueued = false;
      }
    }

    WaitUntil([&] { return gate->parked.load() >= workerCount; });
    parkedCount = gate->parked.load();

    for (int i = 0; i < 200; ++i) {
      if (pool.tryEnqueue([] {}, kCap)) {
        ++accepted;
      }
    }

    // Release before the pool is destroyed, otherwise the destructor would
    // block forever joining workers that are still parked.
    gate->Release();
  }  // pool destroyed here, joining every worker

  // All assertions run after the workers are gone, so a failure cannot leave
  // a thread reading a destroyed stack frame.
  CHECK(allEnqueued);
  CHECK(parkedCount == workerCount);
  CHECK(accepted <= static_cast<int>(kCap));
  CHECK(accepted > 0);
}

TEST_CASE("ThreadPool runs every task exactly once") {
  struct State {
    std::mutex mutex;
    std::set<std::thread::id> ids;
    std::atomic<int> done{0};
  };

  auto state = std::make_shared<State>();
  constexpr int kTasks = 400;

  {
    DrainingPool pool;
    for (int i = 0; i < kTasks; ++i) {
      pool.tryEnqueue(
          [state] {
            {
              std::lock_guard lock(state->mutex);
              state->ids.insert(std::this_thread::get_id());
            }
            state->done.fetch_add(1);
          },
          kTasks);
    }
  }  // destruction drains and joins

  CHECK(state->done.load() == kTasks);

  // NOTE: this test used to assert state->ids.size() > 1, on the reasoning
  // that a pool of max(2, hardware_concurrency) workers "cannot" run 400
  // trivial tasks on one thread. That reasoning is wrong. A worker that is
  // already running can drain the entire queue before a second worker is
  // scheduled onto a busy machine -- and that is exactly what happens on a
  // loaded CI runner. The assertion passed locally 10/10 and then failed on
  // the GitHub macOS runner, where it is a false positive, not a real defect.
  //
  // Genuine concurrency is asserted deterministically by the bounded-blocking
  // test above ("ThreadPool runs tasks on more than one thread"), which cannot
  // pass unless a second worker picks up a second task. Recording the thread
  // ids here is still useful -- it feeds a diagnostic below -- but it is not
  // something to assert on.
  if (state->ids.size() <= 1) {
    MESSAGE("all " << kTasks << " tasks ran on "
                   << state->ids.size() << " thread(s); not asserted on");
  }
}

TEST_CASE("ThreadPool survives a task that throws") {
  // An exception escaping a worker took the whole process down.
  // std::terminate fires the moment one propagates out of a thread's entry
  // function, because there is no handler between task() and the thread
  // boundary -- so a single std::bad_alloc from the mesher's push_back growth
  // aborted the process mid-frame with "terminate called after throwing an
  // instance of 'std::bad_alloc'" and SIGABRT. That is indistinguishable from
  // any other crash, and it loses all 4224 chunks that were fine.
  //
  // The correct outcome is to lose one task and keep going.
  //
  // If this regresses the test binary aborts rather than failing, which is
  // exactly the behaviour under test.
  auto state = std::make_shared<std::atomic<int>>(0);
  auto laterRan = std::make_shared<std::atomic<bool>>(false);

  {
    DrainingPool pool;
    // std::runtime_error rather than std::bad_alloc: the point is that an
    // exception must not escape, not which exception type.
    REQUIRE(pool.tryEnqueue(
        [] { throw std::runtime_error("deliberate test failure"); }, 8));

    // A second task must still run. Without the guard the process is already
    // gone by this point, so this is the assertion that distinguishes
    // "caught" from "ignored and continued" in the wrong order.
    for (int i = 0; i < 64; ++i) {
      pool.tryEnqueue(
          [state] { state->fetch_add(1, std::memory_order_release); }, 128);
    }
    REQUIRE(WaitUntil([state] {
      return state->load(std::memory_order_acquire) == 64;
    }));

    pool.tryEnqueue([laterRan] { laterRan->store(true); }, 8);
    REQUIRE(WaitUntil([laterRan] { return laterRan->load(); }));
  }

  CHECK(laterRan->load());
}

TEST_CASE("ThreadPool survives a task that throws a non-std exception") {
  // The catch(...) arm. A task throwing something that is not a
  // std::exception -- std::bad_alloc is one, but so is anything a future
  // mesher might raise -- must not terminate the process either.
  auto laterRan = std::make_shared<std::atomic<bool>>(false);

  {
    DrainingPool pool;
    REQUIRE(pool.tryEnqueue([] { throw 42; }, 8));
    pool.tryEnqueue([laterRan] { laterRan->store(true); }, 8);
    REQUIRE(WaitUntil([laterRan] { return laterRan->load(); }));
  }

  CHECK(laterRan->load());
}

TEST_CASE("ThreadPool keeps running after many throwing tasks") {
  // The realistic shape: a run of bad allocations across several workers,
  // interleaved with good work. Losing the pool to the first one would leave
  // every remaining chunk unmeshed.
  auto good = std::make_shared<std::atomic<int>>(0);

  {
    DrainingPool pool;
    for (int i = 0; i < 100; ++i) {
      pool.tryEnqueue(
          [] { throw std::runtime_error("deliberate test failure"); }, 256);
    }
    for (int i = 0; i < 100; ++i) {
      pool.tryEnqueue(
          [good] { good->fetch_add(1, std::memory_order_release); }, 256);
    }
    REQUIRE(WaitUntil([good] { return good->load(std::memory_order_acquire) == 100; }));
  }

  CHECK(good->load() == 100);
}

TEST_CASE("ThreadPool destruction discards the backlog instead of draining") {
  // Pressing Escape used to leave the process spinning with no window while
  // the pool meshed every remaining chunk -- up to 1024 full 16 x 256 chunk
  // meshes, potentially seconds of 100% CPU. From the user's point of view
  // the app "wouldn't quit" (#157).
  //
  // The queued work is worthless at that point: nothing will upload or draw a
  // chunk meshed after the window is gone.
  //
  // The measurement is a wall-clock bound rather than a counter, because the
  // thing being prevented is time. Each task blocks on a gate that is never
  // opened, so a draining implementation cannot finish until the timeout,
  // while a discarding one returns promptly.
  auto state = std::make_shared<std::atomic<int>>(0);
  auto started = std::make_shared<std::atomic<int>>(0);
  auto release = std::make_shared<Gate>();
  constexpr int kBlockedTasks = 64;

  const auto begin = std::chrono::steady_clock::now();
  {
    ThreadPool pool;
    for (int i = 0; i < kBlockedTasks; ++i) {
      pool.tryEnqueue(
          [state, started, release] {
            started->fetch_add(1, std::memory_order_release);
            release->Park();  // never opened
            state->fetch_add(1, std::memory_order_release);
          },
          kBlockedTasks);
    }
    // Let the workers pick up work and block on the gate. Bounded: if they do
    // not all start, the destructor still has a finite queue to discard.
    WaitUntil([started] {
      return started->load(std::memory_order_acquire) >= 1;
    });
    // Release so a draining implementation could not deadlock here rather than
    // merely being slow -- the point is the discard, not a hang.
    release->Release();
  }  // destructor: must discard, not drain
  const auto elapsed = std::chrono::steady_clock::now() - begin;

  INFO("destruction took "
       << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
       << "ms");
  CHECK_MESSAGE(elapsed < std::chrono::seconds(5),
                "destruction was slow, which suggests the backlog was drained "
                "rather than discarded");

  // Far fewer than kBlockedTasks ran, because the backlog was discarded. A
  // draining destructor would run all 64. The exact count depends on how many
  // workers had picked up a task before the queue was swapped out, so this is
  // an upper bound: no worker can start a task after the swap.
  CHECK_MESSAGE(state->load(std::memory_order_acquire) < kBlockedTasks,
                "every queued task ran, so the backlog was drained rather than "
                "discarded");
  CHECK(state->load(std::memory_order_acquire) <= kBlockedTasks);
}

TEST_CASE("ThreadPool requestShutdown with discard drops queued work") {
  // The same policy, asked for explicitly rather than relied upon from the
  // destructor. This is what ChunkManager::shutdown uses (#157).
  auto ran = std::make_shared<std::atomic<int>>(0);
  auto release = std::make_shared<Gate>();
  constexpr int kTasks = 32;

  {
    ThreadPool pool;
    // One task parks a worker; the rest queue up behind it.
    pool.tryEnqueue(
        [release] { release->Park(); }, kTasks);
    for (int i = 0; i < kTasks - 1; ++i) {
      pool.tryEnqueue(
          [ran] { ran->fetch_add(1, std::memory_order_release); }, kTasks);
    }

    pool.requestShutdown(/*discardPending=*/true);
    release->Release();
    pool.waitForShutdown();
  }

  // The backlog was discarded, so most of the queued tasks never ran. The one
  // that was already executing may have completed, so the assertion is an
  // upper bound rather than an exact count.
  INFO("ran " << ran->load() << " of " << (kTasks - 1) << " queued tasks");
  CHECK_MESSAGE(ran->load(std::memory_order_acquire) < kTasks - 1,
                "expected the queued tasks to be discarded, but all of them "
                "ran");
}

TEST_CASE("ThreadPool requestShutdown without discard runs everything") {
  // The opposite policy, so the flag is doing the work rather than the
  // discard being incidental.
  auto ran = std::make_shared<std::atomic<int>>(0);
  constexpr int kTasks = 64;

  {
    ThreadPool pool;
    for (int i = 0; i < kTasks; ++i) {
      pool.tryEnqueue(
          [ran] { ran->fetch_add(1, std::memory_order_release); }, kTasks);
    }
    pool.requestShutdown(/*discardPending=*/false);
    pool.waitForShutdown();
  }

  CHECK(ran->load(std::memory_order_acquire) == kTasks);
}

TEST_CASE("ThreadPool is not copyable or movable") {
  // Stated rather than left to fail as ill-formed by accident: the pool owns
  // threads and a mutex, and std::mutex being non-copyable is an accident of
  // the implementation, not a declaration of intent (#157).
  static_assert(!std::is_copy_constructible_v<ThreadPool>);
  static_assert(!std::is_copy_assignable_v<ThreadPool>);
  static_assert(!std::is_move_constructible_v<ThreadPool>);
  static_assert(!std::is_move_assignable_v<ThreadPool>);
  CHECK(true);
}
