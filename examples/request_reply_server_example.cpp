// Echo service on "svc.echo". Run it, then run natscpp_request_reply_client_example. Ctrl-C to stop.
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

namespace {
std::atomic<bool> stop_requested{false};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): set by the signal handler
extern "C" void on_signal(int) { stop_requested = true; }
}  // namespace

int main() {
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::cout << std::unitbuf;  // show each line immediately, also when piped to a file

  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});

    // A queue group lets several copies of this server share the load: each request goes to one of them.
    auto service = nc.subscribe_queue_sync("svc.echo", "echo-workers");
    std::cout << "[server] listening on svc.echo\n";

    while (!stop_requested) {
      natscpp::message req;
      try {
        req = service.next_message(std::chrono::milliseconds(500));  // short wait so a stop request is seen
      } catch (const natscpp::nats_error& e) {
        if (e.status() == NATS_TIMEOUT) continue;  // idle
        throw;
      }
      if (req.reply_to().empty()) continue;  // a plain publish, not a request

      const std::string response = "echo:" + std::string(req.data());
      nc.publish(req.reply_to(), response);
      std::cout << "[server] request='" << req.data() << "' response='" << response << "'\n";
    }

    // Stop taking requests and let in-flight replies go out before exiting.
    service.unsubscribe();
    nc.flush(std::chrono::seconds(1));
    std::cout << "[server] stopped\n";
    return 0;
  });
}
