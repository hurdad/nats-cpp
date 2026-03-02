#include <chrono>
#include <thread>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.sync.push.events";
  constexpr auto durable = "demo-js-sync-push-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_SYNC_PUSH", .subjects = {"demo.js.sync.push.>"}});
  [[maybe_unused]] auto consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_SYNC_PUSH",
      .durable_name = durable,
      .filter_subject = subject,
      .deliver_subject = "demo.js.sync.push.delivery",
      .type = natscpp::js_consumer_type::push,
  });

  auto consumer = js.push_subscribe(subject, durable);
  (void) consumer;

  js.publish(subject, "hello-jetstream-sync-push");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  return 0;
}
