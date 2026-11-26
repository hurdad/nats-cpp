// The checks below are assert()s, several with side effects (waits, gets); keep them active in Release builds.
#undef NDEBUG

#include "unit_tests.hpp"

#include <natscpp/connection.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/message.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Tests that need the plain JetStream nats-server at nats://127.0.0.1:4222: lifecycle callbacks,
// connection options, and regressions for bugs that only show up with live traffic.

namespace {

using namespace std::chrono_literals;

std::string unique(std::string_view prefix) {
  return std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

// Connects with opts, or reports a skip and returns nullopt.
std::optional<natscpp::connection> connect_or_skip(const natscpp::connection_options& opts, std::string_view what) {
  try {
    return std::optional<natscpp::connection>{std::in_place, opts};
  } catch (const natscpp::nats_error&) {
    unit_tests::skip(std::string(what) + " (NATS server unavailable)");
    return std::nullopt;
  }
}

template <typename T>
bool ready_within(std::future<T>& f, std::chrono::milliseconds timeout) {
  return f.wait_for(timeout) == std::future_status::ready;
}

void test_closed_and_disconnected_callbacks_fire_on_close() {
  auto closed = std::make_shared<std::promise<void>>();
  auto disconnected = std::make_shared<std::promise<void>>();
  auto closed_f = closed->get_future();
  auto disconnected_f = disconnected->get_future();

  natscpp::connection_options opts;
  opts.set_closed_cb([closed](natsConnection*) { closed->set_value(); });
  opts.set_disconnected_cb([disconnected, fired = std::make_shared<std::atomic<bool>>(false)](natsConnection*) {
    if (!fired->exchange(true)) disconnected->set_value();
  });
  auto nc = connect_or_skip(opts, "closed/disconnected callbacks");
  if (!nc) return;

  nc->close();
  assert(ready_within(disconnected_f, 2s));
  assert(ready_within(closed_f, 2s));
}

void test_reconnect_fires_disconnected_and_reconnected_callbacks() {
  auto reconnected = std::make_shared<std::promise<void>>();
  auto reconnected_f = reconnected->get_future();
  auto disconnects = std::make_shared<std::atomic<int>>(0);

  natscpp::connection_options opts;
  opts.set_reconnect_wait(50ms);
  opts.set_reconnect_jitter(0, 0);
  opts.set_disconnected_cb([disconnects](natsConnection*) { ++*disconnects; });
  opts.set_reconnected_cb([reconnected, fired = std::make_shared<std::atomic<bool>>(false)](natsConnection*) {
    if (!fired->exchange(true)) reconnected->set_value();
  });
  auto nc = connect_or_skip(opts, "reconnect callbacks");
  if (!nc) return;

  nc->reconnect();
  assert(ready_within(reconnected_f, 5s));
  assert(*disconnects >= 1);
  assert(nc->get_statistics().reconnects >= 1);

  // The connection still works after the reconnect.
  const std::string subj = unique("natscpp.reconnect.");
  auto sub = nc->subscribe_sync(subj);
  nc->publish(subj, "after-reconnect");
  assert(sub.next_message(2s).data() == "after-reconnect");
}

void test_error_handler_reports_slow_consumer() {
  auto slow = std::make_shared<std::promise<natsStatus>>();
  auto slow_f = slow->get_future();

  natscpp::connection_options opts;
  opts.set_error_handler([slow, fired = std::make_shared<std::atomic<bool>>(false)](natsConnection*, natsSubscription*,
                                                                                     natsStatus s) {
    if (!fired->exchange(true)) slow->set_value(s);
  });
  auto nc = connect_or_skip(opts, "error handler");
  if (!nc) return;

  const std::string subj = unique("natscpp.slow.");
  std::promise<void> release;
  auto release_f = release.get_future().share();
  auto sub = nc->subscribe_async(subj, [release_f](natscpp::message) { release_f.wait_for(5s); });
  sub.set_pending_limits(1, 1024);
  for (int i = 0; i < 50; ++i) nc->publish(subj, "x");
  nc->flush(2s);

  assert(ready_within(slow_f, 5s));
  assert(slow_f.get() == NATS_SLOW_CONSUMER);
  release.set_value();
  sub.unsubscribe();
}

void test_connection_options_are_applied() {
  std::atomic<int> token_calls{0};
  static std::atomic<int>* token_calls_ptr = nullptr;
  token_calls_ptr = &token_calls;

  natscpp::connection_options opts;
  // First URL is unreachable; no_randomize makes the client try them in order and land on the second.
  opts.set_urls({"nats://127.0.0.1:1", "nats://127.0.0.1:4222"});
  opts.set_no_randomize(true);
  opts.set_name("natscpp-options-test");
  opts.set_timeout(2s);
  opts.set_ping_interval(30s);
  opts.set_max_pings_out(3);
  opts.set_allow_reconnect(true);
  opts.set_max_reconnect(5);
  opts.set_reconnect_wait(100ms);
  opts.set_reconnect_jitter(10, 20);
  opts.set_reconnect_buf_size(1 << 20);
  opts.set_max_pending_msgs(1000);
  opts.set_max_pending_bytes(1 << 20);
  opts.set_io_buf_size(64 * 1024);
  opts.set_send_asap(true);
  opts.set_fail_requests_on_disconnect(true);
  opts.set_ignore_discovered_servers(true);
  opts.set_disable_no_responders(false);
  opts.set_pedantic(true);
  opts.set_verbose(true);
  opts.set_no_echo(false);
  opts.set_custom_inbox_prefix("_NATSCPP_TEST_INBOX");
  // The test server has no auth, so it ignores the token; the handler must still be consulted.
  opts.set_token_handler([](void*) -> const char* {
    ++*token_calls_ptr;
    return "ignored-token";
  }, nullptr);
  auto nc = connect_or_skip(opts, "connection options");
  if (!nc) return;

  assert(nc->connected_url().find(":4222") != std::string::npos);
  assert(token_calls >= 1);

  // custom_inbox_prefix shows up in the reply subject a request uses.
  const std::string subj = unique("natscpp.opts.");
  std::promise<std::string> seen_reply_to;
  auto seen_f = seen_reply_to.get_future();
  auto responder = nc->subscribe_async(subj, [&nc, &seen_reply_to](natscpp::message req) {
    try { seen_reply_to.set_value(std::string(req.reply_to())); } catch (...) {}
    nc->publish(req.reply_to(), "pong");
  });
  nc->flush(2s);
  assert(nc->request_sync(subj, "ping", 2s).data() == "pong");
  assert(seen_f.get().rfind("_NATSCPP_TEST_INBOX.", 0) == 0);
  responder.unsubscribe();
  token_calls_ptr = nullptr;
}

void test_old_request_style_and_connect_to_and_subscribe_queue() {
  natscpp::connection_options opts;
  opts.set_use_old_request_style(true);
  auto nc = connect_or_skip(opts, "old request style");
  if (!nc) return;

  auto responder_conn = natscpp::connection::connect_to("nats://127.0.0.1:4222");
  const std::string subj = unique("natscpp.oldstyle.");
  auto responder = responder_conn.subscribe_queue(subj, "workers", [&responder_conn](natscpp::message req) {
    responder_conn.publish(req.reply_to(), std::string("re:") + std::string(req.data()));
  });
  responder_conn.flush(2s);
  assert(nc->request_sync(subj, "a", 2s).data() == "re:a");
  assert(nc->request_sync(subj, "b", 2s).data() == "re:b");
  responder.unsubscribe();
}

void test_exception_in_handler_is_contained() {
  auto nc = connect_or_skip({}, "handler exception");
  if (!nc) return;

  const std::string subj = unique("natscpp.throw.");
  std::promise<std::string> second;
  auto second_f = second.get_future();
  std::atomic<int> calls{0};
  auto sub = nc->subscribe_async(subj, [&](natscpp::message msg) {
    if (calls++ == 0) throw std::runtime_error("handler failure");
    try { second.set_value(std::string(msg.data())); } catch (...) {}
  });
  nc->publish(subj, "first");
  nc->publish(subj, "second");
  nc->flush(2s);
  assert(ready_within(second_f, 2s));
  assert(second_f.get() == "second");
  sub.unsubscribe();
}

// Regression: move-assigning over a live async subscription used to free its handler while nats.c could
// still call it. Under ASan this loop reports a use-after-free on the old code.
void test_subscription_move_assign_under_traffic() {
  auto nc = connect_or_skip({}, "subscription move-assign under traffic");
  if (!nc) return;

  const std::string subj = unique("natscpp.moveassign.");
  std::atomic<bool> stop{false};
  // Shared, not a local reference: handlers of the replaced subscriptions may still run after this returns.
  auto received = std::make_shared<std::atomic<int>>(0);
  std::thread publisher([&] {
    auto pub = natscpp::connection::connect_to("nats://127.0.0.1:4222");
    while (!stop) {
      pub.publish(subj, "payload");
      std::this_thread::yield();
    }
    pub.flush(2s);
  });

  auto make_handler = [received] {
    auto state = std::make_shared<std::vector<int>>(64, 1);  // heap state the handler touches
    return [received, state](natscpp::message) { *received += (*state)[0]; };
  };
  natscpp::subscription sub = nc->subscribe_async(subj, make_handler());
  for (int i = 0; i < 200; ++i) {
    sub = nc->subscribe_async(subj, make_handler());
  }
  // Wait for traffic on the final subscription rather than a fixed time (slow under sanitizers).
  const int before = received->load();
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (received->load() == before && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  stop = true;
  publisher.join();
  sub.unsubscribe();
  assert(received->load() > before);
}

// Regression: a moved-from or destroyed connection used to free handlers of subscriptions still alive.
void test_subscription_outlives_connection_object() {
  auto nc = connect_or_skip({}, "subscription outliving connection object");
  if (!nc) return;

  const std::string subj = unique("natscpp.outlive.");
  std::promise<void> got;
  auto got_f = got.get_future();
  natscpp::subscription sub = nc->subscribe_async(subj, [&got](natscpp::message) {
    try { got.set_value(); } catch (...) {}
  });
  natscpp::connection moved = std::move(*nc);
  nc.reset();  // destroy the moved-from object; the handler must survive
  moved.publish(subj, "still-here");
  moved.flush(2s);
  assert(ready_within(got_f, 2s));
  sub.unsubscribe();
}

// Regression: kv_watcher::next() with no timeout used to throw NATS_INVALID_ARG every time.
void test_kv_watcher_next_without_timeout_blocks_until_update() {
  auto nc = connect_or_skip({}, "kv watcher next() without timeout");
  if (!nc) return;

  const std::string bucket = unique("NATSCPP_WATCH_");
  auto kv = natscpp::key_value::create(*nc, bucket);
  natscpp::kv_watch_options wopts;
  wopts.updates_only = true;
  auto watcher = kv.watch("k", &wopts);
  std::thread writer([&kv] {
    std::this_thread::sleep_for(200ms);
    (void)kv.put("k", "v1");
  });
  auto entry = watcher.next();
  writer.join();
  assert(entry.valid());
  assert(entry.key() == "k");
  assert(entry.value() == "v1");
  watcher.stop();
  natscpp::key_value::delete_bucket(*nc, bucket);
}

}  // namespace

namespace unit_tests {

void run_server_feature_tests() {
  test_closed_and_disconnected_callbacks_fire_on_close();
  test_reconnect_fires_disconnected_and_reconnected_callbacks();
  test_error_handler_reports_slow_consumer();
  test_connection_options_are_applied();
  test_old_request_style_and_connect_to_and_subscribe_queue();
  test_exception_in_handler_is_contained();
  test_subscription_move_assign_under_traffic();
  test_subscription_outlives_connection_object();
  test_kv_watcher_next_without_timeout_blocks_until_update();
}

}  // namespace unit_tests
