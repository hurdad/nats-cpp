#include "unit_tests.hpp"

#include <natscpp/connection.hpp>
#include <natscpp/header.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/library.hpp>
#include <natscpp/message.hpp>

#include <chrono>
#include <concepts>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

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

}  // namespace

namespace unit_tests {

void run_api_surface_unit_tests() { test_connection_has_sync_and_async_apis(); }

}  // namespace unit_tests
