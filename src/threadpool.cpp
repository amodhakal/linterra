#include <threadpool.h>

#include <exception>
#include <print>

ThreadPool::ThreadPool() {
  // Leave a core for the render thread. N workers plus the main thread on an
  // N-core machine means the render thread is guaranteed to be descheduled
  // during a mesh burst (#157).
  const unsigned hardware = std::thread::hardware_concurrency();
  const auto threadCount =
      std::max(1u, hardware > 1 ? hardware - 1 : 1u);
  m_Workers.reserve(threadCount);
  for (unsigned i = 0; i < threadCount; ++i) {
    m_Workers.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock lock(m_Mutex);
          m_Condition.wait(lock, [this] { return m_Stop || !m_Tasks.empty(); });
          if (m_Stop && m_Tasks.empty()) {
            return;
          }
          task = std::move(m_Tasks.front());
          m_Tasks.pop();
        }
        // A task must never let an exception escape the thread's entry
        // function: std::terminate takes the whole process down, when the
        // correct outcome is to lose one chunk and keep rendering the rest.
        //
        // The realistic case is std::bad_alloc from the mesher's push_back
        // growth. There is no OOM handling anywhere in the engine -- no
        // allocator wrapper, no reserve(), no catch on any other path -- and
        // the stated steady state is 1.5 GB resident at high render
        // distance, so a push_back failing is a memory-tight machine rather
        // than a theoretical one (#141).
        try {
          task();
        } catch (const std::exception &e) {
          // stderr, not stdout: the app's stdout surface is ImGui-only.
          std::println(stderr, "ThreadPool: task threw: {}", e.what());
        } catch (...) {
          std::println(stderr, "ThreadPool: task threw a non-std exception");
        }
      }
    });
  }
}

void ThreadPool::requestShutdown(bool discardPending) {
  {
    std::unique_lock lock(m_Mutex);
    if (discardPending) {
      // Swap the backlog out and let it destruct. Draining instead means the
      // destructor joins workers that keep meshing 16 x 256 chunks that
      // nothing will ever upload or draw (#157).
      std::queue<std::function<void()>> discarded;
      m_Tasks.swap(discarded);
    }
    m_Stop = true;
  }

  m_Condition.notify_all();
}

void ThreadPool::waitForShutdown() {
  for (auto &worker : m_Workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

ThreadPool::~ThreadPool() {
  // Discard, not drain -- see requestShutdown.
  requestShutdown(/*discardPending=*/true);
  waitForShutdown();
}
