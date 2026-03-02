#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.async.pull.events";
  constexpr auto durable = "demo-js-async-pull-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_ASYNC_PULL", .subjects = {"demo.js.async.pull.>"}});
  [[maybe_unused]] auto consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_ASYNC_PULL",
      .durable_name = durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::pull,
  });

  auto consumer = js.pull_subscribe(subject, durable);
  auto next_msg = std::async(std::launch::async, [&consumer] {
    return consumer.next(std::chrono::seconds(2));
  });

  js.publish(subject, "hello-jetstream-async-pull");

  auto msg = next_msg.get();
  std::cout << "jetstream async pull received: " << msg.data() << '\n';
  return 0;
}
