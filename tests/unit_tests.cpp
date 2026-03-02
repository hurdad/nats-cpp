#include <natscpp/awaitable.hpp>
#include <natscpp/error.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/message.hpp>

#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <string>

#include <natscpp/connection.hpp>

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

void test_connection_has_sync_and_async_apis() {
  natscpp::connection nc;

  static_assert(requires { nc.subscribe_sync("subj"); });
  static_assert(requires { nc.subscribe_queue_sync("subj", "workers"); });
  static_assert(requires(std::function<void(natscpp::message)> fn) { nc.subscribe_async("subj", fn); });
  static_assert(requires(std::function<void(natscpp::message)> fn) {
    nc.subscribe_queue_async("subj", "workers", fn);
  });
  static_assert(requires { nc.request_sync("svc", "payload"); });
  static_assert(requires { nc.request_async("svc", "payload"); });
}

void test_connection_sync_and_async_roundtrip_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping connection integration checks (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string sync_subject = "natscpp.test.sync." + ts;
  const std::string async_subject = "natscpp.test.async." + ts;
  const std::string rpc_subject = "natscpp.test.rpc." + ts;

  auto sync_sub = nc.subscribe_sync(sync_subject);
  nc.publish(sync_subject, "hello-sync");
  auto sync_msg = sync_sub.next_message(std::chrono::seconds(2));
  assert(sync_msg.data() == "hello-sync");

  std::promise<std::string> async_data;
  auto async_sub = nc.subscribe_async(async_subject, [&async_data](natscpp::message msg) {
    async_data.set_value(std::string(msg.data()));
  });
  nc.publish(async_subject, "hello-async");
  assert(async_data.get_future().get() == "hello-async");
  async_sub.unsubscribe();

  auto responder = nc.subscribe_async(rpc_subject, [&nc](natscpp::message msg) {
    if (!msg.reply_to().empty()) {
      nc.publish(msg.reply_to(), msg.data());
    }
  });

  auto sync_reply = nc.request_sync(rpc_subject, "rpc-sync", std::chrono::seconds(2));
  assert(sync_reply.data() == "rpc-sync");

  auto async_reply = nc.request_async(rpc_subject, "rpc-async", std::chrono::seconds(2)).get();
  assert(async_reply.data() == "rpc-async");
  responder.unsubscribe();
}

}  // namespace

int main() {
  test_throw_on_error_ok_does_not_throw();
  test_throw_on_error_reports_status_and_context();
  test_message_accessors_and_headers();
  test_future_awaitable_ready_and_resume();
  test_jetstream_and_consumers_move_semantics_on_empty_handles();
  test_connection_has_sync_and_async_apis();
  test_connection_sync_and_async_roundtrip_if_server_available();
  return 0;
}
