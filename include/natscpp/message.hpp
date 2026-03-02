#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include <natscpp/detail/deleters.hpp>
#include <natscpp/error.hpp>

namespace natscpp {

/**
 * @brief Owns a natsMsg and provides high-level accessors.
 */
class message {
 public:
  message() = default;
  explicit message(natsMsg* raw) : msg_(raw) {}

  [[nodiscard]] natsMsg* native_handle() const noexcept { return msg_.get(); }
  [[nodiscard]] bool valid() const noexcept { return msg_ != nullptr; }

  [[nodiscard]] std::string_view subject() const noexcept {
    const char* v = natsMsg_GetSubject(msg_.get());
    return v != nullptr ? std::string_view{v} : std::string_view{};
  }

  [[nodiscard]] std::string_view reply_to() const noexcept {
    const char* v = natsMsg_GetReply(msg_.get());
    return v != nullptr ? std::string_view{v} : std::string_view{};
  }

  [[nodiscard]] std::string_view data() const noexcept {
    const char* bytes = reinterpret_cast<const char*>(natsMsg_GetData(msg_.get()));
    const int len = natsMsg_GetDataLength(msg_.get());
    return bytes != nullptr && len >= 0 ? std::string_view{bytes, static_cast<std::size_t>(len)} : std::string_view{};
  }

  [[nodiscard]] std::string header(std::string_view key) const {
    const char* val = nullptr;
    if (natsMsgHeader_Get(msg_.get(), std::string(key).c_str(), &val) != NATS_OK || val == nullptr) {
      return {};
    }
    return std::string{val};
  }

  void set_header(std::string_view key, std::string_view value) {
    throw_on_error(natsMsgHeader_Set(msg_.get(), std::string(key).c_str(), std::string(value).c_str()),
                   "natsMsgHeader_Set");
  }

 private:
  std::unique_ptr<natsMsg, detail::msg_deleter> msg_;
};

}  // namespace natscpp
