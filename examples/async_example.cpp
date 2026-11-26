// Core NATS with callbacks: an async subscription, then an async request answered by an async responder.
#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});

    // Handlers run on a nats.c delivery thread. The subscription keeps its handler alive until nats.c is done
    // with it, so destroying or unsubscribing it while a message is being handled is safe.
    std::promise<void> received;
    auto sub = nc.subscribe_async("demo.async.events", [&received](natscpp::message msg) {
      std::cout << "async received: " << msg.data() << '\n';
      received.set_value();
    });

    nc.publish("demo.async.events", "hello-async");
    nc.flush(std::chrono::seconds(1));
    if (received.get_future().wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
      std::cerr << "no message within 1s\n";
      return 1;
    }
    sub.unsubscribe();

    // Request/reply: an async echo responder, and a request whose reply arrives as a std::future.
    auto echo_sub = nc.subscribe_async("demo.async.echo", [&nc](natscpp::message req) {
      if (!req.reply_to().empty()) {
        nc.publish(req.reply_to(), req.data());
      }
    });

    auto fut = nc.request_async("demo.async.echo", "ping-async", std::chrono::seconds(1));
    auto reply = fut.get();  // rethrows the request's nats_error, if any
    std::cout << "async reply: " << reply.data() << '\n';
    return reply.data() == "ping-async" ? 0 : 1;
  });
}
