#pragma once

#include <nats/nats.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/connection.hpp>
#include <natscpp/detail/validate.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>
#include <natscpp/subscription.hpp>

namespace natscpp {

// ---------------------------------------------------------------------------
// Policy enumerations — thin wrappers over the nats.c C enums.
// ---------------------------------------------------------------------------

enum class js_storage_type : int {
  file = js_FileStorage,
  memory = js_MemoryStorage,
};

enum class js_retention_policy : int {
  limits = js_LimitsPolicy,
  interest = js_InterestPolicy,
  work_queue = js_WorkQueuePolicy,
};

enum class js_discard_policy : int {
  old = js_DiscardOld,
  new_msgs = js_DiscardNew,
};

enum class js_deliver_policy : int {
  all = js_DeliverAll,
  last = js_DeliverLast,
  new_msgs = js_DeliverNew,
  by_start_seq = js_DeliverByStartSequence,
  by_start_time = js_DeliverByStartTime,
  last_per_subject = js_DeliverLastPerSubject,
};

enum class js_ack_policy : int {
  explicit_ = js_AckExplicit,
  none = js_AckNone,
  all = js_AckAll,
};

enum class js_replay_policy : int {
  instant = js_ReplayInstant,
  original = js_ReplayOriginal,
};

// ---------------------------------------------------------------------------
// Publish options
// ---------------------------------------------------------------------------

/**
 * @brief Options used for JetStream publish operations.
 */
struct js_publish_options {
  std::string msg_id;
  std::string expected_stream;
  std::string expected_last_msg_id;
  uint64_t expected_last_seq = 0;
  uint64_t expected_last_subject_seq = 0;
  bool expect_no_message = false;
  int64_t max_wait_ms = 0;  ///< 0 = use context default
  int64_t msg_ttl_ms = 0;   ///< 0 = no TTL (requires nats-server 2.11+)
};

/**
 * @brief Publish acknowledgment returned by js_Publish.
 */
struct js_pub_ack {
  std::string stream;
  uint64_t sequence = 0;
  std::string domain;
  bool duplicate = false;
};

// ---------------------------------------------------------------------------
// Stream configuration and info
// ---------------------------------------------------------------------------

enum class js_consumer_type {
  pull,
  push,
};

/**
 * @brief Configuration used to create or update a JetStream stream.
 */
struct js_stream_config {
  std::string name;
  std::vector<std::string> subjects;
  std::optional<std::string> description;
  js_storage_type storage = js_storage_type::file;
  js_retention_policy retention = js_retention_policy::limits;
  js_discard_policy discard = js_discard_policy::old;
  int64_t max_consumers = -1;
  int64_t max_msgs = -1;
  int64_t max_bytes = -1;
  int64_t max_age_ns = 0;  ///< nanoseconds; 0 = unlimited
  int64_t max_msgs_per_subject = -1;
  int32_t max_msg_size = -1;  ///< bytes; -1 = unlimited
  int64_t replicas = 1;
  bool no_ack = false;
  int64_t duplicates_ns = 0;  ///< dedup window in nanoseconds
  bool allow_direct = false;
  bool deny_delete = false;
  bool deny_purge = false;
};

/**
 * @brief Stream metadata returned by stream management operations.
 */
struct stream_info {
  std::string name;
  std::string description;
  uint64_t messages = 0;
  uint64_t bytes = 0;
  uint64_t first_seq = 0;
  uint64_t last_seq = 0;
  int64_t consumers = 0;
};

// ---------------------------------------------------------------------------
// Consumer configuration and info
// ---------------------------------------------------------------------------

/**
 * @brief Configuration used to create or update a JetStream consumer.
 */
struct js_consumer_config {
  std::string stream;
  std::string durable_name;
  std::string filter_subject;
  std::string deliver_subject;  ///< push consumers
  std::string deliver_group;    ///< push consumers (queue group)
  js_consumer_type type = js_consumer_type::pull;
  js_deliver_policy deliver_policy = js_deliver_policy::all;
  js_ack_policy ack_policy = js_ack_policy::explicit_;
  js_replay_policy replay_policy = js_replay_policy::instant;
  int64_t ack_wait_ns = 0;            ///< nanoseconds; 0 = server default
  int64_t max_deliver = -1;           ///< -1 = unlimited
  int64_t max_ack_pending = -1;       ///< -1 = server default
  int64_t max_waiting = -1;           ///< pull consumers; -1 = server default
  int64_t inactive_threshold_ns = 0;  ///< ephemeral consumer inactivity; 0 = server default
  int64_t replicas = 0;               ///< 0 = inherit from stream
  bool headers_only = false;
  uint64_t opt_start_seq = 0;                ///< for deliver_policy::by_start_seq
  int64_t opt_start_time_ns = 0;             ///< for deliver_policy::by_start_time (UTC nanoseconds)
  std::vector<std::string> filter_subjects;  ///< multi-filter alternative to filter_subject
  std::optional<std::string> description;
};

/**
 * @brief High-level consumer metadata.
 */
struct consumer_info {
  std::string stream_name;
  std::string durable_name;
};

// ---------------------------------------------------------------------------
// Account info
// ---------------------------------------------------------------------------

struct js_account_info {
  uint64_t memory = 0;
  uint64_t store = 0;
  int64_t streams = 0;
  int64_t consumers = 0;
};

// ---------------------------------------------------------------------------
// Consumer wrappers
// ---------------------------------------------------------------------------

/**
 * @brief High-level pull consumer wrapper.
 */
class js_pull_consumer {
 public:
  js_pull_consumer() = default;
  explicit js_pull_consumer(natsSubscription* sub) : sub_(sub) {}
  ~js_pull_consumer() {
    if (sub_ != nullptr) natsSubscription_Destroy(sub_);
  }

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
  [[nodiscard]] natsSubscription* native_handle() const noexcept { return sub_; }

