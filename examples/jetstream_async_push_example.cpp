// JetStream push consumer with a callback: jetstream::subscribe_async runs the handler on a nats.c thread
// for each message. With manual_ack the handler acknowledges (or naks) each message itself.
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <string>

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

    constexpr auto stream = "DEMO_JS_ASYNC_PUSH";
    constexpr auto subject = "demo.js.async.push.events";
    constexpr auto durable = "demo-js-async-push-worker";
    constexpr int total = 3;

    (void)js.create_stream({.name = stream, .subjects = {"demo.js.async.push.>"}});
    example::scope_exit cleanup([&] { js.delete_stream(stream); });
    (void)js.create_consumer_group({
        .stream = stream,
        .durable_name = durable,
        .filter_subject = subject,
        .deliver_subject = "deliver.js.async.push",
        .type = natscpp::js_consumer_type::push,
    });

    std::atomic<int> received{0};
    std::promise<void> all_received;
    auto sub = js.subscribe_async(
        subject, durable,
        [&](natscpp::message msg) {
          std::cout << "jetstream async push received: " << msg.data() << '\n';
          msg.ack();
          if (++received == total) all_received.set_value();  // redeliveries past total are just acked
        },
        /*manual_ack=*/true);

    for (int i = 1; i <= total; ++i) {
      (void)js.publish(subject, "event-" + std::to_string(i));
    }
    const bool ok = all_received.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready;

    // The handler uses locals of this function, so make sure it has finished for good before they go out of
    // scope: unsubscribe, then wait for the on-complete callback, which runs after the handler's last return.
    std::promise<void> completed;
    sub.set_on_complete_callback([&completed] { completed.set_value(); });
    sub.unsubscribe();
    completed.get_future().wait();

    if (!ok) {
      std::cerr << "received only " << received << " of " << total << " messages\n";
      return 1;
    }
    return 0;
  });
}
