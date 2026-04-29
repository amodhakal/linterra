#pragma once

#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
  explicit ThreadPool();
  ~ThreadPool();

  template <typename F> void enqueue(F &&func) {
    {
      std::unique_lock lock(m_Mutex);
      m_Tasks.emplace(std::forward<F>(func));
    }
    m_Condition.notify_one();
  }

private:
  std::vector<std::thread> m_Workers;
  std::queue<std::function<void()>> m_Tasks;
  std::mutex m_Mutex;
  std::condition_variable m_Condition;
  bool m_Stop = false;
};