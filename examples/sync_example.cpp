#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  // Pub/sub: publish a message and receive it synchronously.
  auto sub = nc.subscribe_sync("demo.sync.events");
  nc.publish("demo.sync.events", "hello-sync");

  auto msg = sub.next_message(std::chrono::seconds(1));
  std::cout << "sync received: " << msg.data() << '\n';

  // Request/reply: set up a local echo responder before sending the request.
  // The responder runs in a separate thread because both sides block.
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
  echo_sub.unsubscribe();
  return 0;
}