  /**
   * @brief Fetches up to batch messages in one pull request, waiting at most timeout for the first.
   *
   * Returns as soon as at least one message has arrived and no more are immediately available; throws
   * NATS_TIMEOUT when none arrive in time.
   */
  [[nodiscard]] std::vector<message> fetch(int batch, std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
    if (sub_ == nullptr) throw nats_error(NATS_INVALID_ARG, "js_pull_consumer: not initialized");
    if (batch < 1) throw std::invalid_argument("js_pull_consumer::fetch: batch must be at least 1");
    natsMsgList list{};
    throw_on_error(natsSubscription_Fetch(&list, sub_, batch, static_cast<int64_t>(timeout.count()), nullptr),
                   "natsSubscription_Fetch");
    struct list_guard {
      natsMsgList& l;
      ~list_guard() { natsMsgList_Destroy(&l); }
    } guard{list};
    std::vector<message> out;
    out.reserve(static_cast<std::size_t>(list.Count));
    for (int i = 0; i < list.Count; ++i) {
      out.emplace_back(list.Msgs[i]);
      list.Msgs[i] = nullptr;
    }
    if (out.empty()) {
      throw nats_error(NATS_TIMEOUT, "natsSubscription_Fetch returned no messages");
    }
    return out;
  }

  /**
   * @brief Fetches a single message (one pull request per call; use fetch() for throughput).
   */
  [[nodiscard]] message next(std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
    return std::move(fetch(1, timeout).front());
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
  ~js_push_consumer() {
    if (sub_ != nullptr) natsSubscription_Destroy(sub_);
  }

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
  [[nodiscard]] natsSubscription* native_handle() const noexcept { return sub_; }

  [[nodiscard]] message next(std::chrono::milliseconds timeout = std::chrono::seconds(1)) {
    if (sub_ == nullptr) throw nats_error(NATS_INVALID_ARG, "js_push_consumer: not initialized");
    natsMsg* msg{};
    throw_on_error(natsSubscription_NextMsg(&msg, sub_, static_cast<int64_t>(timeout.count())),
                   "natsSubscription_NextMsg");
    return message{msg};
  }

 private:
  natsSubscription* sub_{};
};

// ---------------------------------------------------------------------------
// JetStream context
// ---------------------------------------------------------------------------

/**
 * @brief JetStream context created from a core connection.
 */
class jetstream {
 public:
  jetstream() = default;

