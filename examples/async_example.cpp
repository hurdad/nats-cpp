#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto sub = nc.subscribe_async("demo.async.events", [](natscpp::message msg) {
    std::cout << "async received: " << msg.data() << '\n';
  });

  nc.publish("demo.async.events", "hello-async");
  nc.flush(std::chrono::seconds(1));
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
