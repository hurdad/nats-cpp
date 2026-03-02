#pragma once

#include <nats/nats.h>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

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
};

/**
 * @brief Owns a natsConnection and provides modern C++ APIs.
 */
class connection {
 public:
  connection() = default;

  explicit connection(const connection_options& options) {
    connect(options);
  }

  void connect(const connection_options& options = {}) {
    natsConnection* raw{};
    throw_on_error(natsConnection_ConnectTo(&raw, options.url.c_str()), "natsConnection_ConnectTo");
    conn_.reset(raw);
  }

  [[nodiscard]] bool connected() const noexcept { return conn_ != nullptr; }
  [[nodiscard]] natsConnection* native_handle() const noexcept { return conn_.get(); }

  void publish(std::string_view subject, std::string_view payload) {
    throw_on_error(natsConnection_Publish(conn_.get(), std::string(subject).c_str(), payload.data(),
                                          static_cast<int>(payload.size())),
                   "natsConnection_Publish");
  }

  void flush(std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    throw_on_error(natsConnection_FlushTimeout(conn_.get(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_FlushTimeout");
  }

  [[nodiscard]] subscription subscribe_sync(std::string_view subject) {
    natsSubscription* raw{};
    throw_on_error(natsConnection_SubscribeSync(&raw, conn_.get(), std::string(subject).c_str()),
                   "natsConnection_SubscribeSync");
    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_queue_sync(std::string_view subject, std::string_view queue) {
    natsSubscription* raw{};
    throw_on_error(
        natsConnection_QueueSubscribeSync(&raw, conn_.get(), std::string(subject).c_str(), std::string(queue).c_str()),
        "natsConnection_QueueSubscribeSync");
    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_queue_async(std::string_view subject, std::string_view queue,
                                                  std::function<void(message)> handler) {
    natsSubscription* raw{};
    auto token = std::make_shared<std::function<void(message)>>(std::move(handler));

    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callbacks_[token.get()] = token;
    }

    natsStatus status = natsConnection_QueueSubscribe(
        &raw, conn_.get(), std::string(subject).c_str(), std::string(queue).c_str(),
        [](natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
          auto* fn = static_cast<std::function<void(message)>*>(closure);
          natsMsg* dup{};
          if (natsMsg_Create(&dup, natsMsg_GetSubject(msg), natsMsg_GetReply(msg), natsMsg_GetData(msg),
                             natsMsg_GetDataLength(msg)) == NATS_OK) {
            (*fn)(message{dup});
          }
        },
        token.get());

    if (status != NATS_OK) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callbacks_.erase(token.get());
      throw_on_error(status, "natsConnection_QueueSubscribe");
    }

    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_async(std::string_view subject, std::function<void(message)> handler) {
    natsSubscription* raw{};
    auto token = std::make_shared<std::function<void(message)>>(std::move(handler));

    {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callbacks_[token.get()] = token;
    }

    natsStatus status = natsConnection_Subscribe(
        &raw, conn_.get(), std::string(subject).c_str(),
        [](natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
          auto* fn = static_cast<std::function<void(message)>*>(closure);
          natsMsg* dup{};
          if (natsMsg_Create(&dup, natsMsg_GetSubject(msg), natsMsg_GetReply(msg), natsMsg_GetData(msg),
                             natsMsg_GetDataLength(msg)) == NATS_OK) {
            (*fn)(message{dup});
          }
        },
        token.get());

    if (status != NATS_OK) {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callbacks_.erase(token.get());
      throw_on_error(status, "natsConnection_Subscribe");
    }

    return subscription{raw};
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
    natsMsg* reply{};
    throw_on_error(natsConnection_Request(&reply, conn_.get(), std::string(subject).c_str(), payload.data(),
                                          static_cast<int>(payload.size()), static_cast<int64_t>(timeout.count())),
                   "natsConnection_Request");
    return message{reply};
  }

  [[nodiscard]] message request(std::string_view subject, std::string_view payload,
                                std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    return request_sync(subject, payload, timeout);
  }

  [[nodiscard]] std::future<message> request_async(
      std::string subject, std::string payload, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    return std::async(std::launch::async, [this, subject = std::move(subject), payload = std::move(payload), timeout]() {
      return request_sync(subject, payload, timeout);
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
    std::string value = reinterpret_cast<const char*>(inbox);
    natsInbox_Destroy(inbox);
    return value;
  }

 private:
  std::unique_ptr<natsConnection, detail::connection_deleter> conn_;
  std::unordered_map<void*, std::shared_ptr<std::function<void(message)>>> callbacks_;
  std::mutex callback_mutex_;
};

}  // namespace natscpp
