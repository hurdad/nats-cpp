// C++20 coroutines: co_await a request with connection::request_awaitable.
//
// natscpp only provides the awaitable; the coroutine type comes from your framework. This example uses a
// minimal task whose result is reported through a std::future. The coroutine resumes on a background thread
// once the reply arrives, so the task starts suspended and is started explicitly: a coroutine that starts
// running inside its own call (initial_suspend = suspend_never) can be resumed, finished and freed by that
// thread while the caller is still returning from the call, which is a data race on the coroutine frame.
#include <chrono>
#include <coroutine>
#include <exception>
#include <future>
#include <iostream>
#include <string>
#include <utility>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

namespace {

// Created suspended; start() runs it. It frees itself when it finishes and publishes its result (or exception)
// through a future.
template <typename T>
class task {
 public:
  struct promise_type {
    std::promise<T> result;
    task get_return_object() { return task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_value(T value) { result.set_value(std::move(value)); }
    void unhandled_exception() { result.set_exception(std::current_exception()); }
  };

  task(const task&) = delete;
  task& operator=(const task&) = delete;
  task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})), future_(std::move(other.future_)) {}
  task& operator=(task&&) = delete;
  ~task() {
    if (handle_) handle_.destroy();  // never started
  }

  // Runs the coroutine up to its first suspension; from then on the coroutine owns (and frees) its frame.
  // Returns the future for its result.
  std::future<T> start() && {
    std::exchange(handle_, {}).resume();
    return std::move(future_);
  }

 private:
  explicit task(std::coroutine_handle<promise_type> h) : handle_(h), future_(h.promise().result.get_future()) {}
  std::coroutine_handle<promise_type> handle_;
  std::future<T> future_;
};

// nc is a reference that must outlive the coroutine; main() guarantees that by waiting for the result.
task<std::string> ask(natscpp::connection& nc,  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
                      std::string question) {
  // Suspends here until the reply arrives (or the request fails, which rethrows from co_await).
  natscpp::message reply =
      co_await nc.request_awaitable("demo.coro.echo", std::move(question), std::chrono::seconds(2));
  co_return "reply: " + std::string(reply.data());
}

}  // namespace

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});
    auto responder = nc.subscribe_async("demo.coro.echo", [&nc](natscpp::message req) {
      if (!req.reply_to().empty()) nc.publish(req.reply_to(), req.data());
    });
    nc.flush(std::chrono::seconds(1));

    std::future<std::string> reply = ask(nc, "ping-coroutine").start();
    std::cout << reply.get() << '\n';  // rethrows the coroutine's exception, if any
    return 0;
  });
}
