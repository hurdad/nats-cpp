#pragma once

#include <coroutine>
#include <future>
#include <thread>
#include <chrono>
#include <utility>

namespace natscpp {

/**
 * @brief Minimal awaitable wrapper over std::future for optional coroutine APIs.
 */
template <typename T>
class future_awaitable {
 public:
  explicit future_awaitable(std::future<T> fut) : fut_(std::move(fut)) {}

  [[nodiscard]] bool await_ready() const {
    return fut_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> h) {
    std::thread([fut = std::move(fut_), h]() mutable {
      fut.wait();
      h.resume();
    }).detach();
  }

  T await_resume() { return fut_.get(); }

 private:
  std::future<T> fut_;
};

}  // namespace natscpp
