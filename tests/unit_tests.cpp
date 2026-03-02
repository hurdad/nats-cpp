#include <natscpp/awaitable.hpp>
#include <natscpp/error.hpp>
#include <natscpp/header.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/message.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/library.hpp>
#include <natscpp/trace.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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
  static_assert(requires(natscpp::connection_options opts, natsSignatureHandler sig_cb, void* closure,
                         std::function<void(natsConnection*)> conn_cb,
                         std::function<void(natsConnection*, natsSubscription*, natsStatus)> err_cb) {
    opts.set_token("token");
    opts.set_nkey("PUB", sig_cb, closure);
    opts.set_user_credentials_from_files("user.creds", "seed.txt");
    opts.set_user_info("user", "pwd");
    opts.set_secure(true);
    opts.load_ca_trusted_certificates("ca.pem");
    opts.load_certificates_chain("cert.pem", "key.pem");
    opts.set_skip_server_verification(true);
    opts.set_closed_cb(conn_cb);
    opts.set_disconnected_cb(conn_cb);
    opts.set_reconnected_cb(conn_cb);
    opts.set_error_handler(err_cb);
    opts.set_lame_duck_mode_cb(conn_cb);
    opts.set_reconnect_wait(std::chrono::milliseconds(100));
    opts.set_max_reconnect(10);
    opts.set_ping_interval(std::chrono::seconds(1));
    opts.set_timeout(std::chrono::seconds(2));
    opts.set_name("cpp-client");
    opts.set_no_echo(true);
  });
  static_assert(requires(natscpp::message m) { m.ack_sync(); m.nak_with_delay(std::chrono::milliseconds(10)); m.get_metadata(); });
  static_assert(requires(char* buffer, size_t buf_len, FILE* stream, natsClientConfig* cfg,
                         unsigned char** signature, int* signature_length) {
    natscpp::nats_CheckCompatibility();
    natscpp::nats_CheckCompatibilityImpl(1, 1, "1.0.0");
    natscpp::nats_Close();
    natscpp::nats_CloseAndWait(0);
    natscpp::nats_CloseAndWait(std::chrono::milliseconds(1));
    natscpp::nats_GetLastErrorStack(buffer, buf_len);
    natscpp::nats_GetVersion();
    natscpp::nats_GetVersionNumber();
    natscpp::nats_Now();
    natscpp::nats_NowInNanoSeconds();
    natscpp::nats_NowMonotonicInNanoSeconds();
    natscpp::nats_Open(100);
    natscpp::nats_OpenWithConfig(cfg);
    natscpp::nats_PrintLastErrorStack(stream);
    natscpp::nats_ReleaseThreadMemory();
    natscpp::nats_SetMessageDeliveryPoolSize(4);
    natscpp::nats_Sign("seed", "nonce", signature, signature_length);
    natscpp::nats_Sleep(1);
    natscpp::nats_Sleep(std::chrono::milliseconds(1));
  });
  static_assert(requires(natscpp::header h) {
    { natscpp::header::create() } -> std::same_as<natscpp::header>;
    { natscpp::natsHeader_New() } -> std::same_as<natscpp::header>;
    h.set("k", "v");
    h.add("k", "v2");
    { h.get("k") } -> std::convertible_to<std::string>;
    { h.values("k") } -> std::same_as<std::vector<std::string>>;
    { h.keys() } -> std::same_as<std::vector<std::string>>;
    { h.keys_count() } -> std::same_as<int>;
    h.erase("k");
    h.destroy();
    natscpp::natsHeader_Set(h, "k", "v");
    natscpp::natsHeader_Add(h, "k", "v2");
    { natscpp::natsHeader_Get(h, "k") } -> std::convertible_to<std::string>;
    { natscpp::natsHeader_Values(h, "k") } -> std::same_as<std::vector<std::string>>;
    { natscpp::natsHeader_Keys(h) } -> std::same_as<std::vector<std::string>>;
    { natscpp::natsHeader_KeysCount(h) } -> std::same_as<int>;
    natscpp::natsHeader_Delete(h, "k");
    natscpp::natsHeader_Destroy(h);
  });
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
  static_assert(requires(natscpp::key_value kv) {
    kv.watch("key");
    kv.watch_multi(std::vector<std::string>{"a", "b"});
    kv.watch_all();
  });
  static_assert(requires(natscpp::kv_watcher watcher) {
    watcher.next();
    watcher.stop();
  });
  static_assert(requires {
    js.create_stream(natscpp::js_stream_config{.name = "ORDERS", .subjects = {"orders.>"}});
  });
  static_assert(requires {
    js.create_consumer_group(natscpp::js_consumer_config{.stream = "ORDERS", .durable_name = "orders-pull"});
  });
  static_assert(requires {
    js.update_consumer_group(natscpp::js_consumer_config{.stream = "ORDERS", .durable_name = "orders-pull"});
  });
  static_assert(requires {
    js.get_consumer_group("ORDERS", "orders-pull");
    js.delete_consumer_group("ORDERS", "orders-pull");
    js.list_consumer_groups("ORDERS");
    js.list_consumer_group_names("ORDERS");
  });
  static_assert(requires {
    js.push_subscribe("orders.created", "orders-push");
  });
}

