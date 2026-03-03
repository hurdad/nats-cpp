#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>

namespace natscpp {

// ---------------------------------------------------------------------------
// Policy enumerations — thin wrappers over the nats.c C enums.
// ---------------------------------------------------------------------------

enum class js_storage_type : int {
  file   = js_FileStorage,
  memory = js_MemoryStorage,
};

enum class js_retention_policy : int {
  limits     = js_LimitsPolicy,
  interest   = js_InterestPolicy,
  work_queue = js_WorkQueuePolicy,
};

enum class js_discard_policy : int {
  old      = js_DiscardOld,
  new_msgs = js_DiscardNew,
};

enum class js_deliver_policy : int {
  all             = js_DeliverAll,
  last            = js_DeliverLast,
  new_msgs        = js_DeliverNew,
  by_start_seq    = js_DeliverByStartSequence,
  by_start_time   = js_DeliverByStartTime,
  last_per_subject = js_DeliverLastPerSubject,
};

enum class js_ack_policy : int {
  explicit_ = js_AckExplicit,
  none      = js_AckNone,
  all       = js_AckAll,
};

enum class js_replay_policy : int {
  instant  = js_ReplayInstant,
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
  uint64_t    expected_last_seq         = 0;
  uint64_t    expected_last_subject_seq = 0;
  bool        expect_no_message         = false;
  int64_t     max_wait_ms               = 0;  ///< 0 = use context default
  int64_t     msg_ttl_ms                = 0;  ///< 0 = no TTL (requires nats-server 2.11+)
};

/**
 * @brief Publish acknowledgment returned by js_Publish.
 */
struct js_pub_ack {
  std::string stream;
  uint64_t    sequence  = 0;
  std::string domain;
  bool        duplicate = false;
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
  std::string                     name;
  std::vector<std::string>        subjects;
  std::optional<std::string>      description;
  js_storage_type                 storage             = js_storage_type::file;
  js_retention_policy             retention           = js_retention_policy::limits;
  js_discard_policy               discard             = js_discard_policy::old;
  int64_t                         max_consumers       = -1;
  int64_t                         max_msgs            = -1;
  int64_t                         max_bytes           = -1;
  int64_t                         max_age_ns          = 0;   ///< nanoseconds; 0 = unlimited
  int64_t                         max_msgs_per_subject = -1;
  int32_t                         max_msg_size        = -1;  ///< bytes; -1 = unlimited
  int64_t                         replicas            = 1;
  bool                            no_ack              = false;
  int64_t                         duplicates_ns       = 0;   ///< dedup window in nanoseconds
  bool                            allow_direct        = false;
  bool                            deny_delete         = false;
  bool                            deny_purge          = false;
};

/**
 * @brief Stream metadata returned by stream management operations.
 */
struct stream_info {
  std::string name;
  std::string description;
  uint64_t    messages   = 0;
  uint64_t    bytes      = 0;
  uint64_t    first_seq  = 0;
  uint64_t    last_seq   = 0;
  int64_t     consumers  = 0;
};

// ---------------------------------------------------------------------------
// Consumer configuration and info
// ---------------------------------------------------------------------------

/**
 * @brief Configuration used to create or update a JetStream consumer.
 */
struct js_consumer_config {
  std::string                 stream;
  std::string                 durable_name;
  std::string                 filter_subject;
  std::string                 deliver_subject;          ///< push consumers
  std::string                 deliver_group;            ///< push consumers (queue group)
  js_consumer_type            type                = js_consumer_type::pull;
  js_deliver_policy           deliver_policy      = js_deliver_policy::all;
  js_ack_policy               ack_policy          = js_ack_policy::explicit_;
  js_replay_policy            replay_policy       = js_replay_policy::instant;
  int64_t                     ack_wait_ns         = 0;   ///< nanoseconds; 0 = server default
  int64_t                     max_deliver         = -1;  ///< -1 = unlimited
  int64_t                     max_ack_pending     = -1;  ///< -1 = server default
  int64_t                     max_waiting         = -1;  ///< pull consumers; -1 = server default
  int64_t                     inactive_threshold_ns = 0; ///< ephemeral consumer inactivity; 0 = server default
  int64_t                     replicas            = 0;   ///< 0 = inherit from stream
  bool                        headers_only        = false;
  uint64_t                    opt_start_seq       = 0;   ///< for deliver_policy::by_start_seq
  int64_t                     opt_start_time_ns   = 0;   ///< for deliver_policy::by_start_time (UTC nanoseconds)
  std::vector<std::string>    filter_subjects;           ///< multi-filter alternative to filter_subject
  std::optional<std::string>  description;
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
  uint64_t memory    = 0;
  uint64_t store     = 0;
  int64_t  streams   = 0;
  int64_t  consumers = 0;
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
    if (sub_ == nullptr) throw nats_error(NATS_INVALID_ARG, "js_pull_consumer: not initialized");
    natsMsgList list{};
    throw_on_error(natsSubscription_Fetch(&list, sub_, 1, static_cast<int64_t>(timeout.count()), nullptr),
                   "natsSubscription_Fetch");
    struct list_guard { natsMsgList& l; ~list_guard() { natsMsgList_Destroy(&l); } } guard{list};
    if (list.Count < 1 || list.Msgs == nullptr) {
      throw nats_error(NATS_TIMEOUT, "natsSubscription_Fetch returned no messages");
    }
    natsMsg* msg = list.Msgs[0];
    list.Msgs[0] = nullptr;
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
    jsPubOptions pub_opts{};
    bool has_opts = false;
    if (!opts.msg_id.empty())                { pub_opts.MsgId = opts.msg_id.c_str(); has_opts = true; }
    if (!opts.expected_stream.empty())       { pub_opts.ExpectStream = opts.expected_stream.c_str(); has_opts = true; }
    if (!opts.expected_last_msg_id.empty())  { pub_opts.ExpectLastMsgId = opts.expected_last_msg_id.c_str(); has_opts = true; }
    if (opts.expected_last_seq > 0)          { pub_opts.ExpectLastSeq = opts.expected_last_seq; has_opts = true; }
    if (opts.expected_last_subject_seq > 0)  { pub_opts.ExpectLastSubjectSeq = opts.expected_last_subject_seq; has_opts = true; }
    if (opts.expect_no_message)              { pub_opts.ExpectNoMessage = true; has_opts = true; }
    if (opts.max_wait_ms > 0)                { pub_opts.MaxWait = opts.max_wait_ms; has_opts = true; }
    if (opts.msg_ttl_ms > 0)                 { pub_opts.MsgTTL = opts.msg_ttl_ms; has_opts = true; }

