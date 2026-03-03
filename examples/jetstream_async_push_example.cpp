#include <future>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

// Delivery pattern: WORK QUEUE
//   A single durable consumer is shared across all subscribers. NATS delivers each message to
//   exactly one subscriber (competing consumers). This is the pattern shown below.
//
// To implement PUB/SUB fan-out instead:
//   Create a separate consumer group per subscriber, each with a unique durable name and
//   deliver_subject. Every subscriber then independently receives every message.
//
//     js.create_consumer_group({.stream = "STREAM", .durable_name = "worker-a",
//                                .filter_subject = subject,
//                                .deliver_subject = "delivery.a", .type = push});
//     js.create_consumer_group({.stream = "STREAM", .durable_name = "worker-b",
//                                .filter_subject = subject,
//                                .deliver_subject = "delivery.b", .type = push});
//     auto consumer_a = js.push_subscribe(subject, "worker-a");
//     auto consumer_b = js.push_subscribe(subject, "worker-b");
//     // consumer_a and consumer_b each receive every published message

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.async.push.events";
  constexpr auto durable = "demo-js-async-push-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_ASYNC_PUSH", .subjects = {"demo.js.async.push.>"}});
  [[maybe_unused]] auto consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_ASYNC_PUSH",
      .durable_name = durable,
      .filter_subject = subject,
      .deliver_subject = "demo.js.async.push.delivery",
      .type = natscpp::js_consumer_type::push,
  });

  auto consumer = js.push_subscribe(subject, durable);

  // Start waiting for the message before publishing so the receive does not block the main thread.
  auto next_msg = std::async(std::launch::async, [&consumer] {
    return consumer.next(std::chrono::seconds(2));
  });

  js.publish(subject, "hello-jetstream-async-push");

  auto msg = next_msg.get();
  std::cout << "jetstream async push received: " << msg.data() << '\n';
  return 0;
}
