#include "unit_tests.hpp"

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/message.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace {

void test_connection_state_accessors_if_server_available() {
  natscpp::connection nc;
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

void test_jetstream_consumer_group_crud_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping jetstream consumer group checks (NATS server unavailable)\n";
    return;
  }

  natscpp::jetstream js(nc);
  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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
  const auto found = std::find_if(infos.begin(), infos.end(), [&](const natscpp::consumer_info& info) {
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

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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

void test_publish_request_variants_if_server_available() {
  natscpp::connection nc;
  try {
    nc.connect();
  } catch (const natscpp::nats_error&) {
    std::cerr << "[natscpp_unit_tests] skipping publish_request variants (NATS server unavailable)\n";
    return;
  }

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string subj = "natscpp.test.preq." + ts;

  auto responder = nc.subscribe_async(subj, [&nc](natscpp::message msg) {
    if (!msg.reply_to().empty()) {
      nc.publish(std::string(msg.reply_to()), msg.data());
    }
  });
  nc.flush();

  auto r1 = nc.request_string(subj, "hello-str", std::chrono::seconds(2));
  assert(r1.data() == "hello-str");

  auto req = natscpp::message::create(subj, "", "hello-msg");
  auto r2 = nc.request(std::move(req), std::chrono::seconds(2));
  assert(r2.data() == "hello-msg");

  const auto inbox1 = nc.new_inbox();
  auto inbox_sub1 = nc.subscribe_sync(inbox1);
  nc.publish_request(subj, inbox1, "hello-preq");
  auto r3 = inbox_sub1.next_message(std::chrono::seconds(2));
  assert(r3.data() == "hello-preq");

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

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string subj = "natscpp.test.queue." + ts;

  std::promise<std::string> received;
  auto sub = nc.subscribe_queue_async(subj, "workers", [&received](natscpp::message msg) {
    try {
      received.set_value(std::string(msg.data()));
    } catch (...) {
    }
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

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::string bucket = "NATSCPP_KV2_" + ts;
  const std::string key = "item-1";

  auto kv = natscpp::key_value::create(nc, bucket, 2);

  const auto r1 = kv.put(key, "put-v1");
  assert(r1 >= 1);

  auto e1 = kv.get(key);
  assert(e1.valid());
  assert(e1.key() == key);
  assert(e1.value() == "put-v1");
  assert(e1.bucket() == bucket);
  assert(e1.revision() == r1);
  assert(e1.created() != 0);
  (void)e1.delta();
  assert(e1.operation() == kvOp_Put);

  const auto r2 = kv.put(key, "put-v2");
  assert(r2 > r1);

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

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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
  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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

  (void)pull_sub.queued_messages();

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

  const auto ts = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
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

namespace unit_tests {

void run_integration_tests() {
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
}

}  // namespace unit_tests
