// Message headers and trace-context propagation: the publisher injects W3C trace headers, the subscriber
// reads them back.
#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <natscpp/connection.hpp>
#include <natscpp/trace.hpp>

#include "example_common.hpp"

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});
    if (!nc.has_header_support()) {
      std::cerr << "server does not support headers\n";
      return 1;
    }

    auto sub = nc.subscribe_sync("demo.headers");

    auto msg = natscpp::message::create("demo.headers", /*reply_to=*/"", R"({"order":"A-1"})");
    msg.set_header("Content-Type", "application/json");
    msg.add_header("X-Tag", "blue");
    msg.add_header("X-Tag", "green");  // headers can carry several values

    natscpp::message_trace_carrier out_carrier(msg);
    const std::vector<std::pair<std::string, std::string>> trace = {
        {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
        {"tracestate", "vendor=value"},
    };
    natscpp::inject_trace_context(out_carrier, trace);
    nc.publish(std::move(msg));

    auto received = sub.next_message(std::chrono::seconds(1));
    std::cout << "payload: " << received.data() << '\n';
    std::cout << "Content-Type: " << received.header("Content-Type") << '\n';
    for (const auto& tag : received.header_values("X-Tag")) {
      std::cout << "X-Tag: " << tag << '\n';
    }

    natscpp::message_trace_carrier in_carrier(received);
    std::unordered_map<std::string, std::string> extracted;
    natscpp::extract_trace_context(in_carrier, std::vector<std::string>{"traceparent", "tracestate"}, extracted);
    for (const auto& [key, value] : extracted) {
      std::cout << "trace " << key << ": " << value << '\n';
    }
    return extracted.size() == 2 ? 0 : 1;
  });
}
