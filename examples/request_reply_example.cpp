// Request/reply in one process: a responder on a background thread and a client request.
// See request_reply_server_example.cpp and request_reply_client_example.cpp for the two-process version.
#include <chrono>
#include <future>
#include <iostream>
#include <string>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});

    // Subscribe before sending the request so it is not missed.
    auto responder = nc.subscribe_sync("demo.svc.echo");

    // The responder must run concurrently: both sides block waiting for each other.
    auto responder_task = std::async(std::launch::async, [&] {
      auto req = responder.next_message(std::chrono::seconds(1));
      if (!req.reply_to().empty()) {
        nc.publish(req.reply_to(), "echo:" + std::string(req.data()));
      }
    });

    auto reply = nc.request_sync("demo.svc.echo", "ping", std::chrono::seconds(1));
    std::cout << "request/reply response: " << reply.data() << '\n';

    responder_task.get();
    return reply.data() == "echo:ping" ? 0 : 1;
  });
}
