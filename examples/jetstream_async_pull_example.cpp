// JetStream pull consumer driven from a worker thread: the worker fetches batches and acknowledges them
// while the main thread publishes. (Pull consumers are request-driven; for callback delivery see
// jetstream_async_push_example.cpp.)
#include <atomic>
#include <chrono>
#include <future>
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

    constexpr auto stream = "DEMO_JS_ASYNC_PULL";
    constexpr auto subject = "demo.js.async.pull.events";
    constexpr auto durable = "demo-js-async-pull-worker";
    constexpr int total = 5;

    (void)js.create_stream({.name = stream, .subjects = {"demo.js.async.pull.>"}});
    example::scope_exit cleanup([&] { js.delete_stream(stream); });
    (void)js.create_consumer_group({
        .stream = stream,
        .durable_name = durable,
        .filter_subject = subject,
        .type = natscpp::js_consumer_type::pull,
    });

    auto consumer = js.pull_subscribe(subject, durable);
    auto worker = std::async(std::launch::async, [&consumer] {
      int received = 0;
      while (received < total) {
        try {
          for (auto& msg : consumer.fetch(10, std::chrono::seconds(2))) {
            std::cout << "jetstream async pull received: " << msg.data() << '\n';
            msg.ack();
            ++received;
          }
        } catch (const natscpp::nats_error& e) {
          if (e.status() != NATS_TIMEOUT) throw;  // nothing yet: pull again
        }
      }
      return received;
    });

    for (int i = 1; i <= total; ++i) {
      (void)js.publish(subject, "job-" + std::to_string(i));
    }
    return worker.get() == total ? 0 : 1;  // rethrows the worker's exception, if any
  });
}
