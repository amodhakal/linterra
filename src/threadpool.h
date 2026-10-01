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

  // The pool owns threads and a mutex, so copying it is nonsense. Those
  // operations are deleted rather than left to fail as ill-formed by
  // accident -- the intent is stated, not implied by std::mutex being
  // non-copyable (#157).
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  // Stop the pool. With discardPending the queued backlog is thrown away and
  // workers return after finishing whatever they are holding; otherwise the
  // backlog is drained first.
  //
  // Discarding is the right choice on shutdown. Every queued task is a full
  // 16 x 256 chunk mesh, the backlog reaches 1024 entries, and draining it
  // means pressing Escape appears to hang -- the window is gone but the
  // process sits at 100% CPU for as long as it takes to mesh every remaining
  // chunk, potentially seconds. There is no value in that work: nothing will
  // ever upload or draw the results (#157).
  void requestShutdown(bool discardPending);

  // Join the workers. ~ThreadPool does this, but a caller that has already
  // requested shutdown and wants to observe the result -- or the pool's
  // lifetime -- needs to wait explicitly rather than assume the destructor
  // has run.
  void waitForShutdown();

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