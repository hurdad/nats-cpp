#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.sync.pull.events";
  constexpr auto durable = "demo-js-sync-pull-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_SYNC_PULL", .subjects = {"demo.js.sync.pull.>"}});
  [[maybe_unused]] auto consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_SYNC_PULL",
      .durable_name = durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::pull,
  });

  auto consumer = js.pull_subscribe(subject, durable);
  js.publish(subject, "hello-jetstream-sync-pull");

  auto msg = consumer.next(std::chrono::seconds(2));
  std::cout << "jetstream sync pull received: " << msg.data() << '\n';
  return 0;
}
