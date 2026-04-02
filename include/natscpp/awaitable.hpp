#pragma once

#include <atomic>
#include <coroutine>
#include <chrono>
#include <future>
#include <thread>
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
 * A shared alive flag ensures the detached worker thread does not call
 * h.resume() if the awaitable (and its owning coroutine) has been destroyed
 * before the future becomes ready.
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

  ~future_awaitable() {
    // Race to claim cancellation.  The thread uses a CAS to atomically flip
    // alive true→false; the destructor uses exchange for the same effect.
    // Whichever wins sets alive=false first; the other side is a no-op.
    // If the thread won (already called h.resume()), the coroutine owns the
    // frame and destroying the awaitable from within it is safe.
    state_->alive.exchange(false, std::memory_order_acq_rel);
  }

  [[nodiscard]] bool await_ready() const {
    return state_->fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  void await_suspend(std::coroutine_handle<> h) {
    std::thread([s = state_, h]() mutable {
      s->fut.wait();
      // Atomically claim the right to resume: only one of (thread, destructor)
      // can flip alive from true to false.  If we win, the awaitable is still
      // alive and h is valid; if we lose, the coroutine was destroyed first.
      bool expected = true;
      if (s->alive.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        h.resume();
      }
    }).detach();
  }

  T await_resume() { return state_->fut.get(); }

 private:
  struct shared_state {
    explicit shared_state(std::future<T> f) : fut(std::move(f)) {}
    std::future<T> fut;
    std::atomic<bool> alive{true};
  };
  std::shared_ptr<shared_state> state_;
};

}  // namespace natscpp