void test_jetstream_consumer_group_crud_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping jetstream consumer group checks (NATS server unavailable)\n";
    return;
  }

  natscpp::jetstream js(nc);
  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string stream = "NATSCPP_CG_" + ts;
  const std::string subject = "natscpp.cg." + ts + ".>";
  const std::string durable = "cg" + ts;

  auto created_stream = js.create_stream(natscpp::js_stream_config{.name = stream, .subjects = {subject}});
  assert(created_stream.name == stream);

  auto created = js.create_consumer_group(natscpp::js_consumer_config{
      .stream = stream,
      .durable_name = durable,
      .filter_subject = "natscpp.cg." + ts + ".created",
      .type = natscpp::js_consumer_type::pull,
  });
  assert(created.stream_name == stream);
  assert(created.durable_name == durable);

  auto fetched = js.get_consumer_group(stream, durable);
  assert(fetched.stream_name == stream);
  assert(fetched.durable_name == durable);

  auto names = js.list_consumer_group_names(stream);
  assert(!names.empty());
  assert(std::find(names.begin(), names.end(), durable) != names.end());

  auto infos = js.list_consumer_groups(stream);
  assert(!infos.empty());
  auto found = std::find_if(infos.begin(), infos.end(), [&](const natscpp::consumer_info& info) {
    return info.durable_name == durable && info.stream_name == stream;
  });
  assert(found != infos.end());

  auto updated = js.update_consumer_group(natscpp::js_consumer_config{
      .stream = stream,
      .durable_name = durable,
      .filter_subject = "natscpp.cg." + ts + ".updated",
      .type = natscpp::js_consumer_type::pull,
  });
  assert(updated.stream_name == stream);
  assert(updated.durable_name == durable);

  js.delete_consumer_group(stream, durable);
  try {
    (void)js.get_consumer_group(stream, durable);
    assert(false && "expected get_consumer_group to fail after delete");
  } catch (const natscpp::nats_error& ex) {
    assert(ex.status() != NATS_OK);
  }

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

