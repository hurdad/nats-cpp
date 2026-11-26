// The checks below are assert()s, several with side effects (waits, gets); keep them active in Release builds.
#undef NDEBUG

#include "unit_tests.hpp"

#include <natscpp/awaitable.hpp>
#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>
#include <natscpp/header.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/library.hpp>
#include <natscpp/message.hpp>
#include <natscpp/trace.hpp>

#include <cassert>
#include <chrono>
#include <coroutine>
#include <future>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
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
  assert(natscpp::nats_Open(-1) == NATS_OK);  // raw nats.c messages need the library open to be freed
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
  assert(!extracted.contains("missing"));
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

void test_header_default_state_release_and_reset() {
  // Default-constructed header is not valid.
  natscpp::header h;
  assert(!h.valid());
  assert(h.native_handle() == nullptr);
  assert(h.release() == nullptr);

  // release() transfers ownership; the wrapper becomes invalid.
  auto h2 = natscpp::header::create();
  assert(h2.valid());
  natsHeader* raw = h2.release();
  assert(raw != nullptr);
  assert(!h2.valid());
  ::natsHeader_Destroy(raw);

  // reset() with no argument invalidates a live header.
  auto h3 = natscpp::header::create();
  assert(h3.valid());
  h3.reset();
  assert(!h3.valid());

  // reset(raw) adopts an externally created header.
  natsHeader* raw2 = nullptr;
  assert(::natsHeader_New(&raw2) == NATS_OK);
  h3.reset(raw2);
  assert(h3.valid());
}

void test_jetstream_null_ctx_throws() {
  natscpp::jetstream js;  // default-constructed, ctx_ == nullptr
  assert(!js.valid());

  auto expect_invalid = [](auto&& op) {
    try {
      op();
      assert(false && "expected nats_error from null jetstream");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == NATS_INVALID_ARG);
    }
  };

  expect_invalid([&] { (void)js.publish("s", "p"); });
  expect_invalid([&] {
    auto msg = natscpp::message::create("s", "", "p");
    (void)js.publish_msg(std::move(msg));
  });
  expect_invalid([&] { (void)js.pull_subscribe("s", "d"); });
  expect_invalid([&] { (void)js.push_subscribe("s", "d"); });
  expect_invalid([&] { (void)js.create_stream(natscpp::js_stream_config{.name = "S", .subjects = {"s"}}); });
  expect_invalid([&] { (void)js.update_stream(natscpp::js_stream_config{.name = "S", .subjects = {"s"}}); });
  expect_invalid([&] { js.delete_stream("S"); });
  expect_invalid([&] { js.purge_stream("S"); });
  expect_invalid([&] { (void)js.get_stream_info("S"); });
  expect_invalid([&] { (void)js.list_streams(); });
  expect_invalid([&] { (void)js.list_stream_names(); });
  expect_invalid([&] { (void)js.get_msg("S", 1); });
  expect_invalid([&] { (void)js.get_last_msg("S", "s"); });
  expect_invalid([&] { js.delete_msg("S", 1); });
  expect_invalid([&] { js.erase_msg("S", 1); });
  expect_invalid([&] { (void)js.get_consumer_group("S", "D"); });
  expect_invalid([&] { js.delete_consumer_group("S", "D"); });
  expect_invalid([&] { (void)js.list_consumer_groups("S"); });
  expect_invalid([&] { (void)js.list_consumer_group_names("S"); });
  expect_invalid([&] { (void)js.get_account_info(); });
}

void test_jetstream_consumer_config_validation_guards() {
  // upsert_consumer_group validates stream/durable/push-deliver_subject
  // before touching ctx_, so these fire even on a default-constructed jetstream.
  natscpp::jetstream js;

  auto expect_invalid_arg = [](auto&& op) {
    try {
      op();
      assert(false && "expected std::invalid_argument");
    } catch (const std::invalid_argument&) {
      // expected
    }
  };

  // Empty stream name.
  expect_invalid_arg(
      [&] { (void)js.create_consumer_group(natscpp::js_consumer_config{.stream = "", .durable_name = "D"}); });
  // Empty durable name.
  expect_invalid_arg(
      [&] { (void)js.create_consumer_group(natscpp::js_consumer_config{.stream = "S", .durable_name = ""}); });
  // Push consumer without a deliver_subject.
  expect_invalid_arg([&] {
    (void)js.create_consumer_group(natscpp::js_consumer_config{
        .stream = "S",
        .durable_name = "D",
        .type = natscpp::js_consumer_type::push,
        // deliver_subject intentionally left empty
    });
  });
  // Same guards apply to update_consumer_group.
  expect_invalid_arg(
      [&] { (void)js.update_consumer_group(natscpp::js_consumer_config{.stream = "", .durable_name = "D"}); });
  expect_invalid_arg(
      [&] { (void)js.update_consumer_group(natscpp::js_consumer_config{.stream = "S", .durable_name = ""}); });
}

