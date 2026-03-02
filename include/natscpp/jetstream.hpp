#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
  FARPROC proc = GetProcAddress(module, name);
  void* result = nullptr;
  static_assert(sizeof(FARPROC) == sizeof(void*), "FARPROC and void* must be the same size");
  std::memcpy(&result, &proc, sizeof(result));
  return result;
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

inline void destroy_js_subscription(natsSubscription* sub) {
  using destroy_fn = void (*)(natsSubscription*);
  if (auto* fn = reinterpret_cast<destroy_fn>(resolve_symbol("natsSubscription_Destroy")); fn != nullptr && sub != nullptr) {
    fn(sub);
  }
}

inline void destroy_js_context(jsCtx* ctx) {
  using destroy_fn = void (*)(jsCtx*);
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

enum class js_consumer_type {
  pull,
  push,
};

struct js_stream_config {
  std::string name;
  std::vector<std::string> subjects;
};

struct js_consumer_config {
  std::string stream;
  std::string durable_name;
  std::string filter_subject;
  std::string deliver_subject;
  std::string deliver_group;
  js_consumer_type type{js_consumer_type::pull};
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

/**
 * @brief High-level pull consumer wrapper.
 */
class js_pull_consumer {
 public:
  js_pull_consumer() = default;
  explicit js_pull_consumer(natsSubscription* sub) : sub_(sub) {}
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
    using next_fn = natsStatus (*)(natsMsg**, natsSubscription*, int64_t);
    auto* fn = reinterpret_cast<next_fn>(detail::resolve_symbol("natsSubscription_NextMsg"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    natsMsg* msg{};
    throw_on_error(fn(&msg, sub_, static_cast<int64_t>(timeout.count())), "natsSubscription_NextMsg");
    return message{msg};
  }

 private:
  natsSubscription* sub_{};
};

/**
 * @brief High-level push consumer wrapper.
 */
class js_push_consumer {
 public:
  js_push_consumer() = default;
  explicit js_push_consumer(natsSubscription* sub) : sub_(sub) {}
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
  natsSubscription* sub_{};
};

/**
 * @brief JetStream context created from a core connection.
 */
class jetstream {
 public:
  jetstream() = default;

  explicit jetstream(connection& conn) {
    using create_fn = natsStatus (*)(jsCtx**, natsConnection*, jsOptions*);
    auto* fn = reinterpret_cast<create_fn>(detail::resolve_symbol("natsConnection_JetStream"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    throw_on_error(fn(&ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
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
    using publish_fn = natsStatus (*)(jsPubAck**, jsCtx*, const char*, const void*, int, jsPubOptions*, jsErrCode*);
    auto* fn = reinterpret_cast<publish_fn>(detail::resolve_symbol("js_Publish"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    jsPubAck* ack{};
    throw_on_error(fn(&ack, ctx_, std::string(subject).c_str(), payload.data(), static_cast<int>(payload.size()), nullptr,
                      nullptr),
                   "js_Publish");

    using ack_destroy_fn = void (*)(jsPubAck*);
    if (auto* destroy = reinterpret_cast<ack_destroy_fn>(detail::resolve_symbol("jsPubAck_Destroy"));
        destroy != nullptr && ack != nullptr) {
      destroy(ack);
    }
  }

  [[nodiscard]] js_pull_consumer pull_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    using pull_sub_fn = natsStatus (*)(natsSubscription**, jsCtx*, const char*, const char*, jsOptions*, jsSubOptions*, jsErrCode*);
    auto* fn = reinterpret_cast<pull_sub_fn>(detail::resolve_symbol("js_PullSubscribe"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    natsSubscription* sub{};
    throw_on_error(fn(&sub, ctx_, std::string(stream_subject).c_str(), std::string(durable_name).c_str(), nullptr, nullptr, nullptr),
                   "js_PullSubscribe");
    return js_pull_consumer{sub};
  }

  [[nodiscard]] js_push_consumer push_subscribe(std::string_view stream_subject, std::string_view /*durable_name*/) {
    using sub_fn = natsStatus (*)(natsSubscription**, jsCtx*, const char*, jsOptions*, jsSubOptions*, jsErrCode*);
    auto* fn = reinterpret_cast<sub_fn>(detail::resolve_symbol("js_SubscribeSync"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    natsSubscription* sub{};
    throw_on_error(fn(&sub, ctx_, std::string(stream_subject).c_str(), nullptr, nullptr, nullptr),
                   "js_SubscribeSync");
    return js_push_consumer{sub};
  }

  [[nodiscard]] js_push_consumer subscribe(std::string_view stream_subject, std::string_view durable_name) {
    return push_subscribe(stream_subject, durable_name);
  }

  [[nodiscard]] stream_info create_stream(const js_stream_config& config) {
    using add_stream_fn = natsStatus (*)(jsStreamInfo**, jsCtx*, jsStreamConfig*, jsOptions*, jsErrCode*);
    auto* fn = reinterpret_cast<add_stream_fn>(detail::resolve_symbol("js_AddStream"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    if (config.name.empty()) {
      throw std::invalid_argument("stream name cannot be empty");
    }

    std::vector<const char*> subjects;
    subjects.reserve(config.subjects.size());
    for (const auto& subject : config.subjects) {
      subjects.push_back(subject.c_str());
    }

    jsStreamConfig stream_config{};
    stream_config.Name = config.name.c_str();
    stream_config.Subjects = subjects.empty() ? nullptr : subjects.data();
    stream_config.SubjectsLen = static_cast<int>(subjects.size());

    jsStreamInfo* stream_info_raw{};
    throw_on_error(fn(&stream_info_raw, ctx_, &stream_config, nullptr, nullptr), "js_AddStream");

    using stream_destroy_fn = void (*)(jsStreamInfo*);
    if (auto* destroy = reinterpret_cast<stream_destroy_fn>(detail::resolve_symbol("jsStreamInfo_Destroy"));
        destroy != nullptr && stream_info_raw != nullptr) {
      destroy(stream_info_raw);
    }

    return stream_info{.name = config.name};
  }

  [[nodiscard]] consumer_info create_consumer_group(const js_consumer_config& config) {
    using add_consumer_fn = natsStatus (*)(jsConsumerInfo**, jsCtx*, const char*, jsConsumerConfig*, jsOptions*, jsErrCode*);
    auto* fn = reinterpret_cast<add_consumer_fn>(detail::resolve_symbol("js_AddConsumer"));
    if (fn == nullptr) {
      throw jetstream_not_available();
    }

    if (config.stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }
    if (config.durable_name.empty()) {
      throw std::invalid_argument("consumer durable name cannot be empty");
    }
    if (config.type == js_consumer_type::push && config.deliver_subject.empty()) {
      throw std::invalid_argument("push consumer requires deliver_subject");
    }

    jsConsumerConfig consumer_config{};
    consumer_config.Durable = config.durable_name.c_str();
    consumer_config.FilterSubject = config.filter_subject.empty() ? nullptr : config.filter_subject.c_str();
    if (config.type == js_consumer_type::push) {
      consumer_config.DeliverSubject = config.deliver_subject.c_str();
      consumer_config.DeliverGroup = config.deliver_group.empty() ? nullptr : config.deliver_group.c_str();
    }

    jsConsumerInfo* consumer_info_raw{};
    throw_on_error(fn(&consumer_info_raw, ctx_, config.stream.c_str(), &consumer_config, nullptr, nullptr),
                   "js_AddConsumer");

    using consumer_destroy_fn = void (*)(jsConsumerInfo*);
    if (auto* destroy = reinterpret_cast<consumer_destroy_fn>(detail::resolve_symbol("jsConsumerInfo_Destroy"));
        destroy != nullptr && consumer_info_raw != nullptr) {
      destroy(consumer_info_raw);
    }

    return consumer_info{.durable_name = config.durable_name};
  }

 private:
  jsCtx* ctx_{};
};

}  // namespace natscpp
