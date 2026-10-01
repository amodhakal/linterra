#include <threadpool.h>

#include <exception>
#include <print>

ThreadPool::ThreadPool() {
  const auto threadCount = std::max(2u, std::thread::hardware_concurrency());
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

ThreadPool::~ThreadPool() {
  {
    std::unique_lock lock(m_Mutex);
    m_Stop = true;
  }

  m_Condition.notify_all();
  for (auto &worker : m_Workers) {
    worker.join();
  }
}