void test_js_pull_consumer_null_guard() {
  natscpp::js_pull_consumer consumer;
  assert(!consumer.valid());
  try {
    (void)consumer.next(std::chrono::milliseconds(1));
    assert(false && "expected nats_error from null js_pull_consumer");
  } catch (const natscpp::nats_error& ex) {
    assert(ex.status() == NATS_INVALID_ARG);
  }
}

void test_js_push_consumer_null_guard() {
  natscpp::js_push_consumer consumer;
  assert(!consumer.valid());
  try {
    (void)consumer.next(std::chrono::milliseconds(1));
    assert(false && "expected nats_error from null js_push_consumer");
  } catch (const natscpp::nats_error& ex) {
    assert(ex.status() == NATS_INVALID_ARG);
  }
}

void test_kv_entry_null_guards() {
  natscpp::kv_entry e;
  assert(!e.valid());
  assert(e.key().empty());
  assert(e.value().empty());
  assert(e.bucket().empty());
  assert(e.revision() == 0);
  assert(e.created() == 0);
  assert(e.delta() == 0);
  assert(e.operation() == kvOp_Put);  // default sentinel when null
}

void test_kv_watcher_null_guards() {
  natscpp::kv_watcher w;
  assert(!w.valid());

  auto expect_invalid = [](auto&& op) {
    try {
      op();
      assert(false && "expected nats_error from null kv_watcher");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == NATS_INVALID_ARG);
    }
  };

  expect_invalid([&] { (void)w.next(); });
  expect_invalid([&] { w.stop(); });
}

void test_key_value_null_guards() {
  natscpp::key_value kv;
  assert(!kv.valid());

  auto expect_invalid = [](auto&& op) {
    try {
      op();
      assert(false && "expected nats_error from null key_value");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == NATS_INVALID_ARG);
    }
  };

  expect_invalid([&] { (void)kv.watch("k"); });
  expect_invalid([&] { (void)kv.watch_multi({"k"}); });
  expect_invalid([&] { (void)kv.watch_all(); });
  expect_invalid([&] { (void)kv.get("k"); });
  expect_invalid([&] { (void)kv.put("k", "v"); });
  expect_invalid([&] { (void)kv.create("k", "v"); });
  expect_invalid([&] { (void)kv.update("k", "v", 1); });
  expect_invalid([&] { (void)kv.get_revision("k", 1); });
  expect_invalid([&] { kv.erase("k"); });
  expect_invalid([&] { kv.purge("k"); });
  expect_invalid([&] { (void)kv.keys(); });
  expect_invalid([&] { (void)kv.keys_with_filters({"*"}); });
  expect_invalid([&] { (void)kv.history("k"); });
  expect_invalid([&] { kv.purge_deletes(); });
  expect_invalid([&] { (void)kv.bucket(); });
  expect_invalid([&] { (void)kv.status(); });
}

// ---------------------------------------------------------------------------
// header: null-handle guards (check_valid() coverage)
// ---------------------------------------------------------------------------

void test_header_null_handle_throws() {
  natscpp::header h;  // default-constructed: not valid
  assert(!h.valid());

  auto expect_invalid = [](auto&& op) {
    try {
      op();
      assert(false && "expected nats_error from null header");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == NATS_INVALID_ARG);
    }
  };

  expect_invalid([&] { h.set("k", "v"); });
  expect_invalid([&] { h.add("k", "v"); });
  expect_invalid([&] { (void)h.get("k"); });
  expect_invalid([&] { (void)h.values("k"); });
  expect_invalid([&] { (void)h.keys(); });
  expect_invalid([&] { (void)h.keys_count(); });
  expect_invalid([&] { h.erase("k"); });
}

