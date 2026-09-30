#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

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

}  // namespace

TEST_CASE("ThreadPool runs accepted tasks") {
  std::atomic<int> counter{0};
  constexpr int kTasks = 200;

  {
    ThreadPool pool;
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
    ThreadPool pool;

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
  ThreadPool pool;
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
  const unsigned workerCount =
      std::max(2u, std::thread::hardware_concurrency());
  constexpr std::size_t kCap = 4;

  bool allEnqueued = true;
  int accepted = 0;
  unsigned parkedCount = 0;

  {
    ThreadPool pool;

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

TEST_CASE("ThreadPool spreads work across worker threads") {
  struct State {
    std::mutex mutex;
    std::set<std::thread::id> ids;
    std::atomic<int> done{0};
  };

  auto state = std::make_shared<State>();
  constexpr int kTasks = 400;

  {
    ThreadPool pool;
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
  // A pool of max(2, hardware_concurrency) workers cannot run 400 trivial
  // tasks entirely on one thread, so more than one id must appear.
  CHECK(state->ids.size() > 1);
}
