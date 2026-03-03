#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

// Delivery pattern: WORK QUEUE
//   A single durable consumer is shared across all subscribers. NATS delivers each message to
//   exactly one subscriber (competing consumers). This is the pattern shown below.
//
// To implement PUB/SUB fan-out instead:
//   Create a separate consumer group per subscriber, each with a unique durable name. Every
//   subscriber then independently receives every message.
//
//     js.create_consumer_group({.stream = "STREAM", .durable_name = "worker-a",
//                                .filter_subject = subject, .type = pull});
//     js.create_consumer_group({.stream = "STREAM", .durable_name = "worker-b",
//                                .filter_subject = subject, .type = pull});
//     auto consumer_a = js.pull_subscribe(subject, "worker-a");
//     auto consumer_b = js.pull_subscribe(subject, "worker-b");
//     // consumer_a and consumer_b each receive every published message

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