// ---------------------------------------------------------------------------
// connection: runtime payload-size guards (assert → throw)
// ---------------------------------------------------------------------------

void test_publish_oversized_payload_throws() {
  // Construct a string_view that reports size > INT_MAX without allocating real
  // memory.  publish()/sign()/request_sync() check .size() before touching the
  // data pointer, so this is safe to use purely as a size-check trigger.
  const char dummy = 'x';
  const std::string_view oversized{&dummy, static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1};

  natscpp::connection nc;  // not connected; size check fires before any I/O

  auto expect_invalid_arg = [](auto&& op, const char* ctx) {
    try {
      op();
      assert(false && "expected std::invalid_argument");
    } catch (const std::invalid_argument& ex) {
      const std::string what{ex.what()};
      assert(what.find("INT_MAX") != std::string::npos);
      (void)ctx;
    }
  };

  expect_invalid_arg([&] { nc.publish("s", oversized); }, "publish");
  expect_invalid_arg([&] { (void)nc.sign(oversized); }, "sign");
  expect_invalid_arg([&] { (void)nc.request_sync("s", oversized); }, "request_sync");
}

// ---------------------------------------------------------------------------
// message: empty and binary payloads
// ---------------------------------------------------------------------------

void test_message_empty_payload() {
  auto msg = natscpp::message::create("s", "", "");
  assert(msg.valid());
  assert(msg.data().empty());
  assert(msg.data().empty());
}

void test_message_binary_payload_with_null_bytes() {
  const char binary[] = {'\x00', '\x01', '\xFF', '\x00', '\xAB'};
  natsMsg* raw = nullptr;
  assert(natscpp::nats_Open(-1) == NATS_OK);  // raw nats.c messages need the library open to be freed
  assert(natsMsg_Create(&raw, "bin.subj", nullptr, binary, static_cast<int>(sizeof(binary))) == NATS_OK);
  natscpp::message msg{raw};
  assert(msg.valid());
  assert(msg.data().size() == 5);
  assert(msg.data() == std::string_view(binary, sizeof(binary)));  // embedded NULs preserved
}

// ---------------------------------------------------------------------------
// library.hpp: basic runtime coverage
// ---------------------------------------------------------------------------

void test_library_version_and_time_functions() {
  // Version string is non-null and non-empty.
  const char* version = natscpp::nats_GetVersion();
  assert(version != nullptr);
  assert(version[0] != '\0');

  // Version number is positive.
  const uint32_t ver_num = natscpp::nats_GetVersionNumber();
  assert(ver_num > 0);

  // Timestamps are positive.
  const int64_t now_ms = natscpp::nats_Now();
  assert(now_ms > 0);

  const int64_t now_ns = natscpp::nats_NowInNanoSeconds();
  assert(now_ns > 0);

  // Monotonic clock does not go backwards between two consecutive calls.
  const int64_t mono1 = natscpp::nats_NowMonotonicInNanoSeconds();
  const int64_t mono2 = natscpp::nats_NowMonotonicInNanoSeconds();
  assert(mono1 > 0);
  assert(mono2 >= mono1);

  // Compatibility check must pass (linked library matches header version).
  assert(natscpp::nats_CheckCompatibility());
}

// ---------------------------------------------------------------------------
// future_awaitable: suspend/resume and destroyed-before-ready
// ---------------------------------------------------------------------------

void test_future_awaitable_suspend_resumes_via_noop_handle() {
  // await_suspend spawns a thread that calls h.resume() once the future is
  // ready.  std::noop_coroutine() provides a safe no-op handle so we can
  // exercise the worker-thread path without a real coroutine frame.
  std::promise<int> p;
  natscpp::future_awaitable<int> a{p.get_future()};
  assert(!a.await_ready());
  a.await_suspend(std::noop_coroutine());
  p.set_value(42);
  // Give the detached thread time to observe the ready future and call resume.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  // Reaching here without crashing confirms the thread path executes cleanly.
}

void test_future_awaitable_destroyed_before_future_ready() {
  // Verify that destroying the awaitable before the future resolves does not
  // crash: the destructor's exchange(false) wins the CAS race and the thread
  // skips h.resume().
  std::promise<int> p;
  {
    natscpp::future_awaitable<int> a{p.get_future()};
    a.await_suspend(std::noop_coroutine());
    // Awaitable destroyed here; alive flipped to false via exchange().
  }
  p.set_value(99);  // Resolve after awaitable is gone; thread must not resume.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  // No crash or sanitizer complaint = CAS guard is working.
}

