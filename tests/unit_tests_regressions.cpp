// The checks below are assert()s, several with side effects (waits, gets); keep them active in Release builds.
#undef NDEBUG

#include "unit_tests.hpp"

#include <natscpp/awaitable.hpp>
#include <natscpp/connection.hpp>
#include <natscpp/header.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/library.hpp>
#include <natscpp/message.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// Regression tests for the lifetime and correctness bugs fixed after the audit, plus edge cases (validation,
// error paths, self-move-assignment) the other files do not reach. Run under ASan/UBSan in CI, where the
// lifetime tests turn a use-after-free into a failure; without sanitizers they still check that every
// callback runs exactly when it should.

namespace {

using namespace std::chrono_literals;

std::string unique(std::string_view prefix) {
  return std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::optional<natscpp::connection> connect_or_skip(std::string_view what, natscpp::connection_options opts = {}) {
  try {
    return std::optional<natscpp::connection>{std::in_place, opts};
  } catch (const natscpp::nats_error&) {
    unit_tests::skip(std::string(what) + " (NATS server unavailable)");
    return std::nullopt;
  }
}

template <typename Fn>
natsStatus status_of(Fn&& fn) {
  try {
    fn();
  } catch (const natscpp::nats_error& e) {
    return e.status();
  }
  return NATS_OK;
}

template <typename Fn>
bool throws_invalid_argument(Fn&& fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

// Moves obj into itself through an alias (a direct x = std::move(x) is a compile-time warning).
template <typename T>
void self_move_assign(T& obj) {
  T& alias = obj;
  obj = std::move(alias);
}

template <typename Pred>
bool eventually(Pred&& pred, std::chrono::milliseconds timeout = 2s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Offline: no server needed
// ---------------------------------------------------------------------------

void test_credentials_setter_leaves_seed_unset_by_default() {
  natscpp::connection_options opts;
  opts.set_user_credentials_from_files("user.creds");
  assert(opts.user_credentials_file == "user.creds");
  assert(!opts.user_nkey_seed_file.has_value());  // was "": nats.c then tried to open a file named ""

  opts.set_user_credentials_from_files("user.jwt", "user.nk");
  assert(opts.user_nkey_seed_file == "user.nk");
  opts.set_user_credentials_from_files("chained.creds");
  assert(!opts.user_nkey_seed_file.has_value());
}

void test_error_text_omits_unrelated_last_error() {
  // Leave a thread-local nats.c error behind, then report a different status.
  assert(natsConnection_Publish(nullptr, "subj", "x", 1) == NATS_INVALID_ARG);
  try {
    natscpp::throw_on_error(NATS_TIMEOUT, "ctx");
    assert(false);
  } catch (const natscpp::nats_error& e) {
    const std::string what = e.what();
    assert(e.status() == NATS_TIMEOUT);
    assert(what.rfind("ctx: ", 0) == 0);
    assert(what.find('|') == std::string::npos);  // no stale "Invalid Argument" detail appended
  }
  try {
    natscpp::throw_on_error(NATS_ERR, nullptr);
    assert(false);
  } catch (const natscpp::nats_error& e) {
    assert(std::string(e.what()).rfind("nats error", 0) == 0);
  }
}

void test_future_awaitable_is_move_only() {
  static_assert(!std::is_copy_constructible_v<natscpp::future_awaitable<int>>);
  static_assert(!std::is_copy_assignable_v<natscpp::future_awaitable<int>>);
  static_assert(std::is_nothrow_move_constructible_v<natscpp::future_awaitable<int>>);

  std::promise<int> p;
  natscpp::future_awaitable<int> a{p.get_future()};
  natscpp::future_awaitable<int> b{std::move(a)};  // a is now moved-from and must destroy cleanly
  p.set_value(7);
  assert(b.await_ready());
  assert(b.await_resume() == 7);
}

void test_compatibility_check_keeps_nats_macro() {
#ifdef nats_CheckCompatibility
  assert(nats_CheckCompatibility());  // nats.c's own macro is still usable after including natscpp
#endif
  assert(natscpp::nats_CheckCompatibility());
}

void test_invalid_objects_reject_operations() {
  natscpp::message msg;
  assert(status_of([&] { msg.ack(); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.ack_sync(); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.nak(); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.nak_with_delay(1ms); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.in_progress(); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.term(); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)msg.get_metadata(); }) == NATS_INVALID_ARG);
  assert(!msg.is_no_responders());
  assert(msg.timestamp() == 0 && msg.sequence() == 0);

  natscpp::jetstream js;
  assert(status_of([&] { (void)js.create_consumer_group({.stream = "S", .durable_name = "d"}); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.subscribe_async("s", "d", [](natscpp::message) {}); }) == NATS_INVALID_ARG);

  natscpp::js_pull_consumer pull;
  assert(pull.native_handle() == nullptr);
  assert(status_of([&] { (void)pull.fetch(5); }) == NATS_INVALID_ARG);
  natscpp::js_push_consumer push;
  assert(push.native_handle() == nullptr);

  natscpp::subscription sub;
  assert(status_of([&] { sub.set_on_complete_callback([] {}); }) == NATS_INVALID_ARG);
}

void test_self_move_assignment_is_a_no_op() {
  natscpp::subscription sub;
  self_move_assign(sub);
  assert(!sub.valid());
  natscpp::js_pull_consumer pull;
  self_move_assign(pull);
  natscpp::js_push_consumer push;
  self_move_assign(push);
  natscpp::jetstream js;
  self_move_assign(js);
  natscpp::kv_entry entry;
  self_move_assign(entry);
  natscpp::kv_watcher watcher;
  self_move_assign(watcher);
  natscpp::key_value kv;
  self_move_assign(kv);
  assert(!pull.valid() && !push.valid() && !js.valid() && !entry.valid() && !watcher.valid() && !kv.valid());
}

void test_header_errors() {
  auto h = natscpp::header::create();
  h.set("k", "v");
  assert(status_of([&] { (void)h.get("missing"); }) == NATS_NOT_FOUND);
  assert(status_of([&] { (void)h.values("missing"); }) == NATS_NOT_FOUND);
  assert(status_of([&] { h.erase("missing"); }) == NATS_NOT_FOUND);

  auto msg = natscpp::message::create("subj", "reply.to", "payload");
  assert(msg.reply_to() == "reply.to");
  assert(msg.header("missing").empty());
  assert(status_of([&] { (void)msg.header_values("missing"); }) == NATS_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// Server-backed: lifetime regressions
// ---------------------------------------------------------------------------

// Destroying an async subscription while its handler runs must not free the handler under it.
void test_subscription_destroyed_while_handler_runs() {
  auto nc = connect_or_skip("subscription destroyed while handler runs");
  if (!nc) return;
  const auto subject = unique("natscpp.regress.inflight.");
  std::atomic<bool> entered{false};
  std::atomic<bool> finished{false};
  {
    std::string capture(64, 'x');  // owned by the handler; ASan flags a read after it is freed
    auto sub = nc->subscribe_async(subject, [capture, &entered, &finished](natscpp::message) {
      entered = true;
      std::this_thread::sleep_for(100ms);
      assert(capture.size() == 64 && capture[10] == 'x');
      finished = true;
    });
    nc->publish(subject, "hi");
    nc->flush(1s);
    assert(eventually([&] { return entered.load(); }));
  }  // destroyed mid-handler
  assert(eventually([&] { return finished.load(); }));
}

// A handler that unsubscribes its own subscription keeps running safely to the end.
void test_handler_unsubscribes_itself() {
  auto nc = connect_or_skip("handler unsubscribes itself");
  if (!nc) return;
  const auto subject = unique("natscpp.regress.selfunsub.");
  auto holder = std::make_shared<std::optional<natscpp::subscription>>();
  std::atomic<bool> done{false};
  std::string capture(64, 'y');
  *holder = nc->subscribe_async(subject, [holder, capture, &done](natscpp::message) {
    holder->reset();  // destroys the subscription, and with it natscpp's reference to this handler
    assert(capture.size() == 64 && capture[10] == 'y');
    done = true;
  });
  nc->publish(subject, "hi");
  nc->flush(1s);
  assert(eventually([&] { return done.load(); }));
}

// The on-complete callback survives its subscription and runs exactly once.
void test_on_complete_runs_after_subscription_is_gone() {
  auto nc = connect_or_skip("on-complete after subscription destroyed");
  if (!nc) return;
  const auto subject = unique("natscpp.regress.complete.");
  auto fired = std::make_shared<std::atomic<int>>(0);
  constexpr int rounds = 20;
  for (int i = 0; i < rounds; ++i) {
    auto sub = nc->subscribe_async(subject, [](natscpp::message) {});
    std::string capture(64, 'z');
    sub.set_on_complete_callback([capture, fired] {
      assert(capture[10] == 'z');
      ++*fired;
    });
    sub.unsubscribe();
  }  // each subscription is destroyed before nats.c runs its callback
  assert(eventually([&] { return fired->load() == rounds; }));
  std::this_thread::sleep_for(20ms);
  assert(fired->load() == rounds);  // and never twice
}

// on-complete is rejected for sync subscriptions and after completion; a throwing callback is contained.
void test_on_complete_rules() {
  auto nc = connect_or_skip("on-complete rules");
  if (!nc) return;
  const auto subject = unique("natscpp.regress.completerules.");

  auto sync_sub = nc->subscribe_sync(subject);
  assert(status_of([&] { sync_sub.set_on_complete_callback([] {}); }) == NATS_INVALID_SUBSCRIPTION);

  std::promise<void> completed;
  auto sub = nc->subscribe_async(subject, [](natscpp::message) {});
  sub.set_on_complete_callback([&completed] {
    completed.set_value();
    throw std::runtime_error("ignored");  // must not escape into nats.c
  });
  sub.auto_unsubscribe(1);
  nc->publish(subject, "one");
  assert(completed.get_future().wait_for(2s) == std::future_status::ready);
  assert(eventually([&] { return status_of([&] { sub.set_on_complete_callback([] {}); }) != NATS_OK; }));
  assert(status_of([&] { sub.set_on_complete_callback([] {}); }) == NATS_INVALID_SUBSCRIPTION);
}

// The closed callback runs after the connection object is gone, with its captures still alive.
void test_closed_callback_after_connection_destroyed() {
  auto fired = std::make_shared<std::atomic<int>>(0);
  constexpr int rounds = 10;
  for (int i = 0; i < rounds; ++i) {
    natscpp::connection_options opts;
    std::string capture(64, 'w');
    opts.set_closed_cb([capture, fired](natsConnection*) {
      assert(capture[10] == 'w');
      ++*fired;
    });
    auto nc = connect_or_skip("closed callback after destroy", opts);
    if (!nc) return;
  }  // each connection destroyed without waiting for its callback
  assert(eventually([&] { return fired->load() == rounds; }));
}

// A failed connect frees its handlers itself (nats.c runs no closed callback then); LSan checks the leak.
void test_failed_connect_frees_handlers() {
  natscpp::connection_options opts;
  opts.set_url("nats://127.0.0.1:1");  // nothing listens on port 1
  opts.set_allow_reconnect(false);
  opts.set_closed_cb([](natsConnection*) { assert(false && "closed_cb must not run for a failed connect"); });
  assert(status_of([&] { natscpp::connection nc(opts); }) == NATS_NO_SERVER);
}

// ---------------------------------------------------------------------------
// Server-backed: correctness and coverage
// ---------------------------------------------------------------------------

void test_connection_state_and_header_support() {
  auto nc = connect_or_skip("connection state");
  if (!nc) return;
  assert(nc->connected());
  assert(nc->has_header_support());  // was inverted: nats.c returns NATS_OK (0) when supported
  assert(!nc->is_reconnecting() && !nc->is_draining());
  nc->close();
  assert(!nc->connected());  // a closed connection is not connected, though it still has a handle
  assert(nc->native_handle() != nullptr);
}

void test_rarely_used_connection_options_are_accepted() {
  natscpp::connection_options opts;
  opts.set_token("ignored-by-a-server-without-auth");
  opts.set_lame_duck_mode_cb([](natsConnection*) {});
  opts.set_discovered_servers_cb([](natsConnection*) {});
  auto nc = connect_or_skip("rarely used connection options", opts);
  if (!nc) return;
  assert(nc->connected());

  // TLS options on a plain connection: accepted with secure = false when nats.c has TLS; a nats.c built
  // without TLS rejects them before connecting.
  natscpp::connection_options tls_opts;
  tls_opts.set_secure(false);  // opt out of the TLS requirement the next two would add
  tls_opts.set_expected_hostname("localhost");
  tls_opts.set_ciphers("HIGH:!aNULL");
#ifdef NATS_HAS_TLS
  natscpp::connection tls_nc(tls_opts);
  assert(tls_nc.connected());
#else
  assert(status_of([&] { natscpp::connection tls_nc(tls_opts); }) == NATS_ILLEGAL_STATE);
#endif
}

// Exceptions from connection callbacks are caught at the C boundary: the connection keeps working and the
// remaining callbacks, including the closed callback that frees the handlers, still run.
void test_throwing_connection_callbacks_are_contained() {
  using namespace std::chrono_literals;
  auto reconnected = std::make_shared<std::promise<void>>();
  auto slow = std::make_shared<std::promise<void>>();
  auto closed = std::make_shared<std::promise<void>>();
  auto reconnected_f = reconnected->get_future();
  auto slow_f = slow->get_future();
  auto closed_f = closed->get_future();

  natscpp::connection_options opts;
  opts.set_reconnect_wait(50ms);
  opts.set_reconnect_jitter(0, 0);
  opts.set_disconnected_cb([](natsConnection*) { throw std::runtime_error("disconnected"); });
  opts.set_reconnected_cb([reconnected, once = std::make_shared<std::once_flag>()](natsConnection*) {
    std::call_once(*once, [&] { reconnected->set_value(); });
    throw std::runtime_error("reconnected");
  });
  opts.set_error_handler(
      [slow, once = std::make_shared<std::once_flag>()](natsConnection*, natsSubscription*, natsStatus) {
        std::call_once(*once, [&] { slow->set_value(); });
        throw std::runtime_error("error handler");
      });
  opts.set_closed_cb([closed](natsConnection*) {
    closed->set_value();
    throw std::runtime_error("closed");
  });
  auto nc = connect_or_skip("throwing connection callbacks", opts);
  if (!nc) return;

  nc->reconnect();  // disconnected + reconnected callbacks, both throwing
  assert(reconnected_f.wait_for(5s) == std::future_status::ready);

  // A slow consumer makes nats.c call the error handler.
  const std::string subj = unique("natscpp.throwcb.");
  std::promise<void> release;
  auto release_f = release.get_future().share();
  auto sub = nc->subscribe_async(subj, [release_f](natscpp::message) { release_f.wait_for(5s); });
  sub.set_pending_limits(1, 1024);
  for (int i = 0; i < 50; ++i) nc->publish(subj, "x");
  nc->flush(2s);
  assert(slow_f.wait_for(5s) == std::future_status::ready);
  release.set_value();
  sub.unsubscribe();

  // Still usable after every callback threw.
  auto check = nc->subscribe_sync(subj + ".ok");
  nc->publish(subj + ".ok", "alive");
  assert(check.next_message(2s).data() == "alive");

  nc->close();
  assert(closed_f.wait_for(5s) == std::future_status::ready);
}

// A request to a subject nobody listens on comes back as a no-responders status message. A sync subscription
// reports it as NATS_NO_RESPONDERS from next_message(); an async handler receives the message itself.
void test_no_responders_message() {
  auto nc = connect_or_skip("no-responders status message");
  if (!nc) return;
  const auto nobody = unique("natscpp.regress.nobody.");

  const auto sync_inbox = nc->new_inbox();
  auto sync_replies = nc->subscribe_sync(sync_inbox);
  nc->publish_request(nobody, sync_inbox, "anyone?");
  assert(status_of([&] { (void)sync_replies.next_message(2s); }) == NATS_NO_RESPONDERS);

  std::promise<bool> seen;
  const auto async_inbox = nc->new_inbox();
  auto async_replies =
      nc->subscribe_async(async_inbox, [&seen](natscpp::message m) { seen.set_value(m.is_no_responders()); });
  nc->publish_request(nobody, async_inbox, "anyone?");
  auto fut = seen.get_future();
  assert(fut.wait_for(2s) == std::future_status::ready && fut.get());
  std::promise<void> completed;
  async_replies.set_on_complete_callback([&completed] { completed.set_value(); });
  async_replies.unsubscribe();
  completed.get_future().wait();  // the handler uses locals of this function
}

void test_async_timeout_subscription_ignores_timeouts() {
  auto nc = connect_or_skip("async subscription timeout");
  if (!nc) return;
  std::atomic<int> handled{0};
  const auto subject = unique("natscpp.regress.timeout.");
  auto sub = nc->subscribe_async_timeout(subject, 5ms, [&handled](natscpp::message) { ++handled; });
  std::this_thread::sleep_for(50ms);  // several timeouts: nats.c delivers a null message for each
  assert(handled == 0);
  nc->publish(subject, "real");
  nc->flush(1s);
  assert(eventually([&] { return handled.load() == 1; }));
  sub.drain();
  sub.wait_for_drain_completion(1s);
}

void test_jetstream_validation_and_options() {
  auto nc = connect_or_skip("jetstream validation and options");
  if (!nc) return;
  natscpp::jetstream js(*nc);
  const auto stream = unique("REGRESS_JS_");
  const auto subject = "regress.js." + stream;

  assert(throws_invalid_argument([&] { (void)js.create_stream({}); }));
  assert(throws_invalid_argument([&] { (void)js.update_stream({}); }));
  assert(throws_invalid_argument([&] { js.delete_stream(""); }));
  assert(throws_invalid_argument([&] { js.purge_stream(""); }));
  assert(throws_invalid_argument([&] { (void)js.get_stream_info(""); }));
  assert(throws_invalid_argument([&] { (void)js.get_consumer_group("", "d"); }));
  assert(throws_invalid_argument([&] { (void)js.get_consumer_group("S", ""); }));
  assert(throws_invalid_argument([&] { js.delete_consumer_group("", "d"); }));
  assert(throws_invalid_argument([&] { js.delete_consumer_group("S", ""); }));
  assert(throws_invalid_argument([&] { (void)js.list_consumer_groups(""); }));
  assert(throws_invalid_argument([&] { (void)js.list_consumer_group_names(""); }));
  assert(throws_invalid_argument([&] {
    (void)js.create_consumer_group(
        {.stream = stream, .durable_name = "d", .filter_subject = subject, .filter_subjects = {subject}});
  }));

  auto created = js.create_stream({.name = stream, .subjects = {subject + ".>"}, .description = "regression"});
  assert(created.description == "regression");

  // Every optional consumer field, on a push consumer with a queue group.
  auto ci = js.create_consumer_group({
      .stream = stream,
      .durable_name = "all-fields",
      .deliver_subject = "deliver." + stream,
      .deliver_group = "grp",
      .type = natscpp::js_consumer_type::push,
      .deliver_policy = natscpp::js_deliver_policy::by_start_seq,
      .ack_wait_ns = 5'000'000'000,
      .max_deliver = 3,
      .max_ack_pending = 10,
      .replicas = 1,
      .opt_start_seq = 1,
      .filter_subjects = {subject + ".a", subject + ".b"},
      .description = "every field",
  });
  assert(ci.durable_name == "all-fields");
  (void)js.create_consumer_group(
      {.stream = stream, .durable_name = "pull-fields", .max_waiting = 16, .inactive_threshold_ns = 60'000'000'000});

  // Publish options: each expectation set to the value that holds, so the publish succeeds.
  auto first = js.publish(subject + ".a", "one", {.msg_id = "m1"});
  natscpp::js_publish_options expect{.msg_id = "m2",
                                     .expected_stream = stream,
                                     .expected_last_msg_id = "m1",
                                     .expected_last_seq = first.sequence,
                                     .max_wait_ms = 2000};
  auto second = js.publish(subject + ".a", "two", expect);
  assert(second.sequence == first.sequence + 1);
  auto third = js.publish_msg(natscpp::message::create(subject + ".a", "", "three"),
                              {.expected_last_subject_seq = second.sequence});
  assert(third.sequence == second.sequence + 1);
  auto fourth = js.publish(subject + ".new", "four", {.expect_no_message = true});
  assert(fourth.sequence == third.sequence + 1);
  assert(js.publish(subject + ".a", "dup", {.msg_id = "m1"}).duplicate);
  assert(status_of([&] { (void)js.publish(subject + ".a", "x", {.expected_last_seq = 1}); }) != NATS_OK);

  auto stored = js.get_msg(stream, first.sequence);
  assert(stored.timestamp() > 0);

  js.delete_stream(stream);
}

void test_jetstream_pull_fetch_and_async_subscribe() {
  auto nc = connect_or_skip("jetstream fetch and async subscribe");
  if (!nc) return;
  natscpp::jetstream js(*nc);
  const auto stream = unique("REGRESS_FETCH_");
  const auto subject = "regress.fetch." + stream;
  (void)js.create_stream({.name = stream, .subjects = {subject + ".>"}});
  (void)js.create_consumer_group({.stream = stream, .durable_name = "puller", .filter_subject = subject + ".pull"});

  auto pull = js.pull_subscribe(subject + ".pull", "puller");
  assert(pull.native_handle() != nullptr);
  assert(throws_invalid_argument([&] { (void)pull.fetch(0); }));
  assert(status_of([&] { (void)pull.fetch(5, 50ms); }) == NATS_TIMEOUT);  // nothing published yet
  for (int i = 0; i < 3; ++i) (void)js.publish(subject + ".pull", "m" + std::to_string(i));
  std::size_t fetched = 0;
  while (fetched < 3) {
    auto batch = pull.fetch(10, 2s);
    for (auto& m : batch) m.ack();
    fetched += batch.size();
  }
  assert(fetched == 3);

  // Ephemeral push consumer (no durable name).
  auto ephemeral = js.push_subscribe(subject + ".eph", "");
  assert(ephemeral.native_handle() != nullptr);
  (void)js.publish(subject + ".eph", "e");
  assert(ephemeral.next(2s).data() == "e");

  // Callback subscription with automatic acks, plus its consumer info.
  std::atomic<int> handled{0};
  auto sub = js.subscribe_async(subject + ".cb", "", [&handled](natscpp::message) { ++handled; });
  (void)js.publish(subject + ".cb", "c1");
  (void)js.publish(subject + ".cb", "c2");
  assert(eventually([&] { return handled.load() == 2; }));
  auto info = sub.get_consumer_info();
  assert(info.stream == stream && !info.name.empty());
  assert(!sub.get_sequence_mismatch().has_value());

  std::promise<void> completed;
  sub.set_on_complete_callback([&completed] { completed.set_value(); });
  sub.unsubscribe();
  assert(completed.get_future().wait_for(2s) == std::future_status::ready);
  js.delete_stream(stream);
}

void test_kv_failure_paths_and_options() {
  auto nc = connect_or_skip("kv failure paths");
  if (!nc) return;
  assert(status_of([&] { (void)natscpp::key_value::create(*nc, "bad bucket name!"); }) != NATS_OK);
  assert(status_of([&] { natscpp::key_value missing(*nc, unique("missing_")); }) != NATS_OK);
  assert(status_of([&] { natscpp::key_value::delete_bucket(*nc, unique("missing_")); }) != NATS_OK);
  assert(status_of([&] { (void)natscpp::key_value::create(*nc, {.bucket = "b", .history = 300}); }) ==
         NATS_INVALID_ARG);

  const auto bucket = unique("regress_kv_");
  auto kv = natscpp::key_value::create(*nc, {.bucket = bucket, .history = 3});
  (void)kv.put("a.1", "x");
  (void)kv.put("b.1", "y");
  kv.erase("a.1");

  auto history = kv.history("a.1");
  assert(history.size() == 2);  // the put and the delete marker
  const auto& tombstone = history.back();
  assert(tombstone.operation() == kvOp_Delete);
  assert(tombstone.value().empty());

  const natscpp::kv_watch_options opts{.ignore_deletes = true};
  auto keys = kv.keys_with_filters({"a.*", "b.*"}, &opts);
  assert(keys.size() == 1 && keys.front() == "b.1");
  auto watcher = kv.watch_multi({"a.*", "b.*"}, &opts);
  auto first = watcher.next(2000);
  assert(first.valid() && first.key() == "b.1");
  watcher.stop();

  kv.purge_deletes(1000);
  natscpp::key_value::delete_bucket(*nc, bucket);
}

}  // namespace

namespace unit_tests {

void run_offline_regression_tests() {
  test_credentials_setter_leaves_seed_unset_by_default();
  test_error_text_omits_unrelated_last_error();
  test_future_awaitable_is_move_only();
  test_compatibility_check_keeps_nats_macro();
  test_invalid_objects_reject_operations();
  test_self_move_assignment_is_a_no_op();
  test_header_errors();
}

void run_server_regression_tests() {
  test_subscription_destroyed_while_handler_runs();
  test_handler_unsubscribes_itself();
  test_on_complete_runs_after_subscription_is_gone();
  test_on_complete_rules();
  test_closed_callback_after_connection_destroyed();
  test_failed_connect_frees_handlers();
  test_connection_state_and_header_support();
  test_rarely_used_connection_options_are_accepted();
  test_throwing_connection_callbacks_are_contained();
  test_no_responders_message();
  test_async_timeout_subscription_ignores_timeouts();
  test_jetstream_validation_and_options();
  test_jetstream_pull_fetch_and_async_subscribe();
  test_kv_failure_paths_and_options();
}

}  // namespace unit_tests