  explicit jetstream(connection& conn) {
    throw_on_error(natsConnection_JetStream(&ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
  }

  ~jetstream() {
    if (ctx_ != nullptr) jsCtx_Destroy(ctx_);
  }

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

  [[nodiscard]] bool valid() const noexcept { return ctx_ != nullptr; }

  // -------------------------------------------------------------------------
  // Publish
  // -------------------------------------------------------------------------

  /**
   * @brief Publish to a JetStream stream and return the server acknowledgment.
   */
  [[nodiscard]] js_pub_ack publish(std::string_view subject, std::string_view payload,
                                   const js_publish_options& opts = {}) {
    check_valid();
    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw std::invalid_argument("jetstream::publish: payload exceeds INT_MAX bytes");
    jsPubOptions pub_opts{};
    const bool has_opts = to_native_pub_options(opts, pub_opts);
    jsPubAck* ack{};
    throw_on_error(js_Publish(&ack, ctx_, detail::subject(subject, "jetstream::publish").c_str(), payload.data(),
                              static_cast<int>(payload.size()), has_opts ? &pub_opts : nullptr, nullptr),
                   "js_Publish");
    return extract_pub_ack(ack);
  }

  /**
   * @brief Publish a pre-built message (with headers) to a JetStream stream.
   */
  [[nodiscard]] js_pub_ack publish_msg(message msg, const js_publish_options& opts = {}) {
    check_valid();
    jsPubOptions pub_opts{};
    const bool has_opts = to_native_pub_options(opts, pub_opts);
    detail::check_outgoing(msg, "jetstream::publish_msg");
    jsPubAck* ack{};
    natsMsg* raw = msg.release();
    natsStatus status = js_PublishMsg(&ack, ctx_, raw, has_opts ? &pub_opts : nullptr, nullptr);
    natsMsg_Destroy(raw);  // js_PublishMsg never takes ownership
    throw_on_error(status, "js_PublishMsg");
    return extract_pub_ack(ack);
  }

  // -------------------------------------------------------------------------
  // Subscribe
  // -------------------------------------------------------------------------

  [[nodiscard]] js_pull_consumer pull_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    check_valid();
    natsSubscription* sub{};
    throw_on_error(js_PullSubscribe(&sub, ctx_, detail::config_subject(stream_subject, "pull_subscribe").c_str(),
                                    detail::name(durable_name, "pull_subscribe durable_name", true).c_str(), nullptr,
                                    nullptr, nullptr),
                   "js_PullSubscribe");
    return js_pull_consumer{sub};
  }

  [[nodiscard]] js_push_consumer push_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    check_valid();
    const std::string subject_str = detail::config_subject(stream_subject, "push_subscribe");
    std::string durable_str = detail::name(durable_name, "push_subscribe durable_name", true);
    jsSubOptions sub_opts{};
    throw_on_error(jsSubOptions_Init(&sub_opts), "jsSubOptions_Init");
    if (!durable_str.empty()) {
      sub_opts.Config.Durable = durable_str.c_str();
    }

    natsSubscription* sub{};
    throw_on_error(
        js_SubscribeSync(&sub, ctx_, subject_str.c_str(), nullptr, durable_str.empty() ? nullptr : &sub_opts, nullptr),
        "js_SubscribeSync");
    return js_push_consumer{sub};
  }

  [[nodiscard]] js_push_consumer subscribe(std::string_view stream_subject, std::string_view durable_name) {
    return push_subscribe(stream_subject, durable_name);
  }

