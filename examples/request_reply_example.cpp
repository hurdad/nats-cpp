#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto responder = nc.subscribe_async("demo.svc.echo", [&nc](natscpp::message msg) {
    const auto reply_to = msg.reply_to();
    if (reply_to.empty()) {
      return;
    }

    std::string response = "echo:";
    response += msg.data();

    natscpp::throw_on_error(
        natsConnection_PublishRequest(nc.native_handle(), reply_to.data(), nullptr, response.data(),
                                      static_cast<int>(response.size())),
        "natsConnection_PublishRequest");
  });

  auto reply = nc.request_sync("demo.svc.echo", "ping", std::chrono::seconds(1));
  std::cout << "request/reply response: " << reply.data() << '\n';

  responder.unsubscribe();
  return reply.data() == "echo:ping" ? 0 : 1;
}
