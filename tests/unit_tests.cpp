#include <natscpp/awaitable.hpp>
#include <natscpp/error.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/message.hpp>

#include <cassert>
#include <future>
#include <string>

namespace {

void test_throw_on_error_ok_does_not_throw() {
  natscpp::throw_on_error(NATS_OK, "noop");
}

void test_throw_on_error_reports_status_and_context() {
  try {
    natscpp::throw_on_error(NATS_ERR, "publish");
    assert(false && "throw_on_error should throw for non-OK status");
  } catch (const natscpp::nats_error& ex) {
    assert(ex.status() == NATS_ERR);
    const std::string what{ex.what()};
    assert(what.find("publish") != std::string::npos);
  }
}

void test_message_accessors_and_headers() {
  natsMsg* raw = nullptr;
  const char payload[] = "hello";
  assert(natsMsg_Create(&raw, "subject.a", "reply.b", payload, static_cast<int>(sizeof(payload) - 1)) == NATS_OK);

  natscpp::message msg{raw};
  assert(msg.valid());
  assert(msg.subject() == "subject.a");
  assert(msg.reply_to() == "reply.b");
  assert(msg.data() == "hello");

  msg.set_header("x-test", "v1");
  assert(msg.header("x-test") == "v1");
  assert(msg.header("missing").empty());
}

void test_future_awaitable_ready_and_resume() {
  {
    std::promise<int> ready;
    ready.set_value(42);
    natscpp::future_awaitable<int> awaitable{ready.get_future()};
    assert(awaitable.await_ready());
    assert(awaitable.await_resume() == 42);
  }

  {
    std::promise<int> pending;
    auto fut = pending.get_future();
    natscpp::future_awaitable<int> awaitable{std::move(fut)};
    assert(!awaitable.await_ready());
    pending.set_value(7);
    assert(awaitable.await_resume() == 7);
  }
}

void test_jetstream_and_consumers_move_semantics_on_empty_handles() {
  natscpp::jetstream js_default;
  natscpp::jetstream js_moved = std::move(js_default);
  natscpp::jetstream js_assigned;
  js_assigned = std::move(js_moved);

  natscpp::js_pull_consumer pull_default;
  natscpp::js_pull_consumer pull_moved = std::move(pull_default);
  natscpp::js_pull_consumer pull_assigned;
  pull_assigned = std::move(pull_moved);

  natscpp::js_push_consumer push_default;
  natscpp::js_push_consumer push_moved = std::move(push_default);
  natscpp::js_push_consumer push_assigned;
  push_assigned = std::move(push_moved);

  assert(!pull_assigned.valid());
  assert(!push_assigned.valid());
}

}  // namespace

int main() {
  test_throw_on_error_ok_does_not_throw();
  test_throw_on_error_reports_status_and_context();
  test_message_accessors_and_headers();
  test_future_awaitable_ready_and_resume();
  test_jetstream_and_consumers_move_semantics_on_empty_handles();
  return 0;
}
