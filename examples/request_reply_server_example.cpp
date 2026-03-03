#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto service = nc.subscribe_sync("svc.echo");
  std::cout << "[server] listening on svc.echo\n";

  while (true) {
    auto req = service.next_message(std::chrono::seconds(30));
    if (req.reply_to().empty()) {
      continue;
    }

    std::string response = "echo:";
    response += req.data();
    nc.publish(req.reply_to(), response);

    std::cout << "[server] request='" << req.data() << "' response='" << response << "'\n";
  }
}
