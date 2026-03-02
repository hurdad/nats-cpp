#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

  static message create(std::string_view subject, std::string_view reply_to, std::string_view payload) {
    natsMsg* raw{};
    throw_on_error(natsMsg_Create(&raw, std::string(subject).c_str(),
                                  reply_to.empty() ? nullptr : std::string(reply_to).c_str(), payload.data(),
                                  static_cast<int>(payload.size())),
                   "natsMsg_Create");
    return message{raw};
  }

  [[nodiscard]] natsMsg* native_handle() const noexcept { return msg_.get(); }
  [[nodiscard]] natsMsg* release() noexcept { return msg_.release(); }
  [[nodiscard]] bool valid() const noexcept { return msg_ != nullptr; }

  [[nodiscard]] std::string_view subject() const noexcept {
    if (!msg_) return {};
    const char* v = natsMsg_GetSubject(msg_.get());
    return v != nullptr ? std::string_view{v} : std::string_view{};
  }

  [[nodiscard]] std::string_view reply_to() const noexcept {
    if (!msg_) return {};
    const char* v = natsMsg_GetReply(msg_.get());
    return v != nullptr ? std::string_view{v} : std::string_view{};
  }

  [[nodiscard]] std::string_view data() const noexcept {
    if (!msg_) return {};
    const char* bytes = reinterpret_cast<const char*>(natsMsg_GetData(msg_.get()));
    const int len = natsMsg_GetDataLength(msg_.get());
    return bytes != nullptr && len >= 0 ? std::string_view{bytes, static_cast<std::size_t>(len)} : std::string_view{};
  }

  [[nodiscard]] std::string header(std::string_view key) const {
    if (!msg_) return {};
    const char* val = nullptr;
    if (natsMsgHeader_Get(msg_.get(), std::string(key).c_str(), &val) != NATS_OK || val == nullptr) {
      return {};
    }
    return std::string{val};
  }

  void set_header(std::string_view key, std::string_view value) {
    if (!msg_) return;
    throw_on_error(natsMsgHeader_Set(msg_.get(), std::string(key).c_str(), std::string(value).c_str()),
                   "natsMsgHeader_Set");
  }

  void add_header(std::string_view key, std::string_view value) {
    if (!msg_) return;
    throw_on_error(natsMsgHeader_Add(msg_.get(), std::string(key).c_str(), std::string(value).c_str()),
                   "natsMsgHeader_Add");
  }

  void delete_header(std::string_view key) {
    if (!msg_) return;
    throw_on_error(natsMsgHeader_Delete(msg_.get(), std::string(key).c_str()), "natsMsgHeader_Delete");
  }

  [[nodiscard]] std::vector<std::string> header_values(std::string_view key) const {
    if (!msg_) return {};
    const char** values = nullptr;
    int count = 0;
    throw_on_error(natsMsgHeader_Values(msg_.get(), std::string(key).c_str(), &values, &count), "natsMsgHeader_Values");

    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      result.emplace_back(values[i] != nullptr ? values[i] : "");
    }
    return result;
  }

  [[nodiscard]] std::vector<std::string> header_keys() const {
    if (!msg_) return {};
    const char** keys = nullptr;
    int count = 0;
    throw_on_error(natsMsgHeader_Keys(msg_.get(), &keys, &count), "natsMsgHeader_Keys");

    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      result.emplace_back(keys[i] != nullptr ? keys[i] : "");
    }
    return result;
  }

  [[nodiscard]] bool is_no_responders() const noexcept { return msg_ != nullptr && natsMsg_IsNoResponders(msg_.get()); }

  void ack() { throw_on_error(natsMsg_Ack(msg_.get(), nullptr), "natsMsg_Ack"); }
  void nak() { throw_on_error(natsMsg_Nak(msg_.get(), nullptr), "natsMsg_Nak"); }
  void in_progress() { throw_on_error(natsMsg_InProgress(msg_.get(), nullptr), "natsMsg_InProgress"); }
  void term() { throw_on_error(natsMsg_Term(msg_.get(), nullptr), "natsMsg_Term"); }

  [[nodiscard]] uint64_t sequence() const noexcept { return msg_ != nullptr ? natsMsg_GetSequence(msg_.get()) : 0; }
  [[nodiscard]] int64_t timestamp() const noexcept { return msg_ != nullptr ? natsMsg_GetTime(msg_.get()) : 0; }

 private:
  std::unique_ptr<natsMsg, detail::msg_deleter> msg_;
};

}  // namespace natscpp