    jsPubAck* ack{};
    throw_on_error(js_Publish(&ack, ctx_, std::string(subject).c_str(), payload.data(),
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
    bool has_opts = false;
    if (!opts.msg_id.empty())                { pub_opts.MsgId = opts.msg_id.c_str(); has_opts = true; }
    if (!opts.expected_stream.empty())       { pub_opts.ExpectStream = opts.expected_stream.c_str(); has_opts = true; }
    if (!opts.expected_last_msg_id.empty())  { pub_opts.ExpectLastMsgId = opts.expected_last_msg_id.c_str(); has_opts = true; }
    if (opts.expected_last_seq > 0)          { pub_opts.ExpectLastSeq = opts.expected_last_seq; has_opts = true; }
    if (opts.expected_last_subject_seq > 0)  { pub_opts.ExpectLastSubjectSeq = opts.expected_last_subject_seq; has_opts = true; }
    if (opts.expect_no_message)              { pub_opts.ExpectNoMessage = true; has_opts = true; }
    if (opts.max_wait_ms > 0)                { pub_opts.MaxWait = opts.max_wait_ms; has_opts = true; }
    if (opts.msg_ttl_ms > 0)                 { pub_opts.MsgTTL = opts.msg_ttl_ms; has_opts = true; }

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
    throw_on_error(js_PullSubscribe(&sub, ctx_, std::string(stream_subject).c_str(),
                                    std::string(durable_name).c_str(), nullptr, nullptr, nullptr),
                   "js_PullSubscribe");
    return js_pull_consumer{sub};
  }

  [[nodiscard]] js_push_consumer push_subscribe(std::string_view stream_subject, std::string_view durable_name) {
    check_valid();
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
    check_valid();
    return push_subscribe(stream_subject, durable_name);
  }

  // -------------------------------------------------------------------------
  // Stream management
  // -------------------------------------------------------------------------

  [[nodiscard]] stream_info create_stream(const js_stream_config& config) {
    check_valid();
    if (config.name.empty()) {
      throw std::invalid_argument("stream name cannot be empty");
    }
    auto [subjects, native_cfg] = build_stream_config(config);
    jsStreamInfo* si_raw{};
    throw_on_error(js_AddStream(&si_raw, ctx_, &native_cfg, nullptr, nullptr), "js_AddStream");
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
    auto [subjects, native_cfg] = build_stream_config(config);
    jsStreamInfo* si_raw{};
    throw_on_error(js_UpdateStream(&si_raw, ctx_, &native_cfg, nullptr, nullptr), "js_UpdateStream");
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
    throw_on_error(js_DeleteStream(ctx_, std::string(name).c_str(), nullptr, nullptr), "js_DeleteStream");
  }

  void purge_stream(std::string_view name) {
    check_valid();
    if (name.empty()) throw std::invalid_argument("stream name cannot be empty");
    throw_on_error(js_PurgeStream(ctx_, std::string(name).c_str(), nullptr, nullptr), "js_PurgeStream");
  }

  [[nodiscard]] stream_info get_stream_info(std::string_view name) {
    check_valid();
    if (name.empty()) throw std::invalid_argument("stream name cannot be empty");
    jsStreamInfo* si_raw{};
    throw_on_error(js_GetStreamInfo(&si_raw, ctx_, std::string(name).c_str(), nullptr, nullptr), "js_GetStreamInfo");
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
    throw_on_error(js_GetMsg(&msg, ctx_, std::string(stream).c_str(), seq, nullptr, nullptr), "js_GetMsg");
    return message{msg};
  }

  /**
   * @brief Fetch the last message published to a specific subject in a stream.
   */
  [[nodiscard]] message get_last_msg(std::string_view stream, std::string_view subject) {
    check_valid();
    natsMsg* msg{};
    throw_on_error(js_GetLastMsg(&msg, ctx_, std::string(stream).c_str(), std::string(subject).c_str(), nullptr, nullptr),
                   "js_GetLastMsg");
    return message{msg};
  }

  /**
   * @brief Delete a message from a stream by sequence number.
   */
  void delete_msg(std::string_view stream, uint64_t seq) {
    check_valid();
    throw_on_error(js_DeleteMsg(ctx_, std::string(stream).c_str(), seq, nullptr, nullptr), "js_DeleteMsg");
  }

  /**
   * @brief Securely erase a message from a stream by sequence number.
   */
  void erase_msg(std::string_view stream, uint64_t seq) {
    check_valid();
    throw_on_error(js_EraseMsg(ctx_, std::string(stream).c_str(), seq, nullptr, nullptr), "js_EraseMsg");
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
    check_valid();
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
    check_valid();
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
    check_valid();
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
      out.memory    = raw->Memory;
      out.store     = raw->Store;
      out.streams   = raw->Streams;
      out.consumers = raw->Consumers;
    }
    return out;
  }

