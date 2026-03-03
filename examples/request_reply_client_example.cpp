#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto reply = nc.request_sync("svc.echo", "ping", std::chrono::seconds(2));
  std::cout << "[client] reply='" << reply.data() << "'\n";

  return reply.data() == "echo:ping" ? 0 : 1;
}
