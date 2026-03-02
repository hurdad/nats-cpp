#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/jetstream.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
  natscpp::jetstream js(nc);

  constexpr auto subject = "demo.js.async.events";
  constexpr auto durable = "demo-js-async-worker";

  auto consumer = js.pull_subscribe(subject, durable);

  auto next_msg = std::async(std::launch::async, [&consumer] {
    return consumer.next(std::chrono::seconds(2));
  });

  js.publish(subject, "hello-jetstream-async");

  auto msg = next_msg.get();
  std::cout << "jetstream async received: " << msg.data() << '\n';
  return 0;
}
