#pragma once

#include <nats/nats.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <natscpp/detail/deleters.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>

namespace natscpp {

/**
 * @brief RAII wrapper for natsSubscription.
 */
class subscription {
 public:
  struct pending_limits {
    int messages = 0;
    int bytes = 0;
  };

  struct pending_state {
    int messages = 0;
    int bytes = 0;
  };

  struct consumer_info {
    std::string stream;
    std::string name;
  };

  struct consumer_sequence_mismatch {
    uint64_t stream_seq = 0;
    uint64_t consumer_client_seq = 0;
    uint64_t consumer_server_seq = 0;
  };

  struct stats {
    int pending_messages = 0;
    int pending_bytes = 0;
    int max_pending_messages = 0;
    int max_pending_bytes = 0;
    int64_t delivered_messages = 0;
    int64_t dropped_messages = 0;
  };

  subscription() = default;
  explicit subscription(natsSubscription* raw, std::function<void()> on_release = {})
      : sub_(raw), on_release_(std::move(on_release)) {}

  ~subscription() { release_callback(); }

  subscription(subscription&& other) noexcept
      : sub_(std::move(other.sub_)),
        on_release_(std::move(other.on_release_)),
        completion_callbacks_(std::move(other.completion_callbacks_)) {}

  subscription& operator=(subscription&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release_callback();
    sub_ = std::move(other.sub_);
    on_release_ = std::move(other.on_release_);
    completion_callbacks_ = std::move(other.completion_callbacks_);
    return *this;
  }

  subscription(const subscription&) = delete;
  subscription& operator=(const subscription&) = delete;

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }
  [[nodiscard]] bool is_valid() const noexcept { return sub_ != nullptr && natsSubscription_IsValid(sub_.get()); }
  [[nodiscard]] natsSubscription* native_handle() const noexcept { return sub_.get(); }
  [[nodiscard]] int64_t id() const noexcept { return sub_ != nullptr ? natsSubscription_GetID(sub_.get()) : -1; }

  [[nodiscard]] std::string subject() const {
    if (!sub_) return {};
    const char* value = natsSubscription_GetSubject(sub_.get());
    return value != nullptr ? std::string(value) : std::string{};
  }

  void unsubscribe() {
    if (!sub_) return;
    throw_on_error(natsSubscription_Unsubscribe(sub_.get()), "natsSubscription_Unsubscribe");
    sub_.reset();
    release_callback();
  }

  [[nodiscard]] message next_message(std::chrono::milliseconds timeout) {
    natsMsg* raw{};
    throw_on_error(natsSubscription_NextMsg(&raw, sub_.get(), static_cast<int64_t>(timeout.count())),
                   "natsSubscription_NextMsg");
    return message{raw};
  }

  void auto_unsubscribe(int max_messages) {
    throw_on_error(natsSubscription_AutoUnsubscribe(sub_.get(), max_messages), "natsSubscription_AutoUnsubscribe");
  }

  [[nodiscard]] uint64_t queued_messages() const {
    uint64_t queued = 0;
    throw_on_error(natsSubscription_QueuedMsgs(sub_.get(), &queued), "natsSubscription_QueuedMsgs");
    return queued;
  }

  void set_pending_limits(int msg_limit, int bytes_limit) {
    throw_on_error(natsSubscription_SetPendingLimits(sub_.get(), msg_limit, bytes_limit),
                   "natsSubscription_SetPendingLimits");
  }

  [[nodiscard]] pending_limits get_pending_limits() const {
    pending_limits result;
    throw_on_error(natsSubscription_GetPendingLimits(sub_.get(), &result.messages, &result.bytes),
                   "natsSubscription_GetPendingLimits");
    return result;
  }

  [[nodiscard]] pending_state pending() const {
    pending_state result;
    throw_on_error(natsSubscription_GetPending(sub_.get(), &result.messages, &result.bytes), "natsSubscription_GetPending");
    return result;
  }

  [[nodiscard]] int64_t delivered() const {
    int64_t value = 0;
    throw_on_error(natsSubscription_GetDelivered(sub_.get(), &value), "natsSubscription_GetDelivered");
    return value;
  }

  [[nodiscard]] int64_t dropped() const {
    int64_t value = 0;
    throw_on_error(natsSubscription_GetDropped(sub_.get(), &value), "natsSubscription_GetDropped");
    return value;
  }

  [[nodiscard]] pending_state max_pending() const {
    pending_state result;
    throw_on_error(natsSubscription_GetMaxPending(sub_.get(), &result.messages, &result.bytes),
                   "natsSubscription_GetMaxPending");
    return result;
  }

  void clear_max_pending() { throw_on_error(natsSubscription_ClearMaxPending(sub_.get()), "natsSubscription_ClearMaxPending"); }