int accept_all_verify_cb(int preverify_ok, void* /*x509_store_ctx*/) { return preverify_ok; }

void test_ssl_verification_callback_setter_stores_pointer() {
  natscpp::connection_options opts;
  assert(!opts.ssl_verification_callback);
  opts.set_ssl_verification_callback(&accept_all_verify_cb);
  assert(opts.ssl_verification_callback);
  assert(*opts.ssl_verification_callback == &accept_all_verify_cb);
}

void test_ssl_verification_callback_is_applied_before_customize_options() {
  // Port 1 is never a NATS server, so connect() can only fail; what it fails with tells us whether
  // natsOptions_SetSSLVerificationCallback was called.
  natscpp::connection_options opts;
  opts.set_url("nats://127.0.0.1:1");
  opts.set_timeout(std::chrono::milliseconds(200));
  opts.set_ssl_verification_callback(&accept_all_verify_cb);
  int customize_calls = 0;
  opts.set_customize_options([&](natsOptions* nopts) {
    assert(nopts != nullptr);
    ++customize_calls;
  });

  natscpp::connection nc;
  try {
    nc.connect(opts);
    assert(false && "connect to port 1 should fail");
  } catch (const natscpp::nats_error& ex) {
    const std::string what{ex.what()};
    if (what.find("natsOptions_SetSSLVerificationCallback") != std::string::npos) {
      // nats.c built without TLS: the call reached nats.c's stub,
      // and failed before customize_options.
      assert(ex.status() == NATS_ILLEGAL_STATE);
      assert(customize_calls == 0);
    } else {
      // nats.c built with TLS: the callback was accepted and customize_options ran after it.
      assert(what.find("natsConnection_Connect") != std::string::npos);
      assert(customize_calls == 1);
    }
  }
  assert(!nc.connected());
}

void test_customize_options_runs_last_and_propagates_exceptions() {
  struct sentinel {};
  natscpp::connection_options opts;
  opts.set_url("nats://127.0.0.1:1");
  opts.set_name("before-customize");
  opts.retry_on_failed_connect = true;
  int calls = 0;
  opts.set_customize_options([&](natsOptions* nopts) {
    assert(nopts != nullptr);
    ++calls;
    throw sentinel{};  // must abort connect() before natsConnection_Connect is reached
  });

  natscpp::connection nc;
  try {
    nc.connect(opts);
    assert(false && "customize_options exception should propagate");
  } catch (const sentinel&) {
  }
  assert(calls == 1);
  assert(!nc.connected());
}

void test_moved_from_connection_subscribe_async_throws() {
  natscpp::connection a;
  natscpp::connection b = std::move(a);
  try {
    auto sub = a.subscribe_async("x", [](natscpp::message) {});  // NOLINT(bugprone-use-after-move): intended
    assert(false && "subscribe_async on a moved-from connection should throw");
  } catch (const natscpp::nats_error&) {
  }
}

void test_kv_config_history_out_of_range_throws() {
  natscpp::connection nc;  // never reached: validation happens first
  for (int64_t h : {int64_t{-1}, int64_t{256}, int64_t{300}}) {
    natscpp::kv_config cfg;
    cfg.bucket = "b";
    cfg.history = h;
    try {
      (void)natscpp::key_value::create(nc, cfg);
      assert(false && "history out of range should throw");
    } catch (const natscpp::nats_error& ex) {
      assert(ex.status() == NATS_INVALID_ARG);
      assert(std::string(ex.what()).find("history") != std::string::npos);
    }
  }
}

void test_certificate_chain_without_key_is_rejected() {
  natscpp::connection_options opts;
  opts.set_url("nats://127.0.0.1:1");
  opts.certificates_chain_file = "cert.pem";  // no private_key_file
  natscpp::connection nc;
  try {
    nc.connect(opts);
    assert(false && "connect should fail");
  } catch (const natscpp::nats_error& ex) {
    // Previously the half-set option was silently skipped and connect failed only for lack of a server.
    assert(std::string(ex.what()).find("natsOptions_LoadCertificatesChain") != std::string::npos);
  }
}

}  // namespace

