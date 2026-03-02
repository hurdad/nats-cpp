#include <natscpp/awaitable.hpp>
#include <natscpp/error.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/message.hpp>
#include <natscpp/kv.hpp>

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
  msg.add_header("x-test", "v2");
  assert(msg.header("x-test") == "v1");
  const auto values = msg.header_values("x-test");
  assert(values.size() == 2);
  const auto keys = msg.header_keys();
  assert(!keys.empty());
  msg.delete_header("x-test");
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

  natscpp::kv_entry kv_entry_default;
  natscpp::kv_entry kv_entry_moved = std::move(kv_entry_default);
  natscpp::kv_entry kv_entry_assigned;
  kv_entry_assigned = std::move(kv_entry_moved);

  natscpp::key_value kv_default;
  natscpp::key_value kv_moved = std::move(kv_default);
  natscpp::key_value kv_assigned;
  kv_assigned = std::move(kv_moved);

  assert(!pull_assigned.valid());
  assert(!push_assigned.valid());
  assert(!kv_entry_assigned.valid());
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

void test_connection_has_sync_and_async_apis() {
  natscpp::connection nc;
  natscpp::jetstream js;

  static_assert(requires { nc.subscribe_sync("subj"); });
  static_assert(requires(std::function<void(natscpp::message)> fn) { nc.subscribe_async_timeout("subj", std::chrono::milliseconds(10), fn); });
  static_assert(requires { nc.flush(std::chrono::milliseconds(10)); });
  static_assert(requires { nc.flush(); });
  static_assert(requires { nc.sign("nonce"); });
  static_assert(requires { nc.process_read_event(); });
  static_assert(requires { nc.process_write_event(); });
  static_assert(requires { nc.is_closed(); });
  static_assert(requires { nc.is_reconnecting(); });
  static_assert(requires { nc.is_draining(); });
  static_assert(requires { nc.status(); });
  static_assert(requires { nc.buffered_bytes(); });
  static_assert(requires { nc.max_payload(); });
  static_assert(requires { nc.get_statistics(); });
  static_assert(requires { nc.connected_url(); });
  static_assert(requires { nc.connected_server_id(); });
  static_assert(requires { nc.servers(); });
  static_assert(requires { nc.discovered_servers(); });
  static_assert(requires { nc.last_error(); });
  static_assert(requires { nc.client_id(); });
  static_assert(requires { nc.client_ip(); });
  static_assert(requires { nc.rtt(); });
  static_assert(requires { nc.has_header_support(); });
  static_assert(requires { nc.local_ip_and_port(); });
  static_assert(requires { nc.publish_string("subj", "payload"); });
  static_assert(requires { nc.publish_request("subj", "reply", "payload"); });
  static_assert(requires { nc.publish_request_string("subj", "reply", "payload"); });
  static_assert(requires { nc.subscribe_queue_sync("subj", "workers"); });
  static_assert(requires(std::function<void(natscpp::message)> fn) {
    nc.subscribe_queue_async_timeout("subj", "workers", std::chrono::milliseconds(10), fn);
  });
  static_assert(requires(std::function<void(natscpp::message)> fn) { nc.subscribe_async("subj", fn); });
  static_assert(requires(std::function<void(natscpp::message)> fn) {
    nc.subscribe_queue_async("subj", "workers", fn);
  });
  static_assert(requires { nc.request_sync("svc", "payload"); });
  static_assert(requires { nc.request_async("svc", "payload"); });
  static_assert(requires(natscpp::message m) { m.ack_sync(); m.nak_with_delay(std::chrono::milliseconds(10)); m.get_metadata(); });
  static_assert(requires(natscpp::subscription s, jsFetchRequest& req) {
    s.drain_completion_status();
    s.set_on_complete_callback([] {});
    s.fetch(1, std::chrono::milliseconds(10));
    s.fetch_request(req);
    s.get_consumer_info();
    s.get_sequence_mismatch();
  });
  static_assert(requires { natscpp::key_value(nc, "bucket"); });
  static_assert(requires { natscpp::key_value::create(nc, "bucket"); });
  static_assert(requires { natscpp::key_value::delete_bucket(nc, "bucket"); });
  static_assert(requires {
    js.create_stream(natscpp::js_stream_config{.name = "ORDERS", .subjects = {"orders.>"}});
  });
  static_assert(requires {
    js.create_consumer_group(natscpp::js_consumer_config{.stream = "ORDERS", .durable_name = "orders-pull"});
  });
  static_assert(requires {
    js.push_subscribe("orders.created", "orders-push");
  });
}

void test_kv_bucket_and_key_crud_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping kv checks (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string bucket = "NATSCPP_KV_" + ts;
  const std::string key = "user-1";

  auto kv = natscpp::key_value::create(nc, bucket);
  assert(kv.valid());
  assert(kv.bucket() == bucket);

  auto r1 = kv.create(key, "v1");
  auto e1 = kv.get(key);
  assert(e1.key() == key);
  assert(e1.value() == "v1");
  assert(e1.bucket() == bucket);

  auto r2 = kv.update(key, "v2", r1);
  assert(r2 > r1);
  auto e2 = kv.get_revision(key, r1);
  assert(e2.value() == "v1");

  auto ks = kv.keys();
  assert(!ks.empty());

  auto st = kv.status();
  assert(st.bucket == bucket);
  assert(st.values >= 1);

  kv.purge(key);
  natscpp::key_value::delete_bucket(nc, bucket);
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

  nc.flush();
  nc.flush(std::chrono::milliseconds(250));
  (void)nc.buffered_bytes();
  (void)nc.max_payload();
  (void)nc.get_statistics();
  (void)nc.connected_url();
  (void)nc.connected_server_id();
  (void)nc.servers();
  (void)nc.discovered_servers();
  (void)nc.last_error();
  (void)nc.has_header_support();
  (void)nc.local_ip_and_port();
  auto sync_sub = nc.subscribe_sync(sync_subject);
  nc.publish(sync_subject, "hello-sync");
  auto sync_msg = sync_sub.next_message(std::chrono::seconds(2));
  assert(sync_msg.data() == "hello-sync");
  sync_sub.auto_unsubscribe(100);
  (void)sync_sub.id();
  (void)sync_sub.subject();
  (void)sync_sub.queued_messages();
  sync_sub.set_pending_limits(1000, 1024 * 1024);
  (void)sync_sub.get_pending_limits();
  (void)sync_sub.pending();
  (void)sync_sub.delivered();
  (void)sync_sub.dropped();
  (void)sync_sub.max_pending();
  sync_sub.clear_max_pending();
  (void)sync_sub.get_stats();

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
  test_subscription_release_callback_lifecycle();
  test_connection_has_sync_and_async_apis();
  test_connection_sync_and_async_roundtrip_if_server_available();
  test_kv_bucket_and_key_crud_if_server_available();
  return 0;
}
