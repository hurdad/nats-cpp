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
 * The future is stored in a shared_ptr so await_suspend's worker thread can
 * call wait() while await_resume calls get() after resumption — without
 * requiring shared_future (whose get() returns const T&, which cannot be
 * moved for move-only result types such as message).
 *
 * @note await_suspend spawns one detached thread per suspension. This is
 * sufficient for occasional use but is not suitable for high-frequency
 * coroutine suspension; use a thread pool or an async I/O executor in that
 * case.
 */
template <typename T>
class future_awaitable {
 public:
  explicit future_awaitable(std::future<T> fut)
      : state_(std::make_shared<shared_state>(std::move(fut))) {}

  [[nodiscard]] bool await_ready() const {
    return state_->fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> h) {
    std::thread([s = state_, h]() mutable {
      s->fut.wait();
      h.resume();
    }).detach();
  }

  T await_resume() { return state_->fut.get(); }

 private:
  struct shared_state {
    explicit shared_state(std::future<T> f) : fut(std::move(f)) {}
    std::future<T> fut;
  };
  std::shared_ptr<shared_state> state_;
};

}  // namespace natscpp