  /**
   * @brief Callback-based push subscription: handler runs on a nats.c delivery thread for each message.
   *
   * durable_name may be empty for an ephemeral consumer. Unless manual_ack is true, nats.c acknowledges each
   * message after the handler returns; with manual_ack the handler must call ack()/nak()/term() itself.
   * The returned subscription has the same lifetime guarantees as connection::subscribe_async.
   */
  [[nodiscard]] subscription subscribe_async(std::string_view stream_subject, std::string_view durable_name,
                                             std::function<void(message)> handler, bool manual_ack = false) {
    check_valid();
    std::string subject_str = detail::config_subject(stream_subject, "jetstream::subscribe_async");
    std::string durable_str = detail::name(durable_name, "jetstream::subscribe_async durable_name", true);
    return detail::make_async_subscription(
        std::move(handler), "js_Subscribe",
        [this, &subject_str, &durable_str, manual_ack](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          jsSubOptions sub_opts{};
          natsStatus s = jsSubOptions_Init(&sub_opts);
          if (s != NATS_OK) return s;
          if (!durable_str.empty()) sub_opts.Config.Durable = durable_str.c_str();
          sub_opts.ManualAck = manual_ack;
          return js_Subscribe(raw, ctx_, subject_str.c_str(), cb, closure, nullptr, &sub_opts, nullptr);
        });
  }

  // -------------------------------------------------------------------------
  // Stream management
  // -------------------------------------------------------------------------

  [[nodiscard]] stream_info create_stream(const js_stream_config& config) {
    check_valid();
    if (config.name.empty()) {
      throw std::invalid_argument("stream name cannot be empty");
    }
    native_stream_config native(config);
    jsStreamInfo* si_raw{};
    throw_on_error(js_AddStream(&si_raw, ctx_, &native.cfg, nullptr, nullptr), "js_AddStream");
    stream_info out;
    if (si_raw != nullptr) {
      std::unique_ptr<jsStreamInfo, void (*)(jsStreamInfo*)> holder(si_raw, jsStreamInfo_Destroy);
      out = extract_stream_info(si_raw);
    } else {
      out.name = config.name;
    }
    return out;
  }

  [[nodiscard]] stream_info update_stream(const js_stream_config& config) {
    check_valid();
    if (config.name.empty()) {
      throw std::invalid_argument("stream name cannot be empty");
    }
    native_stream_config native(config);
    jsStreamInfo* si_raw{};
    throw_on_error(js_UpdateStream(&si_raw, ctx_, &native.cfg, nullptr, nullptr), "js_UpdateStream");
    stream_info out;
    if (si_raw != nullptr) {
      std::unique_ptr<jsStreamInfo, void (*)(jsStreamInfo*)> holder(si_raw, jsStreamInfo_Destroy);
      out = extract_stream_info(si_raw);
    } else {
      out.name = config.name;
    }
    return out;
  }

  void delete_stream(std::string_view name) {
    check_valid();
    if (name.empty()) throw std::invalid_argument("stream name cannot be empty");
    throw_on_error(js_DeleteStream(ctx_, detail::name(name, "stream name").c_str(), nullptr, nullptr),
                   "js_DeleteStream");
  }

  void purge_stream(std::string_view name) {
    check_valid();
    if (name.empty()) throw std::invalid_argument("stream name cannot be empty");
    throw_on_error(js_PurgeStream(ctx_, detail::name(name, "stream name").c_str(), nullptr, nullptr), "js_PurgeStream");
  }

  [[nodiscard]] stream_info get_stream_info(std::string_view name) {
    check_valid();
    if (name.empty()) throw std::invalid_argument("stream name cannot be empty");
    jsStreamInfo* si_raw{};
    throw_on_error(js_GetStreamInfo(&si_raw, ctx_, detail::name(name, "stream name").c_str(), nullptr, nullptr),
                   "js_GetStreamInfo");
    stream_info out;
    if (si_raw != nullptr) {
      std::unique_ptr<jsStreamInfo, void (*)(jsStreamInfo*)> holder(si_raw, jsStreamInfo_Destroy);
      out = extract_stream_info(si_raw);
    }
    return out;
  }

