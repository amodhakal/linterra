#include <threadpool.h>

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
        task();
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
