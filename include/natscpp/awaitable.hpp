#pragma once

#include <coroutine>
#include <future>
#include <thread>
#include <chrono>
#include <utility>

namespace natscpp {

/**
 * @brief Minimal awaitable wrapper over std::future for optional coroutine APIs.
 *
 * Uses std::shared_future so the future can be shared between await_suspend's
 * worker thread and await_resume, avoiding a use-after-move bug.
 *
 * @note await_suspend spawns one detached thread per suspension. This is
 * sufficient for occasional use but is not suitable for high-frequency
 * coroutine suspension; use a thread pool or an async I/O executor in that
 * case.
 */
template <typename T>
class future_awaitable {
 public:
  explicit future_awaitable(std::future<T> fut) : fut_(fut.share()) {}

  [[nodiscard]] bool await_ready() const {
    return fut_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> h) {
    // Copy the shared_future so both the thread and await_resume can call get().
    std::thread([fut = fut_, h]() mutable {
      fut.wait();
      h.resume();
    }).detach();
  }

  T await_resume() { return fut_.get(); }

 private:
  std::shared_future<T> fut_;
};

}  // namespace natscpp
