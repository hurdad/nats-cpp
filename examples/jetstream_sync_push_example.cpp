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

  constexpr auto subject = "demo.js.sync.push.events";
  constexpr auto durable = "demo-js-sync-push-worker";

  [[maybe_unused]] auto stream = js.create_stream({.name = "DEMO_JS_SYNC_PUSH", .subjects = {"demo.js.sync.push.>"}});
  [[maybe_unused]] auto consumer_group = js.create_consumer_group({
      .stream = "DEMO_JS_SYNC_PUSH",
      .durable_name = durable,
      .filter_subject = subject,
      .deliver_subject = "deliver.js.sync.push",
      .type = natscpp::js_consumer_type::push,
  });

  auto consumer = js.push_subscribe(subject, durable);

  (void)js.publish(subject, "hello-jetstream-sync-push");

  auto msg = consumer.next(std::chrono::seconds(2));
  std::cout << "jetstream sync push received: " << msg.data() << '\n';
  return 0;
}
