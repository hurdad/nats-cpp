// Client for request_reply_server_example.cpp: sends one request to "svc.echo".
#include <chrono>
#include <iostream>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});

    try {
      auto reply = nc.request_sync("svc.echo", "ping", std::chrono::seconds(2));
      std::cout << "[client] reply='" << reply.data() << "'\n";
      return reply.data() == "echo:ping" ? 0 : 1;
    } catch (const natscpp::nats_error& e) {
      if (e.status() == NATS_NO_RESPONDERS) {  // nobody subscribed to svc.echo
        std::cerr << "[client] no service on svc.echo: start natscpp_request_reply_server_example first\n";
        return 1;
      }
      if (e.status() == NATS_TIMEOUT) {
        std::cerr << "[client] the service did not reply within 2s\n";
        return 1;
      }
      throw;
    }
  });
}
