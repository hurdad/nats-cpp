#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.sync.events";
  constexpr auto durable = "demo-js-sync-worker";

  auto consumer = js.pull_subscribe(subject, durable);
  js.publish(subject, "hello-jetstream-sync");

  auto msg = consumer.next(std::chrono::seconds(2));
  std::cout << "jetstream sync received: " << msg.data() << '\n';
  return 0;
}
