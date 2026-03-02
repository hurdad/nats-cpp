#pragma once

#include <nats/nats.h>

#include <cassert>
#include <chrono>
#include <array>
#include <cstdio>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <natscpp/awaitable.hpp>
#include <natscpp/detail/deleters.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>
#include <natscpp/subscription.hpp>

namespace natscpp {

/**
 * @brief Connection options used to open a NATS connection.
 */
struct connection_options {
  std::string url{"nats://127.0.0.1:4222"};
  bool retry_on_failed_connect = false;
};

/**
 * @brief Owns a natsConnection and provides modern C++ APIs.
 */
class connection {
 public:
  struct statistics {
    uint64_t in_messages = 0;
    uint64_t in_bytes = 0;
    uint64_t out_messages = 0;
    uint64_t out_bytes = 0;
    uint64_t reconnects = 0;
  };

  connection() = default;

  explicit connection(const connection_options& options) {
    connect(options);
  }

  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  void connect(const connection_options& options = {}) {
    natsConnection* raw{};
    natsOptions* nopts = nullptr;
    throw_on_error(natsOptions_Create(&nopts), "natsOptions_Create");
    std::unique_ptr<natsOptions, void (*)(natsOptions*)> holder(nopts, natsOptions_Destroy);
    throw_on_error(natsOptions_SetURL(nopts, options.url.c_str()), "natsOptions_SetURL");
    if (options.retry_on_failed_connect) {
      throw_on_error(natsOptions_SetRetryOnFailedConnect(nopts, true, nullptr, nullptr),
                     "natsOptions_SetRetryOnFailedConnect");
    }
    throw_on_error(natsConnection_Connect(&raw, nopts), "natsConnection_Connect");
    conn_.reset(raw, detail::connection_deleter{});
  }

  [[nodiscard]] bool connected() const noexcept { return conn_ != nullptr; }
  [[nodiscard]] natsConnection* native_handle() const noexcept { return conn_.get(); }
  [[nodiscard]] bool is_closed() const noexcept { return conn_ == nullptr || natsConnection_IsClosed(conn_.get()); }
  [[nodiscard]] bool is_reconnecting() const noexcept {
    return conn_ != nullptr && natsConnection_IsReconnecting(conn_.get());
  }
  [[nodiscard]] bool is_draining() const noexcept { return conn_ != nullptr && natsConnection_IsDraining(conn_.get()); }
  [[nodiscard]] natsConnStatus status() const noexcept {
    return conn_ != nullptr ? natsConnection_Status(conn_.get()) : NATS_CONN_STATUS_CLOSED;
  }

  [[nodiscard]] uint64_t buffered_bytes() const noexcept {
    return conn_ != nullptr ? natsConnection_Buffered(conn_.get()) : 0;
  }

  void publish(std::string_view subject, std::string_view payload) {
    assert(payload.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "publish: payload exceeds INT_MAX bytes");
    throw_on_error(natsConnection_Publish(conn_.get(), std::string(subject).c_str(), payload.data(),
                                          static_cast<int>(payload.size())),
                   "natsConnection_Publish");
  }

  void publish_string(std::string_view subject, std::string_view payload) {
    throw_on_error(natsConnection_PublishString(conn_.get(), std::string(subject).c_str(), std::string(payload).c_str()),
                   "natsConnection_PublishString");
  }

  void publish_request(std::string_view subject, std::string_view reply_to, std::string_view payload) {
    throw_on_error(natsConnection_PublishRequest(conn_.get(), std::string(subject).c_str(), std::string(reply_to).c_str(),
                                                 payload.data(), static_cast<int>(payload.size())),
                   "natsConnection_PublishRequest");
  }

  void publish_request_string(std::string_view subject, std::string_view reply_to, std::string_view payload) {
    throw_on_error(natsConnection_PublishRequestString(conn_.get(), std::string(subject).c_str(),
                                                       std::string(reply_to).c_str(), std::string(payload).c_str()),
                   "natsConnection_PublishRequestString");
  }

  void publish(message msg) {
    natsMsg* raw = msg.release();
    natsStatus status = natsConnection_PublishMsg(conn_.get(), raw);
    if (status != NATS_OK) {
      natsMsg_Destroy(raw);
    }
    throw_on_error(status, "natsConnection_PublishMsg");
  }

