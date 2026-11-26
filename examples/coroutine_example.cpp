// C++20 coroutines: co_await a request with connection::request_awaitable.
//
// natscpp only provides the awaitable; the coroutine type comes from your framework. This example uses a
// minimal eager task whose completion is reported through a std::future. Note that the coroutine resumes on
// a background thread once the reply arrives.
#include <chrono>
#include <coroutine>
#include <exception>
#include <future>
#include <iostream>
#include <string>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

namespace {

// Starts immediately, runs to completion, and publishes its result (or exception) through a future.
template <typename T>
struct eager_task {
  struct promise_type {
    std::promise<T> result;
    eager_task get_return_object() { return eager_task{result.get_future()}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_value(T value) { result.set_value(std::move(value)); }
    void unhandled_exception() { result.set_exception(std::current_exception()); }
  };
  std::future<T> future;
};

// nc is a reference that must outlive the coroutine; main() guarantees that by waiting for the result.
eager_task<std::string> ask(natscpp::connection& nc,  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
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

    auto task = ask(nc, "ping-coroutine");
    std::cout << task.future.get() << '\n';  // rethrows the coroutine's exception, if any
    return 0;
  });
}
