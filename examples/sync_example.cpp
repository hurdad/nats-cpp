// Core NATS with the blocking API: publish/subscribe, then request/reply with a local responder.
#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});

    // Pub/sub: subscribe first, so the message published next is not missed.
    auto sub = nc.subscribe_sync("demo.sync.events");
    nc.publish("demo.sync.events", "hello-sync");

    auto msg = sub.next_message(std::chrono::seconds(1));  // throws NATS_TIMEOUT if nothing arrives
    std::cout << "sync received: " << msg.data() << '\n';

    // Request/reply: the responder runs on another thread because both sides block.
    auto echo_sub = nc.subscribe_sync("demo.sync.echo");
    auto responder = std::async(std::launch::async, [&] {
      auto req = echo_sub.next_message(std::chrono::seconds(1));
      if (!req.reply_to().empty()) {
        nc.publish(req.reply_to(), req.data());
      }
    });

    auto reply = nc.request_sync("demo.sync.echo", "ping", std::chrono::seconds(1));
    std::cout << "sync reply: " << reply.data() << '\n';

    responder.get();
    return reply.data() == "ping" ? 0 : 1;
  });
}
