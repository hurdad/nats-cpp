// JetStream pull consumer, blocking API: publish a few messages, fetch them in one batch, acknowledge each.
#include <chrono>
#include <iostream>
#include <string>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

#include "example_common.hpp"

// Delivery pattern: WORK QUEUE
//   One durable consumer shared by every subscriber: each message goes to exactly one of them (competing
//   consumers). For PUB/SUB fan-out, give each subscriber its own durable consumer instead; each then receives
//   every message.

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});
    natscpp::jetstream js(nc);

    constexpr auto stream = "DEMO_JS_SYNC_PULL";
    constexpr auto subject = "demo.js.sync.pull.events";
    constexpr auto durable = "demo-js-sync-pull-worker";

    (void)js.create_stream({.name = stream, .subjects = {"demo.js.sync.pull.>"}});
    example::scope_exit cleanup([&] { js.delete_stream(stream); });  // the demo leaves nothing behind
    (void)js.create_consumer_group({
        .stream = stream,
        .durable_name = durable,
        .filter_subject = subject,
        .type = natscpp::js_consumer_type::pull,
    });

    auto consumer = js.pull_subscribe(subject, durable);
    for (int i = 1; i <= 3; ++i) {
      auto ack = js.publish(subject, "order-" + std::to_string(i));  // waits for the stream to store it
      std::cout << "stored seq " << ack.sequence << " in " << ack.stream << '\n';
    }

    // One pull request for up to 10 messages; returns once the available ones have arrived.
    int received = 0;
    while (received < 3) {
      for (auto& msg : consumer.fetch(10, std::chrono::seconds(2))) {
        std::cout << "jetstream sync pull received: " << msg.data() << '\n';
        msg.ack();  // explicit ack policy: unacknowledged messages are redelivered after the ack wait
        ++received;
      }
    }
    return 0;
  });
}
