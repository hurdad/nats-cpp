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

  auto kv = natscpp::key_value::create(nc, bucket, 2);
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

// ---------------------------------------------------------------------------
// Additional unit tests (no server required)
// ---------------------------------------------------------------------------

void test_throw_on_error_preserves_status_code() {
  // Each non-OK status must be carried in the exception.
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

void test_message_default_constructor_null_guards() {
  // Default-constructed message is invalid; every accessor returns a safe empty value.
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
  // Mutating operations on an invalid message must be silent no-ops.
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
  // sequence and timestamp are 0 for a non-JetStream message.
  assert(msg.sequence() == 0);

  // Empty reply_to is passed as nullptr to natsMsg_Create.
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

void test_subscription_default_constructor() {
  natscpp::subscription sub;
  assert(!sub.valid());
  assert(!sub.is_valid());
  assert(sub.id() == -1);
  assert(sub.subject().empty());
  assert(sub.native_handle() == nullptr);
  // drain_completion_status() on a null sub must return NATS_OK (null guard).
  assert(sub.drain_completion_status() == NATS_OK);
}

void test_new_inbox_generates_unique_subjects() {
  // natsInbox_Create is a standalone function; new_inbox() does not touch conn_.
  natscpp::connection nc;
  const auto a = nc.new_inbox();
  const auto b = nc.new_inbox();
  assert(!a.empty());
  assert(!b.empty());
  assert(a != b);
}

// ---------------------------------------------------------------------------
// Additional integration tests (skip if no NATS server)
// ---------------------------------------------------------------------------

void test_connection_state_accessors_if_server_available() {
  natscpp::connection nc;

  // Before connect: closed / not connected.
  assert(!nc.connected());
  assert(nc.is_closed());
  assert(!nc.is_reconnecting());
  assert(!nc.is_draining());
  assert(nc.status() == NATS_CONN_STATUS_CLOSED);
  assert(nc.buffered_bytes() == 0);
  assert(nc.max_payload() == 0);

  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping connection state checks (NATS server unavailable)\n";
    return;
  }

  assert(nc.connected());
  assert(!nc.is_closed());
  assert(!nc.is_reconnecting());
  assert(!nc.is_draining());
  assert(nc.status() == NATS_CONN_STATUS_CONNECTED);
  assert(nc.max_payload() > 0);
  assert(nc.client_id() > 0);
  assert(!nc.client_ip().empty());
  assert(nc.rtt().count() >= 0);

  nc.close();
  assert(nc.is_closed());
  assert(nc.status() == NATS_CONN_STATUS_CLOSED);
}

void test_publish_request_variants_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping publish_request variants (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string subj = "natscpp.test.preq." + ts;

  // Echo responder.
  auto responder = nc.subscribe_async(subj, [&nc](natscpp::message msg) {
    if (!msg.reply_to().empty()) {
      nc.publish(std::string(msg.reply_to()), msg.data());
    }
  });

  // request_string
  auto r1 = nc.request_string(subj, "hello-str", std::chrono::seconds(2));
  assert(r1.data() == "hello-str");

  // request(message) overload
  auto req = natscpp::message::create(subj, "", "hello-msg");
  auto r2 = nc.request(std::move(req), std::chrono::seconds(2));
  assert(r2.data() == "hello-msg");

  // publish_request: subscribe to an explicit inbox, then publish with that reply-to
  const auto inbox1 = nc.new_inbox();
  auto inbox_sub1 = nc.subscribe_sync(inbox1);
  nc.publish_request(subj, inbox1, "hello-preq");
  auto r3 = inbox_sub1.next_message(std::chrono::seconds(2));
  assert(r3.data() == "hello-preq");

  // publish_request_string
  const auto inbox2 = nc.new_inbox();
  auto inbox_sub2 = nc.subscribe_sync(inbox2);
  nc.publish_request_string(subj, inbox2, "hello-preqs");
  auto r4 = inbox_sub2.next_message(std::chrono::seconds(2));
  assert(r4.data() == "hello-preqs");

  responder.unsubscribe();
}

void test_queue_subscription_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping queue subscription checks (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string subj = "natscpp.test.queue." + ts;

  std::promise<std::string> received;
  auto sub = nc.subscribe_queue_async(subj, "workers", [&received](natscpp::message msg) {
    try { received.set_value(std::string(msg.data())); } catch (...) {}
  });

  nc.publish(subj, "queued-msg");
  assert(received.get_future().get() == "queued-msg");
  sub.unsubscribe();
}

void test_subscription_drain_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping subscription drain checks (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string subj = "natscpp.test.drain." + ts;

  auto sub = nc.subscribe_async(subj, [](natscpp::message) {});
  sub.no_delivery_delay();
  sub.drain(std::chrono::milliseconds(500));
  sub.wait_for_drain_completion(std::chrono::milliseconds(500));
  assert(sub.drain_completion_status() == NATS_OK);
}

void test_kv_put_erase_and_entry_fields_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping kv put/erase/fields checks (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string bucket = "NATSCPP_KV2_" + ts;
  const std::string key = "item-1";

  auto kv = natscpp::key_value::create(nc, bucket, 2);

  // put: create or overwrite
  const auto r1 = kv.put(key, "put-v1");
  assert(r1 >= 1);

  auto e1 = kv.get(key);
  assert(e1.valid());
  assert(e1.key() == key);
  assert(e1.value() == "put-v1");
  assert(e1.bucket() == bucket);
  assert(e1.revision() == r1);
  assert(e1.created() != 0);
  (void)e1.delta();             // valid to call; value is context-dependent
  assert(e1.operation() == kvOp_Put);

  // put again to create a second revision
  const auto r2 = kv.put(key, "put-v2");
  assert(r2 > r1);

  // erase: marks key deleted (tombstone); subsequent get must throw NATS_NOT_FOUND
  kv.erase(key);
  try {
    (void)kv.get(key);
    assert(false && "expected NATS_NOT_FOUND after erase");
  } catch (const natscpp::nats_error& ex) {
    assert(ex.status() == NATS_NOT_FOUND);
  }

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
  test_throw_on_error_preserves_status_code();
  test_message_accessors_and_headers();
  test_message_default_constructor_null_guards();
  test_message_create_factory();
  test_message_release_transfers_ownership();
  test_future_awaitable_ready_and_resume();
  test_jetstream_and_consumers_move_semantics_on_empty_handles();
  test_subscription_release_callback_lifecycle();
  test_subscription_default_constructor();
  test_new_inbox_generates_unique_subjects();
  test_connection_has_sync_and_async_apis();
  test_connection_state_accessors_if_server_available();
  test_connection_sync_and_async_roundtrip_if_server_available();
  test_publish_request_variants_if_server_available();
  test_queue_subscription_if_server_available();
  test_subscription_drain_if_server_available();
  test_kv_bucket_and_key_crud_if_server_available();
  test_kv_put_erase_and_entry_fields_if_server_available();
  return 0;
}
