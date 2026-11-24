#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  std::promise<void> received;
  auto sub = nc.subscribe_async("demo.async.events", [&received](natscpp::message msg) {
    std::cout << "async received: " << msg.data() << '\n';
    received.set_value();
  });

  nc.publish("demo.async.events", "hello-async");
  nc.flush(std::chrono::seconds(1));
  // The handler runs on a nats.c thread; wait for it before unsubscribing.
  received.get_future().wait_for(std::chrono::seconds(1));
  sub.unsubscribe();

  // Request/reply: subscribe an async echo responder before sending the request.
  auto echo_sub = nc.subscribe_async("demo.async.echo", [&nc](natscpp::message req) {
    if (!req.reply_to().empty()) {
      nc.publish(req.reply_to(), req.data());
    }
  });

  auto fut = nc.request_async("demo.async.echo", "ping-async", std::chrono::seconds(1));
  auto reply = fut.get();
  std::cout << "async reply: " << reply.data() << '\n';

  echo_sub.unsubscribe();
  return 0;
}