void test_trace_carrier_concept_and_helpers() {
  struct map_trace_carrier {
    std::map<std::string, std::string> headers;

    void set(std::string_view key, std::string_view value) { headers[std::string(key)] = std::string(value); }
    [[nodiscard]] std::string get(std::string_view key) const {
      auto it = headers.find(std::string(key));
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
  nc.flush();  // ensure server acknowledges the subscription before first request

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

void test_kv_watchers_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping kv watcher checks (NATS server unavailable)\n";
    return;
  }

  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string bucket = "NATSCPP_KVW_" + ts;

  auto kv = natscpp::key_value::create(nc, bucket, 2);

  natscpp::kv_watch_options opts;
  opts.updates_only = true;

  auto key_watch = kv.watch("user.1", &opts);
  const auto watch_r1 = kv.put("user.1", "first");
  assert(watch_r1 >= 1);
  auto key_entry = key_watch.next(2000);
  assert(key_entry.valid());
  assert(key_entry.key() == "user.1");
  assert(key_entry.value() == "first");

  auto all_watch = kv.watch_all(&opts);
  const auto watch_r2 = kv.put("user.2", "second");
  assert(watch_r2 > watch_r1);
  auto all_entry = all_watch.next(2000);
  assert(all_entry.valid());
  assert(all_entry.key() == "user.2");
  assert(all_entry.value() == "second");

  auto multi_watch = kv.watch_multi({"user.3", "user.4"}, &opts);
  const auto watch_r3 = kv.put("user.4", "fourth");
  assert(watch_r3 > watch_r2);
  auto multi_entry = multi_watch.next(2000);
  assert(multi_entry.valid());
  assert(multi_entry.key() == "user.4");
  assert(multi_entry.value() == "fourth");

  key_watch.stop();
  all_watch.stop();
  multi_watch.stop();

  natscpp::key_value::delete_bucket(nc, bucket);
}

void test_jetstream_publish_subscribe_ack_and_subscription_helpers_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping jetstream publish/subscription checks (NATS server unavailable)\n";
    return;
  }

  natscpp::jetstream js(nc);
  auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string stream = "NATSCPP_JS_" + ts;
  const std::string subject = "natscpp.js." + ts;
  const std::string pull_durable = "pull" + ts;
  const std::string push_durable = "push" + ts;
  const std::string alias_durable = "alias" + ts;

  auto created_stream = js.create_stream(natscpp::js_stream_config{.name = stream, .subjects = {subject}});
  assert(created_stream.name == stream);

  auto created_pull = js.create_consumer_group(natscpp::js_consumer_config{
      .stream = stream,
      .durable_name = pull_durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::pull,
  });
  assert(created_pull.stream_name == stream);

  auto created_push = js.create_consumer_group(natscpp::js_consumer_config{
      .stream = stream,
      .durable_name = push_durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::push,
  });
  assert(created_push.stream_name == stream);

  auto created_alias = js.create_consumer_group(natscpp::js_consumer_config{
      .stream = stream,
      .durable_name = alias_durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::push,
  });
  assert(created_alias.stream_name == stream);

  for (int i = 0; i < 8; ++i) {
    natscpp::js_publish_options opts;
    opts.msg_id = "msg-" + ts + "-" + std::to_string(i);
    opts.expected_stream = stream;
    js.publish(subject, "payload-" + std::to_string(i), opts);
  }

  auto pull_consumer = js.pull_subscribe(subject, pull_durable);
  auto pull_msg1 = pull_consumer.next(std::chrono::seconds(2));
  assert(!pull_msg1.data().empty());
  auto md = pull_msg1.get_metadata();
  assert(md.stream == stream);
  assert(md.consumer == pull_durable);
  assert(md.sequence_stream >= 1);
  assert(md.timestamp > 0);
  assert(pull_msg1.sequence() >= 1);
  assert(pull_msg1.timestamp() > 0);
  pull_msg1.in_progress();
  pull_msg1.ack_sync();

  auto pull_msg2 = pull_consumer.next(std::chrono::seconds(2));
  pull_msg2.ack();

  auto pull_msg3 = pull_consumer.next(std::chrono::seconds(2));
  pull_msg3.nak();

  auto pull_msg4 = pull_consumer.next(std::chrono::seconds(2));
  pull_msg4.nak_with_delay(std::chrono::milliseconds(1));

  auto pull_msg5 = pull_consumer.next(std::chrono::seconds(2));
  pull_msg5.term();

  auto push_consumer = js.push_subscribe(subject, push_durable);
  auto push_msg = push_consumer.next(std::chrono::seconds(2));
  assert(!push_msg.data().empty());
  push_msg.ack();

  auto push_alias_consumer = js.subscribe(subject, alias_durable);
  auto alias_msg = push_alias_consumer.next(std::chrono::seconds(2));
  assert(!alias_msg.data().empty());
  alias_msg.ack();

  jsCtx* raw_ctx = nullptr;
  assert(natsConnection_JetStream(&raw_ctx, nc.native_handle(), nullptr) == NATS_OK);
  std::unique_ptr<jsCtx, void (*)(jsCtx*)> ctx_holder(raw_ctx, jsCtx_Destroy);

  natsSubscription* raw_pull_sub = nullptr;
  const std::string helper_durable = "helper" + ts;
  assert(js_PullSubscribe(&raw_pull_sub, raw_ctx, subject.c_str(), helper_durable.c_str(), nullptr, nullptr, nullptr) ==
         NATS_OK);
  natscpp::subscription pull_sub(raw_pull_sub);

  for (int i = 8; i < 11; ++i) {
    js.publish(subject, "payload-" + std::to_string(i));
  }

  const auto queued_before = pull_sub.queued_messages();
  (void)queued_before;

  auto fetched = pull_sub.fetch(2, std::chrono::seconds(2));
  assert(!fetched.empty());
  for (auto& msg : fetched) {
    msg.ack();
  }

  jsFetchRequest req{};
  req.Batch = 1;
  req.Expires = std::chrono::milliseconds(500).count() * 1000 * 1000;
  auto fetched_req = pull_sub.fetch_request(req);
  assert(!fetched_req.empty());
  fetched_req.front().ack();

  auto info = pull_sub.get_consumer_info();
  assert(info.stream == stream);
  assert(info.name == helper_durable);

  auto mismatch = pull_sub.get_sequence_mismatch();
  assert(!mismatch.has_value());

  pull_sub.unsubscribe();
  assert(!pull_sub.valid());
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
  test_header_wrappers();
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
  test_trace_carrier_concept_and_helpers();
  test_message_trace_carrier_reads_and_writes_headers();
  test_connection_state_accessors_if_server_available();
  test_connection_sync_and_async_roundtrip_if_server_available();
  test_publish_request_variants_if_server_available();
  test_queue_subscription_if_server_available();
  test_subscription_drain_if_server_available();
  test_jetstream_consumer_group_crud_if_server_available();
  test_kv_bucket_and_key_crud_if_server_available();
  test_kv_put_erase_and_entry_fields_if_server_available();
  test_kv_watchers_if_server_available();
  test_jetstream_publish_subscribe_ack_and_subscription_helpers_if_server_available();
  return 0;
}
