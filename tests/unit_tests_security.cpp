// The checks below are assert()s; keep them active in Release builds.
#undef NDEBUG

#include "unit_tests.hpp"

#include <natscpp/connection.hpp>
#include <natscpp/header.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/kv.hpp>
#include <natscpp/library.hpp>
#include <natscpp/message.hpp>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Input that would reach the NATS protocol unchecked: nats.c does not validate subjects or header keys when
// publishing, so a subject or reply subject with whitespace or CR/LF, or a header key with CR/LF, used to inject
// protocol lines (publishing elsewhere with this connection's permissions) or headers (e.g. a forged
// Nats-Msg-Id). And a std::string_view with an embedded NUL used to be cut short by nats.c's C strings.

namespace {

using namespace std::chrono_literals;
using namespace std::string_literals;

template <typename Fn>
natsStatus status_of(Fn&& fn) {
  try {
    fn();
  } catch (const natscpp::nats_error& e) {
    return e.status();
  }
  return NATS_OK;
}

// A protocol-injection payload: if the subject went out unchecked, the server would see a second PUB to
// "sec.injected".
constexpr std::string_view injection_text = "sec.ok 0\r\n\r\nPUB sec.injected 4\r\npwnd\r\nPUB sec.tail";
const std::string injection{injection_text};  // NOLINT(cert-err58-cpp): test-only global

std::string unique(std::string_view prefix) {
  return std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

void test_subject_validation_rules() {
  using natscpp::detail::subject;
  for (const char* ok : {"a", "a.b", "orders.created", "a-b_c/d=e", "a*b.c>d", "$JS.API.INFO", "_INBOX.x"}) {
    assert(subject(ok, "test") == ok);
  }
  for (const char* ok : {"a.*", "a.>", "*", ">", "a.*.c", "*.b.>"}) assert(subject(ok, "test", true) == ok);

  const std::vector<std::string> bad = {
      "", ".", "a.", ".a", "a..b", "a b", "a\tb", "a\rb", "a\nb", "a\x7f" + "b"s, "a\x01" + "b"s, "a\0b"s, injection,
  };
  for (const auto& s : bad) {
    assert(status_of([&] { (void)subject(s, "test", true); }) == NATS_INVALID_SUBJECT);
  }
  for (const char* wc : {"a.*", "a.>", "*", ">"}) {  // no wildcards where they are not allowed (publishing)
    assert(status_of([&] { (void)subject(wc, "test"); }) == NATS_INVALID_SUBJECT);
  }
  assert(status_of([&] { (void)subject("a.>.b", "test", true); }) == NATS_INVALID_SUBJECT);  // '>' must be last
}

void test_header_and_c_string_validation_rules() {
  using natscpp::detail::c_string;
  using natscpp::detail::header_key;
  using natscpp::detail::header_value;
  for (const char* ok : {"X-Trace", "Nats-Msg-Id", "traceparent", "a!#$%&'*+-.^_`|~0"}) {
    assert(header_key(ok, "test") == ok);
  }
  for (const auto& bad : {""s, "a b"s, "a:b"s, "a\r\nb"s, "a\nb"s, "a\0b"s, "\xc3\xa9"s}) {
    assert(status_of([&] { (void)header_key(bad, "test"); }) == NATS_INVALID_ARG);
  }
  assert(header_value("", "test").empty());
  const std::string fine = "any value: with spaces, colons\tand tabs";
  assert(header_value(fine, "test") == fine);
  for (const auto& bad : {"a\r\nb"s, "a\rb"s, "a\nb"s, "a\0b"s}) {
    assert(status_of([&] { (void)header_value(bad, "test"); }) == NATS_INVALID_ARG);
  }
  assert(c_string("plain", "test") == "plain");
  assert(status_of([] { (void)c_string("stream\0x"s, "test"); }) == NATS_INVALID_ARG);

  // The error message shows the offending text with control characters escaped.
  try {
    (void)header_key("a\r\nb", "set_header");
    assert(false);
  } catch (const natscpp::nats_error& e) {
    assert(std::string_view(e.what()).find("a\\x0d\\x0ab") != std::string_view::npos);
  }
}

void test_messages_and_headers_reject_injection() {
  assert(status_of([] { (void)natscpp::message::create(injection, "", "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([] { (void)natscpp::message::create("ok", injection, "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([] { (void)natscpp::message::create("a.*", "", "x"); }) == NATS_INVALID_SUBJECT);

  auto msg = natscpp::message::create("sec.ok", "", "x");
  assert(status_of([&] { msg.set_header("X\r\nNats-Msg-Id", "forged"); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.add_header("X", "v\r\nNats-Msg-Id: forged"); }) == NATS_INVALID_ARG);
  assert(status_of([&] { msg.delete_header("X\0Y"s); }) == NATS_INVALID_ARG);
  msg.set_header("X-Ok", "fine");
  assert(msg.header("X-Ok") == "fine");

  auto h = natscpp::header::create();
  assert(status_of([&] { h.set("X\r\nY", "v"); }) == NATS_INVALID_ARG);
  assert(status_of([&] { h.add("X", "a\nb"); }) == NATS_INVALID_ARG);
  h.set("X-Ok", "fine");
  assert(h.get("X-Ok") == "fine");
}

// nats_Sign used to sign only the part of the input before a NUL byte.
void test_sign_rejects_embedded_nul() {
  unsigned char* sig = nullptr;
  int len = 0;
  assert(status_of([&] {
           (void)natscpp::nats_Sign("SUAMLK2ZNL35WSMW37E7UD4VZ7ELPKW7DHC3BWBSD2GCZ7IUQQXZIORRBU", "signed\0tail"s, &sig,
                                    &len);
         }) == NATS_INVALID_ARG);
  assert(sig == nullptr);
}

// The reply subject of a message built from a raw natsMsg is checked too, and valid input still signs.
void test_raw_message_reply_and_sign_ok() {
  natsMsg* raw = nullptr;
  assert(::nats_Open(-1) == NATS_OK);
  assert(natsMsg_Create(&raw, "sec.ok", injection.c_str(), "x", 1) == NATS_OK);
  const natscpp::message msg{raw};
  assert(status_of([&] { natscpp::detail::check_outgoing(msg, "test"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([] { natscpp::detail::check_outgoing(natscpp::message{}, "test"); }) == NATS_INVALID_ARG);

  unsigned char* sig = nullptr;
  int len = 0;
  // Not a valid seed: nats.c reports an error, but the input reached it.
  assert(natscpp::nats_Sign("not-a-seed", "nonce", &sig, &len) != NATS_OK);
  std::free(sig);
}

std::optional<natscpp::connection> connect_or_skip(std::string_view what) {
  try {
    return std::optional<natscpp::connection>{std::in_place, natscpp::connection_options{}};
  } catch (const natscpp::nats_error&) {
    unit_tests::skip(std::string(what) + " (NATS server unavailable)");
    return std::nullopt;
  }
}

// Every publish path rejects the injection, and nothing reaches a spy on the injected subject.
void test_publish_paths_reject_protocol_injection() {
  auto nc = connect_or_skip("publish injection");
  if (!nc) return;
  auto observer = natscpp::connection::connect_to("nats://127.0.0.1:4222");
  auto spy = observer.subscribe_sync("sec.injected");
  observer.flush(2s);

  assert(status_of([&] { nc->publish(injection, "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { nc->publish("sec.ok extra", "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { nc->publish_string(injection, "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { nc->publish_string("sec.ok", "pay\0load"s); }) == NATS_INVALID_ARG);
  assert(status_of([&] { nc->publish_request("sec.ok", injection, "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { nc->publish_request_string(injection, "r", "x"); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { (void)nc->request_sync(injection, "x", 200ms); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { (void)nc->request_string(injection, "x", 200ms); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { (void)nc->request_async(injection, "x", 200ms).get(); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { (void)nc->subscribe_sync("sec\0.x"s); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { (void)nc->subscribe_queue_sync("sec.x", "q\0x"s); }) == NATS_INVALID_ARG);

  // A message built from a raw natsMsg skips message::create, but publish() and request() check it too.
  natsMsg* raw = nullptr;
  assert(natsMsg_Create(&raw, injection.c_str(), nullptr, "x", 1) == NATS_OK);
  assert(status_of([&] { nc->publish(natscpp::message{raw}); }) == NATS_INVALID_SUBJECT);
  assert(natsMsg_Create(&raw, injection.c_str(), nullptr, "x", 1) == NATS_OK);
  assert(status_of([&] { (void)nc->request(natscpp::message{raw}, 200ms); }) == NATS_INVALID_SUBJECT);

  nc->flush(2s);
  assert(status_of([&] { (void)spy.next_message(300ms); }) == NATS_TIMEOUT);  // nothing was injected
}

// The header-key injection reached JetStream: a forged Nats-Msg-Id made the server drop the next publish as a
// duplicate. Now the key is rejected and both publishes are stored.
void test_jetstream_rejects_injection() {
  auto nc = connect_or_skip("JetStream injection");
  if (!nc) return;
  natscpp::jetstream js(*nc);
  const std::string stream = unique("SECJS");
  const std::string subj = "secjs." + stream;
  (void)js.create_stream({.name = stream, .subjects = {subj}});

  assert(status_of([&] { (void)js.publish(injection, "x"); }) == NATS_INVALID_SUBJECT);
  natsMsg* raw = nullptr;
  assert(natsMsg_Create(&raw, injection.c_str(), nullptr, "x", 1) == NATS_OK);
  assert(status_of([&] { (void)js.publish_msg(natscpp::message{raw}); }) == NATS_INVALID_SUBJECT);
  assert(status_of([&] { (void)js.get_stream_info(stream + "\0x"s); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.subscribe_async(subj, "dur\0x"s, [](natscpp::message) {}); }) == NATS_INVALID_ARG);

  for (uint64_t i = 0; i < 2; ++i) {
    auto m = natscpp::message::create(subj, "", "body");
    assert(status_of([&] { m.set_header("X-Ok\r\nNats-Msg-Id", "forged"); }) == NATS_INVALID_ARG);
    const auto ack = js.publish_msg(std::move(m));
    assert(!ack.duplicate);
    assert(ack.sequence == i + 1);
  }
  js.delete_stream(stream);
}

void test_kv_rejects_embedded_nul() {
  auto nc = connect_or_skip("KV embedded NUL");
  if (!nc) return;
  const std::string bucket = unique("SECKV");
  auto kv = natscpp::key_value::create(*nc, bucket);
  (void)kv.put("secret", "v");
  assert(status_of([&] { (void)kv.get("secret\0x"s); }) == NATS_INVALID_ARG);  // used to read "secret"
  assert(status_of([&] { (void)kv.put("secret\0x"s, "overwrite"); }) == NATS_INVALID_ARG);
  assert(kv.get("secret").value() == "v");
  natscpp::key_value::delete_bucket(*nc, bucket);
}

// last_error() used to throw whenever the connection had an error, so it could never report one.
void test_last_error_reports_instead_of_throwing() {
  auto nc = connect_or_skip("last_error");
  if (!nc) return;
  assert(nc->last_error().empty());

  // A slow consumer sets the connection's error: a handler that blocks, with a pending limit of one message.
  const std::string subj = unique("sec.slow.");
  std::promise<void> release;
  auto release_f = release.get_future().share();
  auto sub = nc->subscribe_async(subj, [release_f](natscpp::message) { release_f.wait_for(5s); });
  sub.set_pending_limits(1, 1024);
  for (int i = 0; i < 50; ++i) nc->publish(subj, "x");
  nc->flush(2s);
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (nc->last_error().empty() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(10ms);
  const std::string err = nc->last_error();
  release.set_value();
  sub.unsubscribe();
  assert(err.find("Slow Consumer") != std::string::npos);

  natscpp::connection unset;
  assert(status_of([&] { (void)unset.last_error(); }) == NATS_INVALID_ARG);
  assert(status_of([&] { unset.process_read_event(); }) == NATS_INVALID_ARG);  // used to crash in nats.c
  assert(status_of([&] { unset.process_write_event(); }) == NATS_INVALID_ARG);
}

void test_name_and_json_text_rules() {
  using natscpp::detail::config_subject;
  using natscpp::detail::json_text;
  using natscpp::detail::kv_key_filter;
  using natscpp::detail::name;
  for (const char* ok : {"ORDERS", "orders-v2", "a_b", "x=1"}) assert(name(ok, "test") == ok);
  assert(name("", "test", true).empty());
  for (const auto& bad : {""s, "a.b"s, "a b"s, "a\tb"s, "a\r\nb"s, "a*"s, "a>"s, "a/b"s, "a\\b"s, "a\"b"s, "a\0b"s}) {
    assert(status_of([&] { (void)name(bad, "test"); }) == NATS_INVALID_ARG);
  }
  assert(config_subject("orders.*.created", "test") == "orders.*.created");
  for (const auto& bad : {"a\"b"s, "a\\b"s, "a b"s, ""s}) {
    assert(status_of([&] { (void)config_subject(bad, "test"); }) == NATS_INVALID_SUBJECT);
  }
  assert(kv_key_filter("users.*", "test") == "users.*");
  for (const auto& bad : {""s, "a\"b"s, "a b"s, "a\nb"s}) {
    assert(status_of([&] { (void)kv_key_filter(bad, "test"); }) == NATS_INVALID_ARG);
  }
  assert(json_text("plain", "test") == "plain");
  assert(json_text("a\"b\\c", "test") == "a\\\"b\\\\c");
  assert(json_text("a\r\nb\x7f", "test") == "a\\u000d\\u000ab\\u007f");
  assert(status_of([] { (void)json_text("a\0b"s, "test"); }) == NATS_INVALID_ARG);
}

// nats.c checks stream names only for '.' (consumer names also for ' ', '*', '>') before putting them into the
// JetStream API subject, so a tab and CR/LF used to inject protocol lines through every stream/consumer call.
void test_jetstream_names_reject_protocol_injection() {
  auto nc = connect_or_skip("JetStream name injection");
  if (!nc) return;
  auto observer = natscpp::connection::connect_to("nats://127.0.0.1:4222");
  auto spy = observer.subscribe_sync("secinjected");
  observer.flush(2s);
  natscpp::jetstream js(*nc);
  const std::string inj = "x\t0\r\n\r\nPUB\tsecinjected\t4\r\npwnd\r\nPUB\tz";

  assert(status_of([&] { (void)js.get_stream_info(inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { js.purge_stream(inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { js.delete_stream(inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.get_msg(inj, 1); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.get_last_msg(inj, "a"); }) == NATS_INVALID_ARG);
  assert(status_of([&] { js.delete_msg(inj, 1); }) == NATS_INVALID_ARG);
  assert(status_of([&] { js.erase_msg(inj, 1); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.list_consumer_groups(inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.list_consumer_group_names(inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.get_consumer_group("S", inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { js.delete_consumer_group("S", inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.create_stream({.name = inj}); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.create_consumer_group({.stream = inj, .durable_name = "d"}); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.create_consumer_group({.stream = "S", .durable_name = inj}); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.pull_subscribe("a", inj); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)js.push_subscribe("a", inj); }) == NATS_INVALID_ARG);

  nc->flush(2s);
  assert(nc->connected());  // a malformed request used to make the server drop the connection
  assert(status_of([&] { (void)spy.next_message(300ms); }) == NATS_TIMEOUT);
}

// nats.c writes config strings into the JSON request unescaped: a description could add fields (deny_delete), a
// subject could add subjects, a filter subject could set max_deliver. Descriptions are now escaped and arrive
// intact; subjects with '"' or '\' are rejected.
void test_jetstream_config_json_is_not_injectable() {
  auto nc = connect_or_skip("JetStream config JSON");
  if (!nc) return;
  natscpp::jetstream js(*nc);
  const std::string stream = unique("SECJSON");
  const std::string description = "d\",\"deny_delete\":true,\"x\":\"\\ \r\n";
  (void)js.create_stream({.name = stream, .subjects = {"secjson." + stream}, .description = description});

  jsCtx* ctx = nullptr;
  assert(natsConnection_JetStream(&ctx, nc->native_handle(), nullptr) == NATS_OK);
  jsStreamInfo* si = nullptr;
  assert(js_GetStreamInfo(&si, ctx, stream.c_str(), nullptr, nullptr) == NATS_OK);
  assert(!si->Config->DenyDelete);                                   // was set by the description
  assert(std::string_view(si->Config->Description) == description);  // and the text arrives intact
  jsStreamInfo_Destroy(si);

  assert(status_of([&] { (void)js.create_stream({.name = stream + "B", .subjects = {"a\",\"b.>"}}); }) ==
         NATS_INVALID_SUBJECT);
  assert(status_of([&] {
           (void)js.create_consumer_group(
               {.stream = stream, .durable_name = "C", .filter_subject = "secjson.x\",\"max_deliver\":2,\"y\":\""});
         }) == NATS_INVALID_SUBJECT);
  const std::string consumer_description = R"(c","max_deliver":2,"y":")";
  (void)js.create_consumer_group({.stream = stream, .durable_name = "C", .description = consumer_description});
  jsConsumerInfo* ci = nullptr;
  assert(js_GetConsumerInfo(&ci, ctx, stream.c_str(), "C", nullptr, nullptr) == NATS_OK);
  assert(ci->Config->MaxDeliver != 2);
  assert(std::string_view(ci->Config->Description) == consumer_description);
  jsConsumerInfo_Destroy(ci);
  jsCtx_Destroy(ctx);
  js.delete_stream(stream);

  const std::string bucket = unique("SECJSONKV");
  natscpp::kv_config cfg;
  cfg.bucket = bucket;
  cfg.description = R"(k","deny_delete":true,"x":")";
  auto kv = natscpp::key_value::create(*nc, cfg);
  assert(status_of([&] { (void)kv.watch("a\",\"b"); }) == NATS_INVALID_ARG);
  assert(status_of([&] { (void)kv.history("a b"); }) == NATS_INVALID_ARG);
  assert(js.get_stream_info("KV_" + bucket).description == cfg.description);
  natscpp::key_value::delete_bucket(*nc, bucket);
}

// nats.c copies user, password, token and name into the CONNECT JSON unescaped: a name with CR/LF injected
// protocol lines, a name could add credentials, and a password containing '"' or '\' could never authenticate.
void test_connect_fields_are_escaped() {
  const char* auth_url = std::getenv("NATSCPP_TEST_AUTH_URL");
  if (auth_url == nullptr || *auth_url == '\0') {
    unit_tests::skip("CONNECT escaping (NATSCPP_TEST_AUTH_URL not set)");
    return;
  }
  natscpp::connection_options quote;
  quote.url = auth_url;
  quote.user = "quote";
  quote.password = "pa\"ss\\word";  // as configured in tests/ci/start-servers.sh
  quote.name = "name with \"quotes\", \\ and \r\nPUB secinjected 4\r\npwnd";
  natscpp::connection nc(quote);
  assert(nc.connected());

  natscpp::connection_options smuggled;  // no credentials of its own; the name used to add alice's
  smuggled.url = auth_url;
  smuggled.name = R"(n","user":"alice","pass":"secret)";
  assert(status_of([&] { natscpp::connection c(smuggled); }) != NATS_OK);

  natscpp::connection_options nul;
  nul.url = auth_url;
  nul.password = "a\0b"s;
  nul.user = "alice";
  assert(status_of([&] { natscpp::connection c(nul); }) == NATS_INVALID_ARG);
}

}  // namespace

namespace unit_tests {

std::vector<test_case> security_unit_tests() {
  return {
      NATSCPP_TEST_CASE("unit", test_subject_validation_rules),
      NATSCPP_TEST_CASE("unit", test_header_and_c_string_validation_rules),
      NATSCPP_TEST_CASE("unit", test_messages_and_headers_reject_injection),
      NATSCPP_TEST_CASE("unit", test_sign_rejects_embedded_nul),
      NATSCPP_TEST_CASE("unit", test_raw_message_reply_and_sign_ok),
      NATSCPP_TEST_CASE("unit", test_name_and_json_text_rules),
  };
}

std::vector<test_case> security_server_tests() {
  return {
      NATSCPP_TEST_CASE("integration", test_publish_paths_reject_protocol_injection),
      NATSCPP_TEST_CASE("integration", test_jetstream_rejects_injection),
      NATSCPP_TEST_CASE("integration", test_kv_rejects_embedded_nul),
      NATSCPP_TEST_CASE("integration", test_last_error_reports_instead_of_throwing),
      NATSCPP_TEST_CASE("integration", test_jetstream_names_reject_protocol_injection),
      NATSCPP_TEST_CASE("integration", test_jetstream_config_json_is_not_injectable),
      NATSCPP_TEST_CASE("tls", test_connect_fields_are_escaped),
  };
}

}  // namespace unit_tests
