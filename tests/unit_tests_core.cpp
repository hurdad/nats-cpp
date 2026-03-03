#include "unit_tests.hpp"

#include <natscpp/awaitable.hpp>
#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>
#include <natscpp/header.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/message.hpp>
#include <natscpp/trace.hpp>

#include <cassert>
#include <chrono>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace {

void test_throw_on_error_ok_does_not_throw() { natscpp::throw_on_error(NATS_OK, "noop"); }

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

void test_throw_on_error_preserves_status_code() {
  for (natsStatus s : {NATS_TIMEOUT, NATS_NOT_FOUND, NATS_NOT_PERMITTED, NATS_NO_MEMORY}) {
    try {
      natscpp::throw_on_error(s, "ctx");
      assert(false && "expected throw");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == s);
      const std::string what{ex.what()};
      assert(what.find("ctx") != std::string::npos);
    }
  }
}

void test_header_wrappers() {
  auto h = natscpp::header::create();
  assert(h.valid());
  h.set("x-test", "v1");
  h.add("x-test", "v2");
  assert(h.get("x-test") == "v1");
  const auto values = h.values("x-test");
  assert(values.size() == 2);
  const auto keys = h.keys();
  assert(keys.size() == 1);
  assert(keys[0] == "x-test");
  assert(h.keys_count() == 1);
  h.erase("x-test");
  h.destroy();
  assert(!h.valid());
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
  msg.add_header("x-test", "v2");
  assert(msg.header("x-test") == "v1");
  const auto values = msg.header_values("x-test");
  assert(values.size() == 2);
  const auto keys = msg.header_keys();
  assert(!keys.empty());
  msg.delete_header("x-test");
  assert(msg.header("missing").empty());
}

void test_message_default_constructor_null_guards() {
  natscpp::message msg;
  assert(!msg.valid());
  assert(msg.native_handle() == nullptr);
  assert(msg.subject().empty());
  assert(msg.reply_to().empty());
  assert(msg.data().empty());
  assert(msg.header("x").empty());
  assert(msg.header_values("x").empty());
  assert(msg.header_keys().empty());
  assert(!msg.is_no_responders());
  assert(msg.sequence() == 0);
  assert(msg.timestamp() == 0);
  msg.set_header("k", "v");
  msg.add_header("k", "v");
  msg.delete_header("k");
}

void test_message_create_factory() {
  auto msg = natscpp::message::create("subj.x", "reply.y", "body");
  assert(msg.valid());
  assert(msg.native_handle() != nullptr);
  assert(msg.subject() == "subj.x");
  assert(msg.reply_to() == "reply.y");
  assert(msg.data() == "body");
  assert(!msg.is_no_responders());
  assert(msg.sequence() == 0);

  auto msg2 = natscpp::message::create("s", "", "d");
  assert(msg2.valid());
  assert(msg2.reply_to().empty());
}

void test_message_release_transfers_ownership() {
  auto msg = natscpp::message::create("s", "", "payload");
  assert(msg.valid());
  natsMsg* raw = msg.release();
  assert(raw != nullptr);
  assert(!msg.valid());
  assert(msg.native_handle() == nullptr);
  natsMsg_Destroy(raw);
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

  natscpp::kv_entry kv_entry_default;
  natscpp::kv_entry kv_entry_moved = std::move(kv_entry_default);
  natscpp::kv_entry kv_entry_assigned;
  kv_entry_assigned = std::move(kv_entry_moved);

  natscpp::kv_watcher kv_watcher_default;
  natscpp::kv_watcher kv_watcher_moved = std::move(kv_watcher_default);
  natscpp::kv_watcher kv_watcher_assigned;
  kv_watcher_assigned = std::move(kv_watcher_moved);

  natscpp::key_value kv_default;
  natscpp::key_value kv_moved = std::move(kv_default);
  natscpp::key_value kv_assigned;
  kv_assigned = std::move(kv_moved);

  assert(!pull_assigned.valid());
  assert(!push_assigned.valid());
  assert(!kv_entry_assigned.valid());
  assert(!kv_watcher_assigned.valid());
  assert(!kv_assigned.valid());
}

void test_subscription_release_callback_lifecycle() {
  int release_count = 0;

  {
    natscpp::subscription sub{nullptr, [&release_count] { ++release_count; }};
  }
  assert(release_count == 1);

  {
    natscpp::subscription lhs{nullptr, [&release_count] { release_count += 10; }};
    natscpp::subscription rhs{nullptr, [&release_count] { release_count += 100; }};
    lhs = std::move(rhs);
  }

  assert(release_count == 111);
}

void test_subscription_default_constructor() {
  natscpp::subscription sub;
  assert(!sub.valid());
  assert(!sub.is_valid());
  assert(sub.id() == -1);
  assert(sub.subject().empty());
  assert(sub.native_handle() == nullptr);
  assert(sub.drain_completion_status() == NATS_OK);
}