  [[nodiscard]] std::vector<stream_info> list_streams() {
    check_valid();
    jsStreamInfoList* list_raw{};
    throw_on_error(js_Streams(&list_raw, ctx_, nullptr, nullptr), "js_Streams");
    std::vector<stream_info> out;
    if (list_raw != nullptr) {
      struct list_guard {
        jsStreamInfoList* l;
        ~list_guard() { jsStreamInfoList_Destroy(l); }
      } guard{list_raw};
      out.reserve(static_cast<std::size_t>(list_raw->Count));
      for (int i = 0; i < list_raw->Count; ++i) {
        if (list_raw->List[i] != nullptr) {
          out.push_back(extract_stream_info(list_raw->List[i]));
        }
      }
    }
    return out;
  }

  [[nodiscard]] std::vector<std::string> list_stream_names() {
    check_valid();
    jsStreamNamesList* list_raw{};
    throw_on_error(js_StreamNames(&list_raw, ctx_, nullptr, nullptr), "js_StreamNames");
    std::vector<std::string> out;
    if (list_raw != nullptr) {
      struct list_guard {
        jsStreamNamesList* l;
        ~list_guard() { jsStreamNamesList_Destroy(l); }
      } guard{list_raw};
      out.reserve(static_cast<std::size_t>(list_raw->Count));
      for (int i = 0; i < list_raw->Count; ++i) {
        out.emplace_back(list_raw->List[i] != nullptr ? list_raw->List[i] : "");
      }
    }
    return out;
  }

  // -------------------------------------------------------------------------
  // Stream message access
  // -------------------------------------------------------------------------

  /**
   * @brief Fetch a specific message from a stream by sequence number.
   */
  [[nodiscard]] message get_msg(std::string_view stream, uint64_t seq) {
    check_valid();
    natsMsg* msg{};
    throw_on_error(js_GetMsg(&msg, ctx_, detail::name(stream, "stream").c_str(), seq, nullptr, nullptr), "js_GetMsg");
    return message{msg};
  }

  /**
   * @brief Fetch the last message published to a specific subject in a stream.
   */
  [[nodiscard]] message get_last_msg(std::string_view stream, std::string_view subject) {
    check_valid();
    natsMsg* msg{};
    throw_on_error(js_GetLastMsg(&msg, ctx_, detail::name(stream, "stream").c_str(),
                                 detail::config_subject(subject, "get_last_msg subject").c_str(), nullptr, nullptr),
                   "js_GetLastMsg");
    return message{msg};
  }

  /**
   * @brief Delete a message from a stream by sequence number.
   */
  void delete_msg(std::string_view stream, uint64_t seq) {
    check_valid();
    throw_on_error(js_DeleteMsg(ctx_, detail::name(stream, "stream").c_str(), seq, nullptr, nullptr), "js_DeleteMsg");
  }

  /**
   * @brief Securely erase a message from a stream by sequence number.
   */
  void erase_msg(std::string_view stream, uint64_t seq) {
    check_valid();
    throw_on_error(js_EraseMsg(ctx_, detail::name(stream, "stream").c_str(), seq, nullptr, nullptr), "js_EraseMsg");
  }

  // -------------------------------------------------------------------------
  // Consumer management
  // -------------------------------------------------------------------------

  [[nodiscard]] consumer_info create_consumer_group(const js_consumer_config& config) {
    return upsert_consumer_group(config, false);
  }

  [[nodiscard]] consumer_info update_consumer_group(const js_consumer_config& config) {
    return upsert_consumer_group(config, true);
  }

  [[nodiscard]] consumer_info get_consumer_group(std::string_view stream, std::string_view durable_name) {
    check_valid();
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }
    if (durable_name.empty()) {
      throw std::invalid_argument("consumer durable name cannot be empty");
    }

    jsConsumerInfo* consumer_info_raw{};
    throw_on_error(js_GetConsumerInfo(&consumer_info_raw, ctx_, detail::name(stream, "stream").c_str(),
                                      detail::name(durable_name, "durable_name").c_str(), nullptr, nullptr),
                   "js_GetConsumerInfo");

    consumer_info out{};
    if (consumer_info_raw != nullptr) {
      std::unique_ptr<jsConsumerInfo, void (*)(jsConsumerInfo*)> holder(consumer_info_raw, jsConsumerInfo_Destroy);
      out.stream_name = consumer_info_raw->Stream != nullptr ? consumer_info_raw->Stream : "";
      out.durable_name = consumer_info_raw->Name != nullptr ? consumer_info_raw->Name : "";
    }