  void flush(std::chrono::milliseconds timeout) {
    throw_on_error(natsConnection_FlushTimeout(conn_.get(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_FlushTimeout");
  }

  void flush() { throw_on_error(natsConnection_Flush(conn_.get()), "natsConnection_Flush"); }
  void close() { natsConnection_Close(conn_.get()); }
  [[nodiscard]] std::array<unsigned char, 64> sign(std::string_view message) {
    std::array<unsigned char, 64> sig{};
    throw_on_error(natsConnection_Sign(conn_.get(), reinterpret_cast<const unsigned char*>(message.data()),
                                       static_cast<int>(message.size()), sig.data()),
                   "natsConnection_Sign");
    return sig;
  }

  void process_read_event() { natsConnection_ProcessReadEvent(conn_.get()); }
  void process_write_event() { natsConnection_ProcessWriteEvent(conn_.get()); }

  void drain() { throw_on_error(natsConnection_Drain(conn_.get()), "natsConnection_Drain"); }
  void drain(std::chrono::milliseconds timeout) {
    throw_on_error(natsConnection_DrainTimeout(conn_.get(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_DrainTimeout");
  }

  [[nodiscard]] int64_t max_payload() const noexcept {
    return conn_ != nullptr ? natsConnection_GetMaxPayload(conn_.get()) : 0;
  }

  [[nodiscard]] statistics get_statistics() const {
    natsStatistics* raw{};
    throw_on_error(natsStatistics_Create(&raw), "natsStatistics_Create");
    std::unique_ptr<natsStatistics, void (*)(natsStatistics*)> holder(raw, natsStatistics_Destroy);
    throw_on_error(natsConnection_GetStats(conn_.get(), holder.get()), "natsConnection_GetStats");
    statistics stats;
    throw_on_error(natsStatistics_GetCounts(holder.get(), &stats.in_messages, &stats.in_bytes, &stats.out_messages,
                                            &stats.out_bytes, &stats.reconnects),
                   "natsStatistics_GetCounts");
    return stats;
  }

  // Buffer sized at 4096 to accommodate long URLs; natsConnection_GetConnectedUrl
  // uses snprintf and silently truncates without returning an error.
  [[nodiscard]] std::string connected_url() const {
    std::array<char, 4096> buf{};
    throw_on_error(natsConnection_GetConnectedUrl(conn_.get(), buf.data(), static_cast<int>(buf.size())),
                   "natsConnection_GetConnectedUrl");
    return std::string(buf.data());
  }

  [[nodiscard]] std::string connected_server_id() const {
    std::array<char, 4096> buf{};
    throw_on_error(natsConnection_GetConnectedServerId(conn_.get(), buf.data(), static_cast<int>(buf.size())),
                   "natsConnection_GetConnectedServerId");
    return std::string(buf.data());
  }

  [[nodiscard]] std::vector<std::string> servers() const { return fetch_server_list(&natsConnection_GetServers); }
  [[nodiscard]] std::vector<std::string> discovered_servers() const {
    return fetch_server_list(&natsConnection_GetDiscoveredServers);
  }

  [[nodiscard]] std::string last_error() const {
    const char* value = nullptr;
    throw_on_error(natsConnection_GetLastError(conn_.get(), &value), "natsConnection_GetLastError");
    return value != nullptr ? std::string(value) : std::string{};
  }

  [[nodiscard]] uint64_t client_id() const {
    uint64_t value = 0;
    throw_on_error(natsConnection_GetClientID(conn_.get(), &value), "natsConnection_GetClientID");
    return value;
  }

  [[nodiscard]] std::string client_ip() const {
    char* value = nullptr;
    throw_on_error(natsConnection_GetClientIP(conn_.get(), &value), "natsConnection_GetClientIP");
    std::string out = value != nullptr ? value : "";
    std::free(value);
    return out;
  }

  [[nodiscard]] std::chrono::microseconds rtt() const {
    int64_t value = 0;
    throw_on_error(natsConnection_GetRTT(conn_.get(), &value), "natsConnection_GetRTT");
    return std::chrono::microseconds(value);
  }

  [[nodiscard]] bool has_header_support() const noexcept {
    return conn_ != nullptr && natsConnection_HasHeaderSupport(conn_.get());
  }

  [[nodiscard]] std::pair<std::string, int> local_ip_and_port() const {
    char* ip = nullptr;
    int port = 0;
    throw_on_error(natsConnection_GetLocalIPAndPort(conn_.get(), &ip, &port), "natsConnection_GetLocalIPAndPort");
    std::pair<std::string, int> out{ip != nullptr ? ip : "", port};
    std::free(ip);
    return out;
  }

  [[nodiscard]] subscription subscribe_sync(std::string_view subject) {
    natsSubscription* raw{};
    throw_on_error(natsConnection_SubscribeSync(&raw, conn_.get(), std::string(subject).c_str()),
                   "natsConnection_SubscribeSync");
    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_async_timeout(std::string_view subject, std::chrono::milliseconds timeout,
                                                     std::function<void(message)> handler) {
    std::string subj(subject);
    return register_async_subscription(
        std::move(handler), "natsConnection_SubscribeTimeout",
        [this, subj, timeout](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_SubscribeTimeout(raw, conn_.get(), subj.c_str(),
                                                 static_cast<int64_t>(timeout.count()), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe_queue_sync(std::string_view subject, std::string_view queue) {
    natsSubscription* raw{};
    throw_on_error(
        natsConnection_QueueSubscribeSync(&raw, conn_.get(), std::string(subject).c_str(), std::string(queue).c_str()),
        "natsConnection_QueueSubscribeSync");
    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_queue_async_timeout(std::string_view subject, std::string_view queue,
                                                           std::chrono::milliseconds timeout,
                                                           std::function<void(message)> handler) {
    std::string subj(subject), q(queue);
    return register_async_subscription(
        std::move(handler), "natsConnection_QueueSubscribeTimeout",
        [this, subj, q, timeout](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_QueueSubscribeTimeout(raw, conn_.get(), subj.c_str(), q.c_str(),
                                                      static_cast<int64_t>(timeout.count()), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe_queue_async(std::string_view subject, std::string_view queue,
                                                   std::function<void(message)> handler) {
    std::string subj(subject), q(queue);
    return register_async_subscription(
        std::move(handler), "natsConnection_QueueSubscribe",
        [this, subj, q](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_QueueSubscribe(raw, conn_.get(), subj.c_str(), q.c_str(), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe_async(std::string_view subject, std::function<void(message)> handler) {
    std::string subj(subject);
    return register_async_subscription(
        std::move(handler), "natsConnection_Subscribe",
        [this, subj](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_Subscribe(raw, conn_.get(), subj.c_str(), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe(std::string_view subject, std::function<void(message)> handler) {
    return subscribe_async(subject, std::move(handler));
  }

  [[nodiscard]] subscription subscribe_queue(std::string_view subject, std::string_view queue,
                                             std::function<void(message)> handler) {
    return subscribe_queue_async(subject, queue, std::move(handler));
  }

  [[nodiscard]] message request_sync(std::string_view subject, std::string_view payload,
                                     std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    assert(payload.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "request_sync: payload exceeds INT_MAX bytes");
    natsMsg* reply{};
    throw_on_error(natsConnection_Request(&reply, conn_.get(), std::string(subject).c_str(), payload.data(),
                                          static_cast<int>(payload.size()), static_cast<int64_t>(timeout.count())),
                   "natsConnection_Request");
    return message{reply};
  }

  [[nodiscard]] message request_string(std::string_view subject, std::string_view payload,
                                       std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    natsMsg* reply{};
    throw_on_error(natsConnection_RequestString(&reply, conn_.get(), std::string(subject).c_str(),
                                                std::string(payload).c_str(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_RequestString");
    return message{reply};
  }

  [[nodiscard]] message request(message request_message, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    natsMsg* reply{};
    natsMsg* raw = request_message.release();
    natsStatus status = natsConnection_RequestMsg(&reply, conn_.get(), raw, static_cast<int64_t>(timeout.count()));
    if (status != NATS_OK) {
      natsMsg_Destroy(raw);
    }
    throw_on_error(status, "natsConnection_RequestMsg");
    return message{reply};
  }

  [[nodiscard]] message request(std::string_view subject, std::string_view payload,
                                std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    return request_sync(subject, payload, timeout);
  }

  [[nodiscard]] std::future<message> request_async(
      std::string subject, std::string payload, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    assert(payload.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "request_async: payload exceeds INT_MAX bytes");
    auto conn_ref = conn_;  // extend connection lifetime across the async call
    return std::async(std::launch::async,
                      [conn_ref, subject = std::move(subject), payload = std::move(payload), timeout]() {
                        natsMsg* reply{};
                        throw_on_error(
                            natsConnection_Request(&reply, conn_ref.get(), subject.c_str(), payload.data(),
                                                   static_cast<int>(payload.size()),
                                                   static_cast<int64_t>(timeout.count())),
                            "natsConnection_Request");
                        return message{reply};
                      });
  }

  [[nodiscard]] future_awaitable<message> request_awaitable(
      std::string subject, std::string payload, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    return future_awaitable<message>{request_async(std::move(subject), std::move(payload), timeout)};
  }

  /**
   * @brief Generates a NATS inbox subject.
   */
  [[nodiscard]] std::string new_inbox() const {
    natsInbox* inbox = nullptr;
    throw_on_error(natsInbox_Create(&inbox), "natsInbox_Create");
    // natsInbox is typedef char, so natsInbox* is char* — no cast needed.
    std::string value{inbox};
    natsInbox_Destroy(inbox);
    return value;
  }

 private:
  // Shared callback dispatched by all async subscriptions. Always null-guards msg
  // (natsConnection_SubscribeTimeout delivers a null msg to signal timeout expiry).
  static void async_msg_callback(natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
    if (msg == nullptr) { return; }
    auto* fn = static_cast<std::function<void(message)>*>(closure);
    natsMsg* dup{};
    if (natsMsg_Create(&dup, natsMsg_GetSubject(msg), natsMsg_GetReply(msg), natsMsg_GetData(msg),
                       natsMsg_GetDataLength(msg)) != NATS_OK) {
      std::fprintf(stderr, "[natscpp] natsMsg_Create failed: dropping message on subject '%s'\n",
                   natsMsg_GetSubject(msg));
      return;
    }
    // Copy all headers so JetStream metadata and user headers are available in the callback.
    const char** keys = nullptr;
    int key_count = 0;
    if (natsMsgHeader_Keys(msg, &keys, &key_count) == NATS_OK && keys != nullptr) {
      for (int i = 0; i < key_count; ++i) {
        const char** vals = nullptr;
        int val_count = 0;
        if (natsMsgHeader_Values(msg, keys[i], &vals, &val_count) == NATS_OK && vals != nullptr) {
          for (int j = 0; j < val_count; ++j) {
            if (vals[j] != nullptr) {
              natsMsgHeader_Add(dup, keys[i], vals[j]);
            }
          }
          std::free(const_cast<char**>(vals));
        }
      }
      std::free(const_cast<char**>(keys));
    }
    try {
      (*fn)(message{dup});
    } catch (...) {
      std::fprintf(stderr, "[natscpp] exception in message callback, ignoring\n");
    }
  }

  // Common registration logic shared by all four async subscribe variants.
  // do_subscribe(natsSubscription**, natsMsgHandler, void*) → natsStatus
  template <typename SubscribeFn>
  [[nodiscard]] subscription register_async_subscription(std::function<void(message)> handler,
                                                         const char* context_name,
                                                         SubscribeFn do_subscribe) {
    auto token = std::make_shared<std::function<void(message)>>(std::move(handler));
    auto state = callback_state_;

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->callbacks[token.get()] = token;
    }

    natsSubscription* raw{};
    natsStatus status = do_subscribe(&raw, &async_msg_callback, token.get());

    if (status != NATS_OK) {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->callbacks.erase(token.get());
      throw_on_error(status, context_name);
    }

    return subscription{raw, [state = std::weak_ptr<callback_state>(state), key = token.get()] {
      if (const auto locked = state.lock()) {
        std::lock_guard<std::mutex> lock(locked->mutex);
        locked->callbacks.erase(key);
      }
    }};
  }

  [[nodiscard]] std::vector<std::string> fetch_server_list(
      natsStatus (*getter)(natsConnection*, char***, int*)) const {
    char** values = nullptr;
    int count = 0;
    throw_on_error(getter(conn_.get(), &values, &count), "natsConnection_GetServers*");
    // RAII guard: free all strings and the array even if emplace_back throws.
    struct list_guard {
      char** arr; int n;
      ~list_guard() { for (int i = 0; i < n; ++i) std::free(arr[i]); std::free(arr); }
    } guard{values, count};

    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      out.emplace_back(values[i] != nullptr ? values[i] : "");
    }
    return out;
  }

  struct callback_state {
    std::unordered_map<void*, std::shared_ptr<std::function<void(message)>>> callbacks;
    std::mutex mutex;
  };

  std::shared_ptr<natsConnection> conn_;
  std::shared_ptr<callback_state> callback_state_ = std::make_shared<callback_state>();
};

}  // namespace natscpp