void test_subscription_requires_valid_handle_for_stateful_operations() {
  natscpp::subscription sub;

  auto expect_invalid_subscription = [](auto&& op) {
    try {
      op();
      assert(false && "expected nats_error");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == NATS_INVALID_ARG);
      const std::string what{ex.what()};
      assert(what.find("subscription: not initialized") != std::string::npos);
    }
  };

  expect_invalid_subscription([&] { (void)sub.next_message(std::chrono::milliseconds(1)); });
  expect_invalid_subscription([&] { sub.auto_unsubscribe(1); });
  expect_invalid_subscription([&] { (void)sub.queued_messages(); });
  expect_invalid_subscription([&] { sub.set_pending_limits(1, 1); });
  expect_invalid_subscription([&] { (void)sub.get_pending_limits(); });
  expect_invalid_subscription([&] { (void)sub.pending(); });
  expect_invalid_subscription([&] { (void)sub.delivered(); });
  expect_invalid_subscription([&] { (void)sub.dropped(); });
  expect_invalid_subscription([&] { (void)sub.max_pending(); });
  expect_invalid_subscription([&] { sub.clear_max_pending(); });
  expect_invalid_subscription([&] { (void)sub.get_stats(); });
  expect_invalid_subscription([&] { sub.no_delivery_delay(); });
  expect_invalid_subscription([&] { sub.drain(); });
  expect_invalid_subscription([&] { sub.drain(std::chrono::milliseconds(1)); });
  expect_invalid_subscription([&] { sub.wait_for_drain_completion(std::chrono::milliseconds(1)); });
  expect_invalid_subscription([&] { sub.set_on_complete_callback([] {}); });
  expect_invalid_subscription([&] { (void)sub.fetch(1, std::chrono::milliseconds(1)); });
  expect_invalid_subscription([&] {
    jsFetchRequest req{};
    req.Batch = 1;
    (void)sub.fetch_request(req);
  });
  expect_invalid_subscription([&] { (void)sub.get_consumer_info(); });
  expect_invalid_subscription([&] { (void)sub.get_sequence_mismatch(); });
}

void test_new_inbox_generates_unique_subjects() {
  natscpp::connection nc;
  const auto a = nc.new_inbox();
  const auto b = nc.new_inbox();
  assert(!a.empty());
  assert(!b.empty());
  assert(a != b);
}

void test_trace_carrier_concept_and_helpers() {
  struct map_trace_carrier {
    std::map<std::string, std::string> headers;
    void set(std::string_view key, std::string_view value) { headers[std::string(key)] = std::string(value); }
    [[nodiscard]] std::string get(std::string_view key) const {
      const auto it = headers.find(std::string(key));
      return it == headers.end() ? std::string{} : it->second;
    }
  };

  static_assert(natscpp::TraceCarrier<map_trace_carrier>);

  map_trace_carrier carrier;
  const std::vector<std::pair<std::string_view, std::string_view>> injected{
      {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736"},
      {"tracestate", "rojo=00f067aa0ba902b7"},
  };
  natscpp::inject_trace_context(carrier, injected);

  assert(carrier.get("traceparent") == "00-4bf92f3577b34da6a3ce929d0e0e4736");
  assert(carrier.get("tracestate") == "rojo=00f067aa0ba902b7");

  const std::vector<std::string_view> keys{"traceparent", "tracestate", "missing"};
  std::map<std::string, std::string> extracted;
  natscpp::extract_trace_context(carrier, keys, extracted);

  assert(extracted.size() == 2);
  assert(extracted["traceparent"] == "00-4bf92f3577b34da6a3ce929d0e0e4736");
  assert(extracted["tracestate"] == "rojo=00f067aa0ba902b7");
  assert(extracted.find("missing") == extracted.end());
}

void test_message_trace_carrier_reads_and_writes_headers() {
  auto msg = natscpp::message::create("trace.subj", "", "payload");
  natscpp::message_trace_carrier carrier{msg};

  carrier.set("traceparent", "00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-1111111111111111-01");
  assert(msg.header("traceparent") == "00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-1111111111111111-01");
  assert(carrier.get("traceparent") == "00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-1111111111111111-01");
  assert(carrier.get("missing").empty());

  const std::vector<std::pair<std::string_view, std::string_view>> injected{
      {"tracestate", "congo=t61rcWkgMzE"},
      {"baggage", "k1=v1"},
  };
  natscpp::inject_trace_context(carrier, injected);
  assert(msg.header("tracestate") == "congo=t61rcWkgMzE");
  assert(msg.header("baggage") == "k1=v1");

  const std::vector<std::string_view> keys{"traceparent", "tracestate", "baggage"};
  std::map<std::string, std::string> extracted;
  natscpp::extract_trace_context(carrier, keys, extracted);

  assert(extracted.size() == 3);
  assert(extracted["traceparent"] == "00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-1111111111111111-01");
  assert(extracted["tracestate"] == "congo=t61rcWkgMzE");
  assert(extracted["baggage"] == "k1=v1");
}

}  // namespace

namespace unit_tests {

void run_core_unit_tests() {
  test_throw_on_error_ok_does_not_throw();
  test_throw_on_error_reports_status_and_context();
  test_throw_on_error_preserves_status_code();
  test_header_wrappers();
  test_message_accessors_and_headers();
  test_message_default_constructor_null_guards();
  test_message_create_factory();
  test_message_release_transfers_ownership();
  test_future_awaitable_ready_and_resume();
  test_jetstream_and_consumers_move_semantics_on_empty_handles();
  test_subscription_release_callback_lifecycle();
  test_subscription_default_constructor();
  test_subscription_requires_valid_handle_for_stateful_operations();
  test_new_inbox_generates_unique_subjects();
  test_trace_carrier_concept_and_helpers();
  test_message_trace_carrier_reads_and_writes_headers();
}

}  // namespace unit_tests
