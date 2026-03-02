#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace natscpp {

class jetstream_not_available : public std::runtime_error {
 public:
  jetstream_not_available() : std::runtime_error("JetStream symbols are not available in linked nats.c") {}
};

namespace detail {
inline void* resolve_symbol(const char* name) {
#if defined(_WIN32)
  auto module = GetModuleHandleA(nullptr);
  return reinterpret_cast<void*>(GetProcAddress(module, name));
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

inline void destroy_js_subscription(void* sub) {
  using destroy_fn = void (*)(void*);
  if (auto* fn = reinterpret_cast<destroy_fn>(resolve_symbol("natsSubscription_Destroy")); fn != nullptr && sub != nullptr) {
    fn(sub);
  }
}

inline void destroy_js_context(void* ctx) {
  using destroy_fn = void (*)(void*);
  if (auto* fn = reinterpret_cast<destroy_fn>(resolve_symbol("jsCtx_Destroy")); fn != nullptr && ctx != nullptr) {
    fn(ctx);
  }
}
}  // namespace detail

/**
 * @brief Options used for JetStream publish operations.
 */
struct js_publish_options {
  std::string msg_id{};
  std::string expected_stream{};
};

/**
 * @brief High-level pull consumer wrapper.
 */
class js_pull_consumer {
 public:
  js_pull_consumer() = default;
  explicit js_pull_consumer(void* sub) : sub_(sub) {}
  ~js_pull_consumer() { detail::destroy_js_subscription(sub_); }

  js_pull_consumer(const js_pull_consumer&) = delete;
  js_pull_consumer& operator=(const js_pull_consumer&) = delete;

  js_pull_consumer(js_pull_consumer&& other) noexcept : sub_(other.sub_) { other.sub_ = nullptr; }
  js_pull_consumer& operator=(js_pull_consumer&& other) noexcept {
    if (this != &other) {
      detail::destroy_js_subscription(sub_);
      sub_ = other.sub_;
      other.sub_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }

  [[nodiscard]] message next(std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
    using next_fn = natsStatus (*)(natsMsg**, void*, int64_t);
    auto* fn = reinterpret_cast<next_fn>(detail::resolve_symbol("natsSubscription_NextMsg"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    natsMsg* msg{};
    throw_on_error(fn(&msg, sub_, static_cast<int64_t>(timeout.count())), "natsSubscription_NextMsg");
    return message{msg};
  }

 private:
  void* sub_{};
};

/**
 * @brief High-level push consumer wrapper.
 */
class js_push_consumer {
 public:
  js_push_consumer() = default;
  explicit js_push_consumer(void* sub) : sub_(sub) {}
  ~js_push_consumer() { detail::destroy_js_subscription(sub_); }

  js_push_consumer(const js_push_consumer&) = delete;
  js_push_consumer& operator=(const js_push_consumer&) = delete;

  js_push_consumer(js_push_consumer&& other) noexcept : sub_(other.sub_) { other.sub_ = nullptr; }
  js_push_consumer& operator=(js_push_consumer&& other) noexcept {
    if (this != &other) {
      detail::destroy_js_subscription(sub_);
      sub_ = other.sub_;
      other.sub_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }

 private:
  void* sub_{};
};

/**
 * @brief JetStream context created from a core connection.
 */
class jetstream {
 public:
  jetstream() = default;

  explicit jetstream(connection& conn) {
    using create_fn = natsStatus (*)(void**, natsConnection*, void*);
    auto* fn = reinterpret_cast<create_fn>(detail::resolve_symbol("natsConnection_JetStream"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    void* context{};
    throw_on_error(fn(&context, conn.native_handle(), nullptr), "natsConnection_JetStream");
    ctx_ = context;
  }

  ~jetstream() {
    detail::destroy_js_context(ctx_);
  }

  jetstream(const jetstream&) = delete;
  jetstream& operator=(const jetstream&) = delete;

  jetstream(jetstream&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }
  jetstream& operator=(jetstream&& other) noexcept {
    if (this != &other) {
      detail::destroy_js_context(ctx_);
      ctx_ = other.ctx_;
      other.ctx_ = nullptr;
    }
    return *this;
  }

  void publish(std::string_view subject, std::string_view payload, const js_publish_options& = {}) {
    using publish_fn = natsStatus (*)(void**, void*, const char*, const void*, int, void*, void*);
    auto* fn = reinterpret_cast<publish_fn>(detail::resolve_symbol("js_Publish"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    void* ack{};
    throw_on_error(fn(&ack, ctx_, std::string(subject).c_str(), payload.data(), static_cast<int>(payload.size()), nullptr,
                      nullptr),
                   "js_Publish");

    using ack_destroy_fn = void (*)(void*);
    if (auto* destroy = reinterpret_cast<ack_destroy_fn>(detail::resolve_symbol("jsPubAck_Destroy"));
        destroy != nullptr && ack != nullptr) {
      destroy(ack);
    }
  }

  [[nodiscard]] js_pull_consumer pull_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    using pull_sub_fn = natsStatus (*)(void**, void*, const char*, const char*, void*, void*);
    auto* fn = reinterpret_cast<pull_sub_fn>(detail::resolve_symbol("js_PullSubscribe"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    void* sub{};
    throw_on_error(fn(&sub, ctx_, std::string(stream_subject).c_str(), std::string(durable_name).c_str(), nullptr, nullptr),
                   "js_PullSubscribe");
    return js_pull_consumer{sub};
  }

  [[nodiscard]] js_push_consumer subscribe(std::string_view stream_subject, std::string_view durable_name) {
    using sub_fn = natsStatus (*)(void**, void*, const char*, const char*, void*, void*);
    auto* fn = reinterpret_cast<sub_fn>(detail::resolve_symbol("js_Subscribe"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    void* sub{};
    throw_on_error(fn(&sub, ctx_, std::string(stream_subject).c_str(), std::string(durable_name).c_str(), nullptr, nullptr),
                   "js_Subscribe");
    return js_push_consumer{sub};
  }

 private:
  void* ctx_{};
};

/**
 * @brief High-level stream metadata.
 */
struct stream_info {
  std::string name;
};

/**
 * @brief High-level consumer metadata.
 */
struct consumer_info {
  std::string durable_name;
};

}  // namespace natscpp
