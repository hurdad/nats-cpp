#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto sub = nc.subscribe_sync("demo.sync.events");
  nc.publish("demo.sync.events", "hello-sync");

  auto msg = sub.next_message(std::chrono::seconds(1));
  std::cout << "sync received: " << msg.data() << '\n';

  auto reply = nc.request_sync("demo.sync.echo", "ping", std::chrono::seconds(1));
  std::cout << "sync reply: " << reply.data() << '\n';
  return 0;
}