  [[nodiscard]] stats get_stats() const {
    stats s;
    throw_on_error(natsSubscription_GetStats(sub_.get(), &s.pending_messages, &s.pending_bytes, &s.max_pending_messages,
                                             &s.max_pending_bytes, &s.delivered_messages, &s.dropped_messages),
                   "natsSubscription_GetStats");
    return s;
  }

  void no_delivery_delay() { throw_on_error(natsSubscription_NoDeliveryDelay(sub_.get()), "natsSubscription_NoDeliveryDelay"); }
  void drain() { throw_on_error(natsSubscription_Drain(sub_.get()), "natsSubscription_Drain"); }

  void drain(std::chrono::milliseconds timeout) {
    throw_on_error(natsSubscription_DrainTimeout(sub_.get(), static_cast<int64_t>(timeout.count())),
                   "natsSubscription_DrainTimeout");
  }

  void wait_for_drain_completion(std::chrono::milliseconds timeout) {
    throw_on_error(natsSubscription_WaitForDrainCompletion(sub_.get(), static_cast<int64_t>(timeout.count())),
                   "natsSubscription_WaitForDrainCompletion");
  }

  [[nodiscard]] natsStatus drain_completion_status() const noexcept {
    return sub_ != nullptr ? natsSubscription_DrainCompletionStatus(sub_.get()) : NATS_OK;
  }

  void set_on_complete_callback(std::function<void()> cb) {
    auto token = std::make_shared<std::function<void()>>(std::move(cb));
    {
      std::lock_guard<std::mutex> lock(completion_callbacks_mutex_);
      completion_callbacks_[token.get()] = token;
    }

    natsStatus status = natsSubscription_SetOnCompleteCB(
        sub_.get(),
        [](void* closure) {
          auto* fn = static_cast<std::function<void()>*>(closure);
          try {
            (*fn)();
          } catch (...) {
          }
        },
        token.get());

    if (status != NATS_OK) {
      std::lock_guard<std::mutex> lock(completion_callbacks_mutex_);
      completion_callbacks_.erase(token.get());
      throw_on_error(status, "natsSubscription_SetOnCompleteCB");
    }
  }

  [[nodiscard]] std::vector<message> fetch(int batch, std::chrono::milliseconds timeout) {
    natsMsgList list{};
    throw_on_error(natsSubscription_Fetch(&list, sub_.get(), batch, static_cast<int64_t>(timeout.count()), nullptr),
                   "natsSubscription_Fetch");

    std::vector<message> out;
    out.reserve(static_cast<std::size_t>(list.Count));
    for (int i = 0; i < list.Count; ++i) {
      out.emplace_back(list.Msgs[i]);
      list.Msgs[i] = nullptr;
    }
    natsMsgList_Destroy(&list);
    return out;
  }

  [[nodiscard]] std::vector<message> fetch_request(jsFetchRequest& request) {
    natsMsgList list{};
    throw_on_error(natsSubscription_FetchRequest(&list, sub_.get(), &request), "natsSubscription_FetchRequest");

    std::vector<message> out;
    out.reserve(static_cast<std::size_t>(list.Count));
    for (int i = 0; i < list.Count; ++i) {
      out.emplace_back(list.Msgs[i]);
      list.Msgs[i] = nullptr;
    }
    natsMsgList_Destroy(&list);
    return out;
  }

  [[nodiscard]] consumer_info get_consumer_info() const {
    jsConsumerInfo* raw = nullptr;
    throw_on_error(natsSubscription_GetConsumerInfo(&raw, sub_.get(), nullptr, nullptr),
                   "natsSubscription_GetConsumerInfo");
    std::unique_ptr<jsConsumerInfo, void (*)(jsConsumerInfo*)> holder(raw, jsConsumerInfo_Destroy);
    consumer_info out;
    out.stream = raw->Stream != nullptr ? raw->Stream : "";
    out.name = raw->Name != nullptr ? raw->Name : "";
    return out;
  }

  [[nodiscard]] std::optional<consumer_sequence_mismatch> get_sequence_mismatch() const {
    jsConsumerSequenceMismatch mismatch{};
    throw_on_error(natsSubscription_GetSequenceMismatch(&mismatch, sub_.get()), "natsSubscription_GetSequenceMismatch");
    if (!mismatch.ConsumerClient && !mismatch.Stream && !mismatch.ConsumerServer) {
      return std::nullopt;
    }
    return consumer_sequence_mismatch{mismatch.Stream, mismatch.ConsumerClient, mismatch.ConsumerServer};
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
  std::unordered_map<void*, std::shared_ptr<std::function<void()>>> completion_callbacks_;
  std::mutex completion_callbacks_mutex_;
};

}  // namespace natscpp
