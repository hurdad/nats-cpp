#pragma once

#include <nats/nats.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

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
  explicit subscription(natsSubscription* raw, std::function<void()> on_release = {})
      : sub_(raw), on_release_(std::move(on_release)) {}

  ~subscription() { release_callback(); }

  subscription(subscription&& other) noexcept
      : sub_(std::move(other.sub_)), on_release_(std::move(other.on_release_)) {}

  subscription& operator=(subscription&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release_callback();
    sub_ = std::move(other.sub_);
    on_release_ = std::move(other.on_release_);
    return *this;
  }

  subscription(const subscription&) = delete;
  subscription& operator=(const subscription&) = delete;

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }
  [[nodiscard]] natsSubscription* native_handle() const noexcept { return sub_.get(); }

  void unsubscribe() {
    throw_on_error(natsSubscription_Unsubscribe(sub_.get()), "natsSubscription_Unsubscribe");
    release_callback();
  }

  [[nodiscard]] message next_message(std::chrono::milliseconds timeout) {
    natsMsg* raw{};
    throw_on_error(natsSubscription_NextMsg(&raw, sub_.get(), static_cast<int64_t>(timeout.count())),
                   "natsSubscription_NextMsg");
    return message{raw};
  }

 private:
  void release_callback() noexcept {
    if (on_release_) {
      on_release_();
      on_release_ = {};
    }
  }

  std::unique_ptr<natsSubscription, detail::subscription_deleter> sub_;
  std::function<void()> on_release_;
};

}  // namespace natscpp