 private:
  void check_valid() const {
    if (ctx_ == nullptr) throw nats_error(NATS_INVALID_ARG, "jetstream: not initialized");
  }

  // Extracts a stream_info from a raw jsStreamInfo pointer (never null-checks — caller must ensure).
  static stream_info extract_stream_info(const jsStreamInfo* si) {
    stream_info out;
    if (si == nullptr) return out;
    if (si->Config != nullptr) {
      out.name        = si->Config->Name        != nullptr ? si->Config->Name        : "";
      out.description = si->Config->Description != nullptr ? si->Config->Description : "";
    }
    out.messages  = si->State.Msgs;
    out.bytes     = si->State.Bytes;
    out.first_seq = si->State.FirstSeq;
    out.last_seq  = si->State.LastSeq;
    out.consumers = si->State.Consumers;
    return out;
  }

  // Extracts and destroys a jsPubAck pointer.
  static js_pub_ack extract_pub_ack(jsPubAck* raw) {
    js_pub_ack out;
    if (raw != nullptr) {
      out.stream    = raw->Stream    != nullptr ? raw->Stream    : "";
      out.sequence  = raw->Sequence;
      out.domain    = raw->Domain    != nullptr ? raw->Domain    : "";
      out.duplicate = raw->Duplicate;
      jsPubAck_Destroy(raw);
    }
    return out;
  }

