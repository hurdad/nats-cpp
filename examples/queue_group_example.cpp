// Queue groups: subscribers that share a queue name split the messages between them (load balancing),
// while a plain subscriber on the same subject still sees every message.
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});
    constexpr int total = 20;

    std::atomic<int> worker_a{0};
    std::atomic<int> worker_b{0};
    std::atomic<int> monitor{0};
    auto sub_a = nc.subscribe_queue_async("demo.jobs", "workers", [&](natscpp::message) { ++worker_a; });
    auto sub_b = nc.subscribe_queue_async("demo.jobs", "workers", [&](natscpp::message) { ++worker_b; });
    auto sub_monitor = nc.subscribe_async("demo.jobs", [&](natscpp::message) { ++monitor; });
    nc.flush(std::chrono::seconds(1));  // make sure the server has all three subscriptions

    for (int i = 0; i < total; ++i) {
      nc.publish("demo.jobs", "job-" + std::to_string(i));
    }
    nc.flush(std::chrono::seconds(1));
    for (int i = 0; i < 100 && (worker_a + worker_b < total || monitor < total); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "worker a: " << worker_a << ", worker b: " << worker_b << " (together " << worker_a + worker_b
              << " of " << total << ")\n";
    std::cout << "monitor (not in the group): " << monitor << '\n';

    // drain() lets queued messages finish before each subscription closes.
    sub_a.drain();
    sub_b.drain();
    sub_monitor.drain();
    sub_a.wait_for_drain_completion(std::chrono::seconds(1));
    sub_b.wait_for_drain_completion(std::chrono::seconds(1));
    sub_monitor.wait_for_drain_completion(std::chrono::seconds(1));
    return worker_a + worker_b == total && monitor == total ? 0 : 1;
  });
}
