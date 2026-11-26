// JetStream push consumer, blocking API: the server pushes messages to the consumer's deliver subject and
// next() takes them one at a time.
#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

#include "example_common.hpp"

// Delivery pattern: WORK QUEUE
//   One durable consumer shared by every subscriber: each message goes to exactly one of them. For PUB/SUB
//   fan-out, give each subscriber its own durable consumer (and deliver_subject); each then receives every
//   message.

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});
    natscpp::jetstream js(nc);

    constexpr auto stream = "DEMO_JS_SYNC_PUSH";
    constexpr auto subject = "demo.js.sync.push.events";
    constexpr auto durable = "demo-js-sync-push-worker";

    (void)js.create_stream({.name = stream, .subjects = {"demo.js.sync.push.>"}});
    example::scope_exit cleanup([&] { js.delete_stream(stream); });
    (void)js.create_consumer_group({
        .stream = stream,
        .durable_name = durable,
        .filter_subject = subject,
        .deliver_subject = "deliver.js.sync.push",
        .type = natscpp::js_consumer_type::push,
    });

    auto consumer = js.push_subscribe(subject, durable);
    (void)js.publish(subject, "hello-jetstream-sync-push");

    auto msg = consumer.next(std::chrono::seconds(2));
    auto meta = msg.get_metadata();
    std::cout << "jetstream sync push received: " << msg.data() << " (stream seq " << meta.sequence_stream
              << ", delivery " << meta.num_delivered << ")\n";
    msg.ack();
    return 0;
  });
}
