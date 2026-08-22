#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
  explicit ThreadPool();
  ~ThreadPool();

  // Enqueue a task unless the pending-task cap is reached.
  // Returns true if the task was queued, false if the caller should
  // retry later (e.g. the next frame). This caps the spawn rate so a
  // single frame can't flood the pool with thousands of tasks.
  template <typename F> bool tryEnqueue(F &&func,
                                        std::size_t maxPendingTasks) {
    {
      std::unique_lock lock(m_Mutex);
      if (m_Stop || m_Tasks.size() >= maxPendingTasks) {
        return false;
      }
      m_Tasks.emplace(std::forward<F>(func));
    }
    m_Condition.notify_one();
    return true;
  }

private:
  std::vector<std::thread> m_Workers;
  std::queue<std::function<void()>> m_Tasks;
  std::mutex m_Mutex;
  std::condition_variable m_Condition;
  bool m_Stop = false;
};