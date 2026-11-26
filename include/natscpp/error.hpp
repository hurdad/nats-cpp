#pragma once

#include <nats/nats.h>

#include <stdexcept>
#include <string>

namespace natscpp {

/**
 * @brief Exception thrown when a nats.c call fails.
 */
class nats_error : public std::runtime_error {
 public:
  nats_error(natsStatus status, const std::string& message) : std::runtime_error(message), status_(status) {}

  [[nodiscard]] natsStatus status() const noexcept { return status_; }

 private:
  natsStatus status_;
};

/**
 * @brief Throws nats_error when status is not NATS_OK.
 */
inline void throw_on_error(natsStatus status, const char* context) {
  if (status == NATS_OK) {
    return;
  }

  std::string message = context != nullptr ? context : "nats error";
  const char* text = natsStatus_GetText(status);
  if (text != nullptr) {
    message += ": ";
    message += text;
  }

  // nats.c keeps the last error per thread and most calls do not clear it, so only append it when it is for
  // this same status; otherwise it may describe an earlier, unrelated failure.
  natsStatus last_status = NATS_OK;
  const char* last = nats_GetLastError(&last_status);
  if (last != nullptr && *last != '\0' && last_status == status) {
    message += " | ";
    message += last;
  }

  throw nats_error(status, message);
}

}  // namespace natscpp
