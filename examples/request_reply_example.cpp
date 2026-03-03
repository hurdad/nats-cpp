#include <chrono>
#include <future>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  // Subscribe synchronously before sending the request so no message is missed.
  auto responder = nc.subscribe_sync("demo.svc.echo");

  // The responder must run concurrently — both sides block waiting for each other.
  auto responder_task = std::async(std::launch::async, [&] {
    auto req = responder.next_message(std::chrono::seconds(1));
    if (!req.reply_to().empty()) {
      std::string response = "echo:";
      response += req.data();
      nc.publish(req.reply_to(), response);
    }
  });

  auto reply = nc.request_sync("demo.svc.echo", "ping", std::chrono::seconds(1));
  std::cout << "request/reply response: " << reply.data() << '\n';

  responder_task.get();
  responder.unsubscribe();
  return reply.data() == "echo:ping" ? 0 : 1;
}