    return out;
  }

  void delete_consumer_group(std::string_view stream, std::string_view durable_name) {
    check_valid();
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }
    if (durable_name.empty()) {
      throw std::invalid_argument("consumer durable name cannot be empty");
    }

    throw_on_error(js_DeleteConsumer(ctx_, detail::name(stream, "stream").c_str(),
                                     detail::name(durable_name, "durable_name").c_str(), nullptr, nullptr),
                   "js_DeleteConsumer");
  }

  [[nodiscard]] std::vector<consumer_info> list_consumer_groups(std::string_view stream) {
    check_valid();
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }

    jsConsumerInfoList* consumer_info_list_raw{};
    throw_on_error(
        js_Consumers(&consumer_info_list_raw, ctx_, detail::name(stream, "stream").c_str(), nullptr, nullptr),
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
    check_valid();
    if (stream.empty()) {
      throw std::invalid_argument("consumer stream cannot be empty");
    }

    jsConsumerNamesList* consumer_names_list_raw{};
    throw_on_error(
        js_ConsumerNames(&consumer_names_list_raw, ctx_, detail::name(stream, "stream").c_str(), nullptr, nullptr),
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

  // -------------------------------------------------------------------------
  // Account info
  // -------------------------------------------------------------------------

  [[nodiscard]] js_account_info get_account_info() {
    check_valid();
    jsAccountInfo* raw{};
    throw_on_error(js_GetAccountInfo(&raw, ctx_, nullptr, nullptr), "js_GetAccountInfo");
    std::unique_ptr<jsAccountInfo, void (*)(jsAccountInfo*)> holder(raw, jsAccountInfo_Destroy);
    js_account_info out;
    if (raw != nullptr) {
      out.memory = raw->Memory;
      out.store = raw->Store;
      out.streams = raw->Streams;
      out.consumers = raw->Consumers;
    }
    return out;
  }

 private:
  void check_valid() const {
    if (ctx_ == nullptr) throw nats_error(NATS_INVALID_ARG, "jetstream: not initialized");
  }

  // Fills native from opts; returns whether any option is set. The strings stay owned by opts.
  static bool to_native_pub_options(const js_publish_options& opts, jsPubOptions& native) {
    // Sent as header values (Nats-Msg-Id, Nats-Expected-Stream, ...): no CR, LF or NUL.
    (void)detail::header_value(opts.msg_id, "msg_id");
    (void)detail::header_value(opts.expected_stream, "expected_stream");
    (void)detail::header_value(opts.expected_last_msg_id, "expected_last_msg_id");
    throw_on_error(jsPubOptions_Init(&native), "jsPubOptions_Init");
    bool has_opts = false;
    if (!opts.msg_id.empty()) {
      native.MsgId = opts.msg_id.c_str();
      has_opts = true;
    }
    if (!opts.expected_stream.empty()) {
      native.ExpectStream = opts.expected_stream.c_str();
      has_opts = true;
    }
    if (!opts.expected_last_msg_id.empty()) {
      native.ExpectLastMsgId = opts.expected_last_msg_id.c_str();
      has_opts = true;
    }
    if (opts.expected_last_seq > 0) {
      native.ExpectLastSeq = opts.expected_last_seq;
      has_opts = true;
    }
    if (opts.expected_last_subject_seq > 0) {
      native.ExpectLastSubjectSeq = opts.expected_last_subject_seq;
      has_opts = true;
    }
    if (opts.expect_no_message) {
      native.ExpectNoMessage = true;
      has_opts = true;
    }
    if (opts.max_wait_ms > 0) {
      native.MaxWait = opts.max_wait_ms;
      has_opts = true;
    }
    if (opts.msg_ttl_ms > 0) {
      native.MsgTTL = opts.msg_ttl_ms;
      has_opts = true;
    }
    return has_opts;
  }

  // Extracts a stream_info from a raw jsStreamInfo pointer; a null pointer yields an empty stream_info.
  static stream_info extract_stream_info(const jsStreamInfo* si) {
    stream_info out;
    if (si == nullptr) return out;
    if (si->Config != nullptr) {
      out.name = si->Config->Name != nullptr ? si->Config->Name : "";
      out.description = si->Config->Description != nullptr ? si->Config->Description : "";
    }
    out.messages = si->State.Msgs;
    out.bytes = si->State.Bytes;
    out.first_seq = si->State.FirstSeq;
    out.last_seq = si->State.LastSeq;
    out.consumers = si->State.Consumers;
    return out;
  }

  // Extracts and destroys a jsPubAck pointer.
  static js_pub_ack extract_pub_ack(jsPubAck* raw) {
    js_pub_ack out;
    if (raw != nullptr) {
      std::unique_ptr<jsPubAck, void (*)(jsPubAck*)> holder(raw, jsPubAck_Destroy);
      out.stream = raw->Stream != nullptr ? raw->Stream : "";
      out.sequence = raw->Sequence;
      out.domain = raw->Domain != nullptr ? raw->Domain : "";
      out.duplicate = raw->Duplicate;
    }
    return out;
  }

  // A jsStreamConfig and the checked strings it points into (nats.c writes them into JSON unescaped).
  struct native_stream_config {
    std::string name;
    std::string description;
    std::vector<std::string> subject_storage;
    std::vector<const char*> subjects;
    jsStreamConfig cfg{};

    explicit native_stream_config(const js_stream_config& config)
        : name(detail::name(config.name, "stream name")),
          description(config.description ? detail::json_text(*config.description, "stream description") : "") {
      subject_storage.reserve(config.subjects.size());
      for (const auto& subj : config.subjects) {
        subject_storage.push_back(detail::config_subject(subj, "stream subject"));
      }
      subjects.reserve(subject_storage.size());
      for (const auto& subj : subject_storage) subjects.push_back(subj.c_str());

      throw_on_error(jsStreamConfig_Init(&cfg), "jsStreamConfig_Init");  // keeps nats.c defaults for unset fields
      cfg.Name = name.c_str();
      cfg.Subjects = subjects.empty() ? nullptr : subjects.data();
      cfg.SubjectsLen = static_cast<int>(subjects.size());
      cfg.Description = config.description ? description.c_str() : nullptr;
      cfg.Storage = static_cast<jsStorageType>(config.storage);
      cfg.Retention = static_cast<jsRetentionPolicy>(config.retention);
      cfg.Discard = static_cast<jsDiscardPolicy>(config.discard);
      cfg.MaxConsumers = config.max_consumers;
      cfg.MaxMsgs = config.max_msgs;
      cfg.MaxBytes = config.max_bytes;
      cfg.MaxAge = config.max_age_ns;
      cfg.MaxMsgsPerSubject = config.max_msgs_per_subject;
      cfg.MaxMsgSize = config.max_msg_size;
      cfg.Replicas = config.replicas;
      cfg.NoAck = config.no_ack;
      cfg.Duplicates = config.duplicates_ns;
      cfg.AllowDirect = config.allow_direct;
      cfg.DenyDelete = config.deny_delete;
      cfg.DenyPurge = config.deny_purge;
    }
    native_stream_config(const native_stream_config&) = delete;  // cfg points into the members
    native_stream_config& operator=(const native_stream_config&) = delete;
    native_stream_config(native_stream_config&&) = delete;
    native_stream_config& operator=(native_stream_config&&) = delete;
    ~native_stream_config() = default;
  };

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
    if (!config.filter_subject.empty() && !config.filter_subjects.empty()) {
      throw std::invalid_argument("set filter_subject or filter_subjects, not both");
    }
    check_valid();  // after the argument checks, which need no server

    // nats.c puts the stream and durable names into the API subject and these strings into JSON unescaped.
    const std::string stream = detail::name(config.stream, "consumer stream");
    const std::string durable = detail::name(config.durable_name, "consumer durable_name");
    const std::string description =
        config.description ? detail::json_text(*config.description, "consumer description") : std::string{};
    const std::string filter_subject =
        config.filter_subject.empty() ? std::string{} : detail::config_subject(config.filter_subject, "filter_subject");
    std::vector<std::string> filter_subjects;
    filter_subjects.reserve(config.filter_subjects.size());
    for (const auto& f : config.filter_subjects)
      filter_subjects.push_back(detail::config_subject(f, "filter_subjects"));
    std::string deliver_subject;
    std::string deliver_group;
    if (config.type == js_consumer_type::push) {
      deliver_subject = detail::config_subject(config.deliver_subject, "deliver_subject");
      deliver_group = detail::name(config.deliver_group, "deliver_group", true);
    }

    jsConsumerConfig consumer_config{};
    throw_on_error(jsConsumerConfig_Init(&consumer_config), "jsConsumerConfig_Init");
    consumer_config.Durable = durable.c_str();
    consumer_config.Description = config.description ? description.c_str() : nullptr;
    consumer_config.DeliverPolicy = static_cast<jsDeliverPolicy>(config.deliver_policy);
    consumer_config.AckPolicy = static_cast<jsAckPolicy>(config.ack_policy);
    consumer_config.ReplayPolicy = static_cast<jsReplayPolicy>(config.replay_policy);
    consumer_config.HeadersOnly = config.headers_only;

    if (!filter_subject.empty()) consumer_config.FilterSubject = filter_subject.c_str();
    if (config.ack_wait_ns > 0) consumer_config.AckWait = config.ack_wait_ns;
    if (config.max_deliver >= 0) consumer_config.MaxDeliver = config.max_deliver;
    if (config.max_ack_pending >= 0) consumer_config.MaxAckPending = config.max_ack_pending;
    if (config.max_waiting >= 0) consumer_config.MaxWaiting = config.max_waiting;
    if (config.inactive_threshold_ns > 0) consumer_config.InactiveThreshold = config.inactive_threshold_ns;
    if (config.replicas > 0) consumer_config.Replicas = config.replicas;
    if (config.opt_start_seq > 0) consumer_config.OptStartSeq = config.opt_start_seq;
    if (config.opt_start_time_ns > 0) consumer_config.OptStartTime = config.opt_start_time_ns;

    if (config.type == js_consumer_type::push) {
      consumer_config.DeliverSubject = deliver_subject.c_str();
      consumer_config.DeliverGroup = deliver_group.empty() ? nullptr : deliver_group.c_str();
    }

    // Multi-filter subjects (alternative to single FilterSubject).
    std::vector<const char*> filter_subject_ptrs;
    if (!filter_subjects.empty()) {
      filter_subject_ptrs.reserve(filter_subjects.size());
      for (const auto& f : filter_subjects) filter_subject_ptrs.push_back(f.c_str());
      consumer_config.FilterSubjects = filter_subject_ptrs.data();
      consumer_config.FilterSubjectsLen = static_cast<int>(filter_subject_ptrs.size());
    }

    jsConsumerInfo* consumer_info_raw{};
    if (update) {
      throw_on_error(js_UpdateConsumer(&consumer_info_raw, ctx_, stream.c_str(), &consumer_config, nullptr, nullptr),
                     "js_UpdateConsumer");
    } else {
      throw_on_error(js_AddConsumer(&consumer_info_raw, ctx_, stream.c_str(), &consumer_config, nullptr, nullptr),
                     "js_AddConsumer");
    }

    consumer_info out{
        .stream_name = config.stream,
        .durable_name = config.durable_name,
    };
    if (consumer_info_raw != nullptr) {
      std::unique_ptr<jsConsumerInfo, void (*)(jsConsumerInfo*)> holder(consumer_info_raw, jsConsumerInfo_Destroy);
      out.stream_name = consumer_info_raw->Stream != nullptr ? consumer_info_raw->Stream : out.stream_name;
      out.durable_name = consumer_info_raw->Name != nullptr ? consumer_info_raw->Name : out.durable_name;
    }

    return out;
  }

  jsCtx* ctx_{};
};

}  // namespace natscpp
