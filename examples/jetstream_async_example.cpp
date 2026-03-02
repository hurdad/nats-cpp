#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.async.events";
  constexpr auto pull_durable = "demo-js-async-pull-worker";
  constexpr auto push_durable = "demo-js-async-push-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_ASYNC", .subjects = {"demo.js.async.>"}});
  [[maybe_unused]] auto push_consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_ASYNC",
      .durable_name = push_durable,
      .filter_subject = subject,
      .deliver_subject = "demo.js.async.delivery",
      .type = natscpp::js_consumer_type::push,
  });

  auto push_consumer = js.push_subscribe(subject, push_durable);
  (void) push_consumer;

  [[maybe_unused]] auto pull_consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_ASYNC",
      .durable_name = pull_durable,
      .filter_subject = subject,
      .type = natscpp::js_consumer_type::pull,
  });

  auto consumer = js.pull_subscribe(subject, pull_durable);
  auto next_msg = std::async(std::launch::async, [&consumer] {
    return consumer.next(std::chrono::seconds(2));
  });

  js.publish(subject, "hello-jetstream-async");

  auto msg = next_msg.get();
  std::cout << "jetstream async received: " << msg.data() << '\n';
  return 0;
}
