#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>

namespace natscpp {

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
  std::string stream_name;
  std::string durable_name;
};

/**
 * @brief High-level pull consumer wrapper.
 */
class js_pull_consumer {
 public:
  js_pull_consumer() = default;
  explicit js_pull_consumer(natsSubscription* sub) : sub_(sub) {}
  ~js_pull_consumer() { if (sub_ != nullptr) natsSubscription_Destroy(sub_); }

  js_pull_consumer(const js_pull_consumer&) = delete;
  js_pull_consumer& operator=(const js_pull_consumer&) = delete;

  js_pull_consumer(js_pull_consumer&& other) noexcept : sub_(other.sub_) { other.sub_ = nullptr; }
  js_pull_consumer& operator=(js_pull_consumer&& other) noexcept {
    if (this != &other) {
      if (sub_ != nullptr) natsSubscription_Destroy(sub_);
      sub_ = other.sub_;
      other.sub_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }

  [[nodiscard]] message next(std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
    natsMsg* msg{};
    throw_on_error(natsSubscription_NextMsg(&msg, sub_, static_cast<int64_t>(timeout.count())),
                   "natsSubscription_NextMsg");
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
  ~js_push_consumer() { if (sub_ != nullptr) natsSubscription_Destroy(sub_); }

  js_push_consumer(const js_push_consumer&) = delete;
  js_push_consumer& operator=(const js_push_consumer&) = delete;

  js_push_consumer(js_push_consumer&& other) noexcept : sub_(other.sub_) { other.sub_ = nullptr; }
  js_push_consumer& operator=(js_push_consumer&& other) noexcept {
    if (this != &other) {
      if (sub_ != nullptr) natsSubscription_Destroy(sub_);
      sub_ = other.sub_;
      other.sub_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return sub_ != nullptr; }

  [[nodiscard]] message next(std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
    natsMsg* msg{};
    throw_on_error(natsSubscription_NextMsg(&msg, sub_, static_cast<int64_t>(timeout.count())),
                   "natsSubscription_NextMsg");
    return message{msg};
  }

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
    throw_on_error(natsConnection_JetStream(&ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
  }

  ~jetstream() { if (ctx_ != nullptr) jsCtx_Destroy(ctx_); }

  jetstream(const jetstream&) = delete;
  jetstream& operator=(const jetstream&) = delete;

  jetstream(jetstream&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }
  jetstream& operator=(jetstream&& other) noexcept {
    if (this != &other) {
      if (ctx_ != nullptr) jsCtx_Destroy(ctx_);
      ctx_ = other.ctx_;
      other.ctx_ = nullptr;
    }
    return *this;
  }

  void publish(std::string_view subject, std::string_view payload, const js_publish_options& opts = {}) {
    jsPubOptions pub_opts{};
    if (!opts.msg_id.empty()) {
      pub_opts.MsgId = opts.msg_id.c_str();
    }
    if (!opts.expected_stream.empty()) {
      pub_opts.ExpectStream = opts.expected_stream.c_str();
    }

    jsPubAck* ack{};
    throw_on_error(js_Publish(&ack, ctx_, std::string(subject).c_str(), payload.data(),
                              static_cast<int>(payload.size()),
                              (pub_opts.MsgId != nullptr || pub_opts.ExpectStream != nullptr) ? &pub_opts : nullptr,
                              nullptr),
                   "js_Publish");
    if (ack != nullptr) {
      jsPubAck_Destroy(ack);
    }
  }

  [[nodiscard]] js_pull_consumer pull_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    natsSubscription* sub{};
    throw_on_error(js_PullSubscribe(&sub, ctx_, std::string(stream_subject).c_str(),
                                    std::string(durable_name).c_str(), nullptr, nullptr, nullptr),
                   "js_PullSubscribe");
    return js_pull_consumer{sub};
  }

  [[nodiscard]] js_push_consumer push_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    std::string durable_str(durable_name);
    jsSubOptions sub_opts{};
    if (!durable_str.empty()) {
      sub_opts.Config.Durable = durable_str.c_str();
    }

    natsSubscription* sub{};
    throw_on_error(js_SubscribeSync(&sub, ctx_, std::string(stream_subject).c_str(), nullptr,
                                    durable_str.empty() ? nullptr : &sub_opts, nullptr),
                   "js_SubscribeSync");
    return js_push_consumer{sub};
  }

  [[nodiscard]] js_push_consumer subscribe(std::string_view stream_subject, std::string_view durable_name) {
    return push_subscribe(stream_subject, durable_name);
  }

  [[nodiscard]] stream_info create_stream(const js_stream_config& config) {
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
    throw_on_error(js_AddStream(&stream_info_raw, ctx_, &stream_config, nullptr, nullptr), "js_AddStream");
    if (stream_info_raw != nullptr) {
      jsStreamInfo_Destroy(stream_info_raw);
    }

    return stream_info{.name = config.name};
  }

  [[nodiscard]] consumer_info create_consumer_group(const js_consumer_config& config) {
    return upsert_consumer_group(config, false);
  }

  [[nodiscard]] consumer_info update_consumer_group(const js_consumer_config& config) {
    return upsert_consumer_group(config, true);
  }

  [[nodiscard]] consumer_info get_consumer_group(std::string_view stream, std::string_view durable_name) {
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }
    if (durable_name.empty()) {
      throw std::invalid_argument("consumer durable name cannot be empty");
    }

    jsConsumerInfo* consumer_info_raw{};
    throw_on_error(js_GetConsumerInfo(&consumer_info_raw, ctx_, std::string(stream).c_str(),
                                      std::string(durable_name).c_str(), nullptr, nullptr),
                   "js_GetConsumerInfo");

    consumer_info out{};
    if (consumer_info_raw != nullptr) {
      out.stream_name = consumer_info_raw->Stream != nullptr ? consumer_info_raw->Stream : "";
      out.durable_name = consumer_info_raw->Name != nullptr ? consumer_info_raw->Name : "";
      jsConsumerInfo_Destroy(consumer_info_raw);
    }

    return out;
  }

  void delete_consumer_group(std::string_view stream, std::string_view durable_name) {
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }
    if (durable_name.empty()) {
      throw std::invalid_argument("consumer durable name cannot be empty");
    }

    throw_on_error(js_DeleteConsumer(ctx_, std::string(stream).c_str(), std::string(durable_name).c_str(), nullptr,
                                     nullptr),
                   "js_DeleteConsumer");
  }