namespace unit_tests {

namespace {
void test_tls_option_setters_store_values() {
  natscpp::connection_options o;
  o.load_ca_trusted_certificates("ca.pem");
  o.set_ca_certs_path("/etc/ca");
  o.load_certificates_chain("cert.pem", "key.pem");
  o.set_skip_server_verification(true);
  o.set_expected_hostname("nats.example");
  assert(o.ca_trusted_certificates_file == "ca.pem");
  assert(o.ca_certs_path == "/etc/ca");
  assert(o.certificates_chain_file == "cert.pem" && o.private_key_file == "key.pem");
  assert(o.skip_server_verification);
  assert(o.expected_hostname == "nats.example");
}
}  // namespace

std::vector<test_case> core_unit_tests() {
  return {
      NATSCPP_TEST_CASE("unit", test_throw_on_error_ok_does_not_throw),
      NATSCPP_TEST_CASE("unit", test_tls_option_setters_store_values),
      NATSCPP_TEST_CASE("unit", test_throw_on_error_reports_status_and_context),
      NATSCPP_TEST_CASE("unit", test_throw_on_error_preserves_status_code),
      NATSCPP_TEST_CASE("unit", test_header_wrappers),
      NATSCPP_TEST_CASE("unit", test_message_accessors_and_headers),
      NATSCPP_TEST_CASE("unit", test_message_default_constructor_null_guards),
      NATSCPP_TEST_CASE("unit", test_message_create_factory),
      NATSCPP_TEST_CASE("unit", test_message_release_transfers_ownership),
      NATSCPP_TEST_CASE("unit", test_future_awaitable_ready_and_resume),
      NATSCPP_TEST_CASE("unit", test_jetstream_and_consumers_move_semantics_on_empty_handles),
      NATSCPP_TEST_CASE("unit", test_subscription_release_callback_lifecycle),
      NATSCPP_TEST_CASE("unit", test_subscription_default_constructor),
      NATSCPP_TEST_CASE("unit", test_subscription_requires_valid_handle_for_stateful_operations),
      NATSCPP_TEST_CASE("unit", test_new_inbox_generates_unique_subjects),
      NATSCPP_TEST_CASE("unit", test_trace_carrier_concept_and_helpers),
      NATSCPP_TEST_CASE("unit", test_message_trace_carrier_reads_and_writes_headers),
      NATSCPP_TEST_CASE("unit", test_header_default_state_release_and_reset),
      NATSCPP_TEST_CASE("unit", test_jetstream_null_ctx_throws),
      NATSCPP_TEST_CASE("unit", test_jetstream_consumer_config_validation_guards),
      NATSCPP_TEST_CASE("unit", test_js_pull_consumer_null_guard),
      NATSCPP_TEST_CASE("unit", test_js_push_consumer_null_guard),
      NATSCPP_TEST_CASE("unit", test_kv_entry_null_guards),
      NATSCPP_TEST_CASE("unit", test_kv_watcher_null_guards),
      NATSCPP_TEST_CASE("unit", test_key_value_null_guards),
      NATSCPP_TEST_CASE("unit", test_header_null_handle_throws),
      NATSCPP_TEST_CASE("unit", test_publish_oversized_payload_throws),
      NATSCPP_TEST_CASE("unit", test_message_empty_payload),
      NATSCPP_TEST_CASE("unit", test_message_binary_payload_with_null_bytes),
      NATSCPP_TEST_CASE("unit", test_library_version_and_time_functions),
      NATSCPP_TEST_CASE("unit", test_future_awaitable_suspend_resumes_via_noop_handle),
      NATSCPP_TEST_CASE("unit", test_future_awaitable_destroyed_before_future_ready),
      NATSCPP_TEST_CASE("unit", test_ssl_verification_callback_setter_stores_pointer),
      NATSCPP_TEST_CASE("unit", test_ssl_verification_callback_is_applied_before_customize_options),
      NATSCPP_TEST_CASE("unit", test_customize_options_runs_last_and_propagates_exceptions),
      NATSCPP_TEST_CASE("unit", test_moved_from_connection_subscribe_async_throws),
      NATSCPP_TEST_CASE("unit", test_kv_config_history_out_of_range_throws),
      NATSCPP_TEST_CASE("unit", test_certificate_chain_without_key_is_rejected),
  };
}

}  // namespace unit_tests
