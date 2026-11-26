#pragma once

// Shared by the examples: where the server is, and turning errors into a message and exit code 1.

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include <natscpp/error.hpp>
#include <natscpp/library.hpp>

namespace example {

// Server URL from $NATS_URL, defaulting to a local server.
inline std::string server_url() {
  const char* url = std::getenv("NATS_URL");
  return url != nullptr && *url != '\0' ? std::string{url} : std::string{"nats://127.0.0.1:4222"};
}

// Runs body() and reports any exception instead of letting it terminate the program. Then waits for nats.c's
// threads: a destroyed connection is closed in the background, and exiting while that thread still runs (for
// example inside OpenSSL, which tears down its globals at exit) can crash.
template <typename Body>
int run(Body&& body) {
  const int rc = [&]() -> int {
    try {
      return std::forward<Body>(body)();  // every natscpp object the body created is destroyed on return
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
  }();
  if (const natsStatus s = natscpp::nats_CloseAndWait(std::chrono::seconds(5)); s != NATS_OK) {
    std::cerr << "warning: nats.c did not shut down cleanly: " << natsStatus_GetText(s) << '\n';
  }
  return rc;
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
