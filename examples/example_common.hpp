#pragma once

// Shared by the examples: where the server is, and turning errors into a message and exit code 1.

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include <natscpp/error.hpp>

namespace example {

// Server URL from $NATS_URL, defaulting to a local server.
inline std::string server_url() {
  const char* url = std::getenv("NATS_URL");
  return url != nullptr && *url != '\0' ? std::string{url} : std::string{"nats://127.0.0.1:4222"};
}

// Runs body() and reports any exception instead of letting it terminate the program.
template <typename Body>
int run(Body&& body) {
  try {
    return std::forward<Body>(body)();
  } catch (const natscpp::nats_error& e) {
    std::cerr << "error: " << e.what() << '\n';
    if (e.status() == NATS_NO_SERVER) {
      std::cerr << "hint: start a server (e.g. `nats-server -js`) or set NATS_URL\n";
    }
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}

// Runs a cleanup action when the scope ends, also on an exception (e.g. deleting a demo stream).
template <typename Fn>
class scope_exit {
 public:
  explicit scope_exit(Fn fn) : fn_(std::move(fn)) {}
  ~scope_exit() {
    try {
      fn_();
    } catch (const std::exception& e) {
      std::cerr << "cleanup failed: " << e.what() << '\n';
    }
  }
  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;

 private:
  Fn fn_;
};

}  // namespace example
