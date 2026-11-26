#pragma once

#include <nats/nats.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/detail/deleters.hpp>
#include <natscpp/detail/validate.hpp>
#include <natscpp/error.hpp>

namespace natscpp {

/**
 * @brief Owns a natsMsg and provides high-level accessors.
 */
class message {
 public:
  struct metadata {
    uint64_t sequence_stream = 0;
    uint64_t sequence_consumer = 0;
    uint64_t num_delivered = 0;
    uint64_t num_pending = 0;
    int64_t timestamp = 0;
    std::string stream;
    std::string consumer;
    std::string domain;
  };

  message() = default;
  explicit message(natsMsg* raw) : msg_(raw) {}

  static message create(std::string_view subject, std::string_view reply_to, std::string_view payload) {
    // natsMsg_Create works without the library, but destroying the message needs it (nats.c's message
    // garbage collector), so a message created before any connection would crash when freed. nats_Open is a
    // no-op once the library is open.
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw std::invalid_argument("message::create: payload exceeds INT_MAX bytes");
    }
    const std::string subj = detail::subject(subject, "message::create");
    const std::string reply = reply_to.empty() ? std::string{} : detail::subject(reply_to, "message::create reply_to");
    throw_on_error(::nats_Open(-1), "nats_Open");
    natsMsg* raw{};
    throw_on_error(natsMsg_Create(&raw, subj.c_str(), reply_to.empty() ? nullptr : reply.c_str(), payload.data(),
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
    const char* bytes = natsMsg_GetData(msg_.get());
    const int len = natsMsg_GetDataLength(msg_.get());
    return bytes != nullptr && len >= 0 ? std::string_view{bytes, static_cast<std::size_t>(len)} : std::string_view{};
  }

  [[nodiscard]] std::string header(std::string_view key) const {
    if (!msg_) return {};
    const char* val = nullptr;
    if (natsMsgHeader_Get(msg_.get(), detail::c_string(key, "key").c_str(), &val) != NATS_OK || val == nullptr) {
      return {};
    }
    return std::string{val};
  }

  void set_header(std::string_view key, std::string_view value) {
    if (!msg_) return;
    throw_on_error(natsMsgHeader_Set(msg_.get(), detail::header_key(key, "set_header").c_str(),
                                     detail::header_value(value, "set_header").c_str()),
                   "natsMsgHeader_Set");
  }

  void add_header(std::string_view key, std::string_view value) {
    if (!msg_) return;
    throw_on_error(natsMsgHeader_Add(msg_.get(), detail::header_key(key, "add_header").c_str(),
                                     detail::header_value(value, "add_header").c_str()),
                   "natsMsgHeader_Add");
  }

  void delete_header(std::string_view key) {
    if (!msg_) return;
    throw_on_error(natsMsgHeader_Delete(msg_.get(), detail::c_string(key, "key").c_str()), "natsMsgHeader_Delete");
  }

  [[nodiscard]] std::vector<std::string> header_values(std::string_view key) const {
    if (!msg_) return {};
    const char** values = nullptr;
    int count = 0;
    throw_on_error(natsMsgHeader_Values(msg_.get(), detail::c_string(key, "key").c_str(), &values, &count),
                   "natsMsgHeader_Values");
    // nats.c allocates the outer array; free it even if the loop throws.
    struct array_free {
      const char** p;
      ~array_free() { std::free(const_cast<char**>(p)); }
    } guard{values};

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
    // nats.c allocates the outer array; free it even if the loop throws.
    struct array_free {
      const char** p;
      ~array_free() { std::free(const_cast<char**>(p)); }
    } guard{keys};

    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      result.emplace_back(keys[i] != nullptr ? keys[i] : "");
    }
    return result;
  }

  [[nodiscard]] bool is_no_responders() const noexcept { return msg_ != nullptr && natsMsg_IsNoResponders(msg_.get()); }

  void ack() {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::ack: not initialized");
    throw_on_error(natsMsg_Ack(msg_.get(), nullptr), "natsMsg_Ack");
  }
  void ack_sync() {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::ack_sync: not initialized");
    throw_on_error(natsMsg_AckSync(msg_.get(), nullptr, nullptr), "natsMsg_AckSync");
  }
  void nak() {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::nak: not initialized");
    throw_on_error(natsMsg_Nak(msg_.get(), nullptr), "natsMsg_Nak");
  }
  void nak_with_delay(std::chrono::milliseconds delay) {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::nak_with_delay: not initialized");
    throw_on_error(natsMsg_NakWithDelay(msg_.get(), static_cast<int64_t>(delay.count()), nullptr),
                   "natsMsg_NakWithDelay");
  }
  void in_progress() {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::in_progress: not initialized");
    throw_on_error(natsMsg_InProgress(msg_.get(), nullptr), "natsMsg_InProgress");
  }
  void term() {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::term: not initialized");
    throw_on_error(natsMsg_Term(msg_.get(), nullptr), "natsMsg_Term");
  }

  [[nodiscard]] metadata get_metadata() const {
    if (!msg_) throw nats_error(NATS_INVALID_ARG, "message::get_metadata: not initialized");
    jsMsgMetaData* raw = nullptr;
    throw_on_error(natsMsg_GetMetaData(&raw, msg_.get()), "natsMsg_GetMetaData");
    std::unique_ptr<jsMsgMetaData, void (*)(jsMsgMetaData*)> holder(raw, jsMsgMetaData_Destroy);

    metadata out;
    out.sequence_stream = raw->Sequence.Stream;
    out.sequence_consumer = raw->Sequence.Consumer;
    out.num_delivered = raw->NumDelivered;
    out.num_pending = raw->NumPending;
    out.timestamp = raw->Timestamp;
    out.stream = raw->Stream != nullptr ? raw->Stream : "";
    out.consumer = raw->Consumer != nullptr ? raw->Consumer : "";
    out.domain = raw->Domain != nullptr ? raw->Domain : "";
    return out;
  }

  [[nodiscard]] uint64_t sequence() const noexcept { return msg_ != nullptr ? natsMsg_GetSequence(msg_.get()) : 0; }
  [[nodiscard]] int64_t timestamp() const noexcept { return msg_ != nullptr ? natsMsg_GetTime(msg_.get()) : 0; }

 private:
  std::unique_ptr<natsMsg, detail::msg_deleter> msg_;
};

namespace detail {

// A message built from a raw natsMsg skips message::create's checks, but its subjects still go into the
// protocol line when it is sent: check them first (see detail/validate.hpp).
inline void check_outgoing(const message& msg, const char* what) {
  if (!msg.valid()) throw nats_error(NATS_INVALID_ARG, std::string(what) + ": message is not initialized");
  const char* subj = natsMsg_GetSubject(msg.native_handle());
  (void)subject(subj != nullptr ? subj : "", what);
  if (const char* reply = natsMsg_GetReply(msg.native_handle()); reply != nullptr && *reply != '\0') {
    (void)subject(reply, what);
  }
}

}  // namespace detail

}  // namespace natscpp
