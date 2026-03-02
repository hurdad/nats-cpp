#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.sync.events";
  constexpr auto durable = "demo-js-sync-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_SYNC", .subjects = {"demo.js.sync.>"}});
  [[maybe_unused]] auto consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_SYNC",
      .durable_name = durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::pull,
  });

  auto consumer = js.pull_subscribe(subject, durable);
  js.publish(subject, "hello-jetstream-sync");

  auto msg = consumer.next(std::chrono::seconds(2));
  std::cout << "jetstream sync received: " << msg.data() << '\n';
  return 0;
}
