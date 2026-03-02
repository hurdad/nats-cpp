#pragma once

#include <nats/nats.h>

#include <chrono>
#include <memory>

#include <natscpp/detail/deleters.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>

namespace natscpp {

/**
 * @brief RAII wrapper for natsSubscription.
 */
class subscription {
 public:
  subscription() = default;
  explicit subscription(natsSubscription* raw) : sub_(raw) {}

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }
  [[nodiscard]] natsSubscription* native_handle() const noexcept { return sub_.get(); }

  void unsubscribe() {
    throw_on_error(natsSubscription_Unsubscribe(sub_.get()), "natsSubscription_Unsubscribe");
  }

  [[nodiscard]] message next_message(std::chrono::milliseconds timeout) {
    natsMsg* raw{};
    throw_on_error(natsSubscription_NextMsg(&raw, sub_.get(), static_cast<int64_t>(timeout.count())),
                   "natsSubscription_NextMsg");
    return message{raw};
  }

 private:
  std::unique_ptr<natsSubscription, detail::subscription_deleter> sub_;
};

}  // namespace natscpp
