// Connection lifecycle: reconnect settings, state callbacks, statistics, and a graceful drain on shutdown.
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    // The callbacks run on a nats.c thread, possibly after this function's locals are gone (closed_cb fires
    // once the connection is fully closed), so they share state through a shared_ptr rather than references.
    auto closed = std::make_shared<std::promise<void>>();
    auto closed_future = closed->get_future();

    natscpp::connection_options opts;
    opts.set_url(example::server_url());
    opts.set_max_reconnect(10);                                // give up after 10 attempts
    opts.set_reconnect_wait(std::chrono::milliseconds(500));
    opts.set_reconnect_jitter(100, 1000);                      // spread reconnects of many clients
    opts.set_disconnected_cb([](natsConnection*) { std::cout << "[cb] disconnected\n"; });
    opts.set_reconnected_cb([](natsConnection*) { std::cout << "[cb] reconnected\n"; });
    opts.set_error_handler([](natsConnection*, natsSubscription*, natsStatus s) {
      std::cout << "[cb] async error: " << natsStatus_GetText(s) << '\n';  // e.g. a slow consumer
    });
    opts.set_closed_cb([closed](natsConnection*) {
      std::cout << "[cb] closed\n";
      closed->set_value();
    });

    natscpp::connection nc(opts);
    std::cout << "connected: " << std::boolalpha << nc.connected() << '\n';

    auto received = std::make_shared<std::atomic<int>>(0);
    auto sub = nc.subscribe_async("demo.lifecycle", [received](natscpp::message) { ++*received; });
    for (int i = 0; i < 100; ++i) {
      nc.publish("demo.lifecycle", "tick");
    }

    // Drain: stop new deliveries, finish the messages already received, flush, then close the connection.
    nc.drain(std::chrono::seconds(5));
    if (closed_future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
      std::cerr << "connection did not close after drain\n";
      return 1;
    }

    auto stats = nc.get_statistics();
    std::cout << "handled " << *received << " messages; out " << stats.out_messages << " msgs / "
              << stats.out_bytes << " bytes, in " << stats.in_messages << " msgs\n";
    return *received == 100 ? 0 : 1;
  });
}