  // Returns {subjects_storage, native_config} — subjects_storage must outlive native_config.
  static std::pair<std::vector<const char*>, jsStreamConfig> build_stream_config(const js_stream_config& config) {
    std::vector<const char*> subjects;
    subjects.reserve(config.subjects.size());
    for (const auto& s : config.subjects) subjects.push_back(s.c_str());

    jsStreamConfig native{};
    native.Name        = config.name.c_str();
    native.Subjects    = subjects.empty() ? nullptr : subjects.data();
    native.SubjectsLen = static_cast<int>(subjects.size());
    native.Description = config.description ? config.description->c_str() : nullptr;
    native.Storage     = static_cast<jsStorageType>(config.storage);
    native.Retention   = static_cast<jsRetentionPolicy>(config.retention);
    native.Discard     = static_cast<jsDiscardPolicy>(config.discard);
    native.MaxConsumers      = config.max_consumers;
    native.MaxMsgs           = config.max_msgs;
    native.MaxBytes          = config.max_bytes;
    native.MaxAge            = config.max_age_ns;
    native.MaxMsgsPerSubject = config.max_msgs_per_subject;
    native.MaxMsgSize        = config.max_msg_size;
    native.Replicas          = config.replicas;
    native.NoAck             = config.no_ack;
    native.Duplicates        = config.duplicates_ns;
    native.AllowDirect       = config.allow_direct;
    native.DenyDelete        = config.deny_delete;
    native.DenyPurge         = config.deny_purge;

    return {std::move(subjects), native};
  }

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
    consumer_config.Durable       = config.durable_name.c_str();
    consumer_config.Description   = config.description ? config.description->c_str() : nullptr;
    consumer_config.DeliverPolicy = static_cast<jsDeliverPolicy>(config.deliver_policy);
    consumer_config.AckPolicy     = static_cast<jsAckPolicy>(config.ack_policy);
    consumer_config.ReplayPolicy  = static_cast<jsReplayPolicy>(config.replay_policy);
    consumer_config.HeadersOnly   = config.headers_only;

    if (!config.filter_subject.empty()) {
      consumer_config.FilterSubject = config.filter_subject.c_str();
    }
    if (config.ack_wait_ns > 0)          consumer_config.AckWait           = config.ack_wait_ns;
    if (config.max_deliver >= 0)         consumer_config.MaxDeliver         = config.max_deliver;
    if (config.max_ack_pending >= 0)     consumer_config.MaxAckPending      = config.max_ack_pending;
    if (config.max_waiting >= 0)         consumer_config.MaxWaiting         = config.max_waiting;
    if (config.inactive_threshold_ns > 0) consumer_config.InactiveThreshold = config.inactive_threshold_ns;
    if (config.replicas > 0)             consumer_config.Replicas           = config.replicas;
    if (config.opt_start_seq > 0)        consumer_config.OptStartSeq        = config.opt_start_seq;
    if (config.opt_start_time_ns > 0)    consumer_config.OptStartTime       = config.opt_start_time_ns;

    if (config.type == js_consumer_type::push) {
      consumer_config.DeliverSubject = config.deliver_subject.c_str();
      consumer_config.DeliverGroup   = config.deliver_group.empty() ? nullptr : config.deliver_group.c_str();
    }

    // Multi-filter subjects (alternative to single FilterSubject).
    std::vector<const char*> filter_subject_ptrs;
    if (!config.filter_subjects.empty()) {
      filter_subject_ptrs.reserve(config.filter_subjects.size());
      for (const auto& s : config.filter_subjects) filter_subject_ptrs.push_back(s.c_str());
      consumer_config.FilterSubjects    = filter_subject_ptrs.data();
      consumer_config.FilterSubjectsLen = static_cast<int>(filter_subject_ptrs.size());
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
      out.stream_name  = consumer_info_raw->Stream != nullptr ? consumer_info_raw->Stream : out.stream_name;
      out.durable_name = consumer_info_raw->Name   != nullptr ? consumer_info_raw->Name   : out.durable_name;
      jsConsumerInfo_Destroy(consumer_info_raw);
    }

    return out;
  }

  jsCtx* ctx_{};
};

}  // namespace natscpp