  [[nodiscard]] std::vector<consumer_info> list_consumer_groups(std::string_view stream) {
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }

    jsConsumerInfoList* consumer_info_list_raw{};
    throw_on_error(js_Consumers(&consumer_info_list_raw, ctx_, std::string(stream).c_str(), nullptr, nullptr),
                   "js_Consumers");

    std::vector<consumer_info> out;
    if (consumer_info_list_raw != nullptr) {
      struct list_guard {
        jsConsumerInfoList* l;
        ~list_guard() { jsConsumerInfoList_Destroy(l); }
      } guard{consumer_info_list_raw};
      out.reserve(static_cast<std::size_t>(consumer_info_list_raw->Count));
      for (int i = 0; i < consumer_info_list_raw->Count; ++i) {
        const jsConsumerInfo* current = consumer_info_list_raw->List[i];
        if (current == nullptr) {
          continue;
        }
        out.push_back({
            .stream_name = current->Stream != nullptr ? current->Stream : "",
            .durable_name = current->Name != nullptr ? current->Name : "",
        });
      }
    }

    return out;
  }

  [[nodiscard]] std::vector<std::string> list_consumer_group_names(std::string_view stream) {
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }

    jsConsumerNamesList* consumer_names_list_raw{};
    throw_on_error(js_ConsumerNames(&consumer_names_list_raw, ctx_, std::string(stream).c_str(), nullptr, nullptr),
                   "js_ConsumerNames");

    std::vector<std::string> out;
    if (consumer_names_list_raw != nullptr) {
      struct list_guard {
        jsConsumerNamesList* l;
        ~list_guard() { jsConsumerNamesList_Destroy(l); }
      } guard{consumer_names_list_raw};
      out.reserve(static_cast<std::size_t>(consumer_names_list_raw->Count));
      for (int i = 0; i < consumer_names_list_raw->Count; ++i) {
        out.emplace_back(consumer_names_list_raw->List[i] != nullptr ? consumer_names_list_raw->List[i] : "");
      }
    }

    return out;
  }

 private:
  [[nodiscard]] consumer_info upsert_consumer_group(const js_consumer_config& config, bool update) {
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
    if (update) {
      throw_on_error(
          js_UpdateConsumer(&consumer_info_raw, ctx_, config.stream.c_str(), &consumer_config, nullptr, nullptr),
          "js_UpdateConsumer");
    } else {
      throw_on_error(js_AddConsumer(&consumer_info_raw, ctx_, config.stream.c_str(), &consumer_config, nullptr,
                                    nullptr),
                     "js_AddConsumer");
    }

    consumer_info out{
        .stream_name = config.stream,
        .durable_name = config.durable_name,
    };
    if (consumer_info_raw != nullptr) {
      out.stream_name = consumer_info_raw->Stream != nullptr ? consumer_info_raw->Stream : out.stream_name;
      out.durable_name = consumer_info_raw->Name != nullptr ? consumer_info_raw->Name : out.durable_name;
      jsConsumerInfo_Destroy(consumer_info_raw);
    }

    return out;
  }

  jsCtx* ctx_{};
};

}  // namespace natscpp
